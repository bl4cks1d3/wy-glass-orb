#include "orb_sync.h"
#include "application.h"
#include "board.h"
#include "display/display.h"
#include "display/orb_ui.h"
#include "mcp_server.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include <cstring>
#include <memory>

#define TAG "OrbSync"

namespace {

constexpr int kCheckEveryMs = 60 * 1000;

// SRAM interna livre agora, a menor desde o boot e o maior bloco (a troca de orb roda no limite)
void LogRam(const char* step) {
    ESP_LOGD(TAG, "RAM %s: livre %u, minima %u, maior bloco %u", step,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
constexpr size_t kChunk = 4096;

// GET que devolve o corpo inteiro (respostas pequenas, como /orbs/current)
bool HttpGetSmall(const std::string& url, std::string& body) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 8000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    Settings websocket("websocket", false);
    std::string token = websocket.GetString("token");
    if (!token.empty()) {
        esp_http_client_set_header(client, "Authorization", ("Bearer " + token).c_str());
    }
    bool ok = esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0 &&
              esp_http_client_get_status_code(client) == 200;
    char buf[256];
    int n;
    while (ok && (n = esp_http_client_read(client, buf, sizeof(buf))) > 0 && body.size() < 4096) {
        body.append(buf, n);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

std::string ToHex(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < len; i++) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 15];
    }
    return out;
}

}  // namespace

void OrbSync::SetServer(const std::string& ws_url) {
    size_t scheme = ws_url.find("://");
    if (scheme == std::string::npos) {
        return;
    }
    size_t path = ws_url.find('/', scheme + 3);
    std::string host = ws_url.substr(scheme + 3, path == std::string::npos ? std::string::npos : path - scheme - 3);
    base_ = (ws_url.rfind("wss://", 0) == 0 ? "https://" : "http://") + host;
}

void OrbSync::Start() {
    if (base_.empty() || task_ != nullptr) {
        return;
    }
    // Pilha na SRAM interna: gravar na flash desliga o cache, e a PSRAM some nesse tempo
    xTaskCreate([](void* arg) {
        static_cast<OrbSync*>(arg)->Loop();
    }, "orb_sync", 5120, this, 2, &task_);
}

void OrbSync::CheckNow() {
    if (task_ != nullptr) {
        xTaskNotifyGive(task_);
    }
}

void OrbSync::RegisterMcpTool() {
    McpServer::GetInstance().AddTool("self.orb.update",
        "Check the server orb library and switch the round screen to the current orb now "
        "(the server chose it; the download takes a few seconds and the screen shows TROCANDO ORB).",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            GetInstance().CheckNow();
            return true;
        });
}

void OrbSync::Loop() {
    vTaskDelay(pdMS_TO_TICKS(3000));  // deixa o boot terminar
    Check();
    while (true) {
        bool asked = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kCheckEveryMs)) > 0;
        // Sozinho, so confere com o orb parado; pedido pelo servidor, confere na hora
        if (asked || Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            Check();
        }
    }
}

void OrbSync::Check() {
    std::string body;
    LogRam("antes da consulta");
    if (!HttpGetSmall(base_ + "/orbs/current", body)) {
        ESP_LOGD(TAG, "Servidor sem biblioteca de orbs");
        return;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    const cJSON* id = cJSON_GetObjectItem(root, "id");
    const cJSON* size = cJSON_GetObjectItem(root, "size");
    const cJSON* sha = cJSON_GetObjectItem(root, "sha256");
    if (cJSON_IsString(id) && cJSON_IsNumber(size) && cJSON_IsString(sha)) {
        Settings orb("orb", false);
        if (orb.GetString("sha256") != sha->valuestring) {
            ESP_LOGI(TAG, "Servidor pede o orb \"%s\" (%d KB)", id->valuestring, size->valueint / 1024);
            Download(id->valuestring, size->valueint, sha->valuestring);
        }
    }
    cJSON_Delete(root);
}

bool OrbSync::Download(const std::string& id, size_t size, const std::string& sha256) {
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "orbs");
    if (part == nullptr || size < 16 || size > part->size) {
        ESP_LOGE(TAG, "Orb nao cabe na particao");
        return false;
    }
    auto display = Board::GetInstance().GetDisplay();
    OrbUi* ui = OrbUi::Instance();
    if (ui) {
        DisplayLockGuard lock(display);
        ui->SetSwapping(true);  // para de ler a particao enquanto ela e regravada
    }

    // Na PSRAM: a SRAM interna ja fica no limite com Wi-Fi e audio (a gravacao na flash aceita PSRAM)
    std::unique_ptr<uint8_t, decltype(&free)> buf((uint8_t*)heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM), &free);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint8_t magic[4] = {};
    size_t written = 0;
    bool ok = buf != nullptr && esp_partition_erase_range(part, 0, (size + 4095) & ~4095u) == ESP_OK;
    LogRam("depois de apagar");

    esp_http_client_config_t config = {};
    std::string url = base_ + "/orbs/" + id + ".orb";
    config.url = url.c_str();
    config.timeout_ms = 10000;
    config.buffer_size = 1024;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    Settings websocket("websocket", false);
    std::string token = websocket.GetString("token");
    if (!token.empty()) {
        esp_http_client_set_header(client, "Authorization", ("Bearer " + token).c_str());
    }
    ok = ok && esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0 &&
         esp_http_client_get_status_code(client) == 200;
    LogRam("conexao aberta");
    while (ok && written < size) {
        if (written % (256 * 1024) < kChunk) {
            LogRam("baixando");
        }
        int n = esp_http_client_read(client, (char*)buf.get(), kChunk);
        if (n <= 0) {
            ok = false;
            break;
        }
        mbedtls_sha256_update(&sha, buf.get(), n);
        // Os 4 bytes "ORB1" ficam por ultimo: um download cortado nunca parece um orb valido
        size_t skip = 0;
        if (written < 4) {
            skip = std::min<size_t>(4 - written, n);
            memcpy(magic + written, buf.get(), skip);
        }
        if (n > (int)skip && esp_partition_write(part, written + skip, buf.get() + skip, n - skip) != ESP_OK) {
            ok = false;
        }
        written += n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    LogRam("fim do download");

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    ok = ok && written == size && ToHex(digest, sizeof(digest)) == sha256 && memcmp(magic, "ORB1", 4) == 0 &&
         esp_partition_write(part, 0, magic, 4) == ESP_OK;
    if (ok) {
        Settings orb("orb", true);
        orb.SetString("sha256", sha256);
        orb.SetString("id", id);
        ESP_LOGI(TAG, "Orb \"%s\" instalado (%u KB)", id.c_str(), (unsigned)(size / 1024));
    } else {
        ESP_LOGE(TAG, "Falha ao baixar o orb \"%s\" (%u de %u bytes)", id.c_str(), (unsigned)written, (unsigned)size);
    }
    if (ui) {
        DisplayLockGuard lock(display);
        ui->ReloadPack();  // com falha, o arquivo ficou invalido: volta ao orb compilado
        ui->SetSwapping(false);
    }
    return ok;
}
