#include "orb_pack.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <jpeg_decoder.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "OrbPack"

namespace {

constexpr esp_partition_subtype_t kSubtype = (esp_partition_subtype_t)0x40;
constexpr size_t kWorkSize = 3100;  // area de trabalho recomendada do esp_jpeg
constexpr const char* kStateNames[OrbPack::kStateCount] = {"starting", "idle", "connecting", "listening",
                                                         "speaking", "error"};

uint32_t ParseColor(const cJSON* item, uint32_t fallback) {
    if (!cJSON_IsString(item) || item->valuestring[0] != '#') {
        return fallback;
    }
    return (uint32_t)strtoul(item->valuestring + 1, nullptr, 16);
}

}  // namespace

OrbPack::~OrbPack() {
    heap_caps_free(jpeg_);
    heap_caps_free(work_);
}

bool OrbPack::Load() {
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kSubtype, "orbs");
    if (partition_ == nullptr) {
        ESP_LOGW(TAG, "Sem particao orbs: usando o orb compilado");
        return false;
    }
    uint8_t head[12];
    if (esp_partition_read(partition_, 0, head, sizeof(head)) != ESP_OK || memcmp(head, "ORB1", 4) != 0) {
        ESP_LOGI(TAG, "Nenhum arquivo .orb gravado: usando o orb compilado");
        return false;
    }
    uint32_t total;
    uint16_t header_len;
    memcpy(&total, head + 4, 4);
    memcpy(&header_len, head + 8, 2);
    if (total > partition_->size || header_len == 0 || header_len > 8192) {
        ESP_LOGE(TAG, "Cabecalho do .orb invalido");
        return false;
    }

    std::string json(header_len, '\0');
    esp_partition_read(partition_, 12, json.data(), header_len);
    cJSON* root = cJSON_ParseWithLength(json.c_str(), header_len);
    if (root == nullptr) {
        ESP_LOGE(TAG, "JSON do .orb invalido");
        return false;
    }
    const cJSON* v = cJSON_GetObjectItem(root, "v");
    const cJSON* width = cJSON_GetObjectItem(root, "width");
    const cJSON* height = cJSON_GetObjectItem(root, "height");
    bool ok = cJSON_IsNumber(v) && v->valueint == 1 && cJSON_IsNumber(width) && width->valueint == kSize &&
              cJSON_IsNumber(height) && height->valueint == kSize;
    if (ok) {
        const cJSON* name = cJSON_GetObjectItem(root, "name");
        name_ = cJSON_IsString(name) ? name->valuestring : "orb";
        const cJSON* fps = cJSON_GetObjectItem(root, "fps");
        fps_ = cJSON_IsNumber(fps) ? std::clamp(fps->valueint, 1, 30) : 15;
        const cJSON* bg = cJSON_GetObjectItem(root, "background");
        for (int i = 0; i < 3 && cJSON_IsArray(bg); i++) {
            background_[i] = ParseColor(cJSON_GetArrayItem(bg, i), background_[i]);
        }
        const cJSON* states = cJSON_GetObjectItem(root, "states");
        for (int s = 0; s < kStateCount; s++) {
            const cJSON* range = cJSON_GetObjectItem(states, kStateNames[s]);
            if (cJSON_IsArray(range) && cJSON_GetArraySize(range) == 2) {
                first_[s] = cJSON_GetArrayItem(range, 0)->valueint;
                count_[s] = cJSON_GetArrayItem(range, 1)->valueint;
            }
        }
        ok = count_[kIdle] > 0;
    }
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "O .orb precisa ser v1, 192x192 e ter quadros do estado idle");
        return false;
    }

    uint32_t frames = 0;
    esp_partition_read(partition_, 12 + header_len, &frames, 4);
    if (frames == 0 || frames > 4000) {
        ESP_LOGE(TAG, "Numero de quadros invalido: %lu", (unsigned long)frames);
        return false;
    }
    std::vector<uint32_t> index(frames * 2);
    esp_partition_read(partition_, 12 + header_len + 4, index.data(), frames * 8);
    offset_.resize(frames);
    size_.resize(frames);
    uint32_t biggest = 0;
    for (uint32_t i = 0; i < frames; i++) {
        offset_[i] = index[i * 2];
        size_[i] = index[i * 2 + 1];
        if (offset_[i] + size_[i] > total || size_[i] == 0) {
            ESP_LOGE(TAG, "Indice de quadros invalido");
            return false;
        }
        biggest = std::max(biggest, size_[i]);
    }
    for (int s = 0; s < kStateCount; s++) {
        if (first_[s] + count_[s] > (int)frames) {
            count_[s] = 0;
        }
    }

    jpeg_ = (uint8_t*)heap_caps_malloc(biggest, MALLOC_CAP_SPIRAM);
    work_ = (uint8_t*)heap_caps_malloc(kWorkSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (jpeg_ == nullptr || work_ == nullptr) {
        ESP_LOGE(TAG, "Sem memoria para tocar o .orb");
        return false;
    }
    loaded_ = true;
    ESP_LOGI(TAG, "Orb \"%s\": %lu quadros, %d qps, %lu KB", name_.c_str(), (unsigned long)frames, fps_,
             (unsigned long)(total / 1024));
    return true;
}

void OrbPack::Range(State state, int& first, int& count) const {
    if (count_[state] == 0) {
        state = kIdle;
    }
    first = first_[state];
    count = count_[state];
}

int OrbPack::Decode(int index, uint16_t* out) {
    if (!loaded_ || index < 0 || index >= (int)offset_.size()) {
        return -1;
    }
    int64_t start = esp_timer_get_time();
    if (esp_partition_read(partition_, offset_[index], jpeg_, size_[index]) != ESP_OK) {
        return -1;
    }
    esp_jpeg_image_cfg_t cfg = {};
    cfg.indata = jpeg_;
    cfg.indata_size = size_[index];
    cfg.outbuf = (uint8_t*)out;
    cfg.outbuf_size = kSize * kSize * 2;
    cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale = JPEG_IMAGE_SCALE_0;
    cfg.advanced.working_buffer = work_;
    cfg.advanced.working_buffer_size = kWorkSize;
    esp_jpeg_image_output_t img;
    if (esp_jpeg_decode(&cfg, &img) != ESP_OK || img.width != kSize || img.height != kSize) {
        ESP_LOGW(TAG, "Quadro %d nao decodificou", index);
        return -1;
    }
    return (int)(esp_timer_get_time() - start);
}
