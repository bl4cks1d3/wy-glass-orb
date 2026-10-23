// Teste das telas interativas do firmware do orb no QEMU (esp32s3):
//  - tela: painel RGB virtual do QEMU (esp_lcd_qemu_rgb), 240x240, com LVGL;
//  - rede: Ethernet emulada (open_eth), o host fica em 10.0.2.2;
//  - protocolo: o mesmo WebSocket do Xiaozhi (hello + mensagens type "mcp").
// O servidor (probes/esp32_screen_layout_qemu.py) manda self.screen.render e simula
// toques com {"type":"wyglass_test","action":"tap"}; os ui_event voltam pelo WebSocket.
#include "display.h"
#include "screen_layout.h"
#include "mcp_server.h"
#include "application.h"

#include <esp_lcd_qemu_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_websocket_client.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <protocol_examples_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

#define TAG "screen_layout_qemu"

#define WIDTH 240
#define HEIGHT 240
#define SERVER_URI "ws://10.0.2.2:8765/xiaozhi/v1/"

class TestDisplay : public Display {
public:
    TestDisplay() {
        mutex_ = xSemaphoreCreateRecursiveMutex();
        width_ = WIDTH;
        height_ = HEIGHT;
    }
    bool Lock(int timeout_ms = 0) override {
        return xSemaphoreTakeRecursive(mutex_, timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY) == pdTRUE;
    }
    void Unlock() override { xSemaphoreGiveRecursive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};

static TestDisplay* display_ = nullptr;
static esp_lcd_panel_handle_t panel_ = nullptr;
static esp_websocket_client_handle_t ws_ = nullptr;
static QueueHandle_t inbox_ = nullptr;
static std::string session_id_ = "qemu";

// Toque simulado: o servidor pede e o LVGL le por um indev de ponteiro, como na placa.
static std::atomic<bool> touch_pressed_{false};
static std::atomic<int> touch_x_{0};
static std::atomic<int> touch_y_{0};

void Application::SendMcpMessage(const std::string& payload) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    if (!ws_ || !esp_websocket_client_is_connected(ws_)) {
        ESP_LOGW(TAG, "not connected, dropping: %s", message.c_str());
        return;
    }
    esp_websocket_client_send_text(ws_, message.c_str(), message.size(), pdMS_TO_TICKS(2000));
}

// draw_bitmap espera o QEMU confirmar a copia, o que so acontece com uma janela grafica
// ativa (-display sdl); com -display none ele trava aqui.
static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_lcd_panel_draw_bitmap(panel_, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    data->point.x = touch_x_;
    data->point.y = touch_y_;
    data->state = touch_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t tick_cb() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

static void lvgl_task(void* arg) {
    while (true) {
        display_->Lock();
        uint32_t wait_ms = lv_timer_handler();
        display_->Unlock();
        vTaskDelay(pdMS_TO_TICKS(LV_CLAMP(5, wait_ms, 30)));
    }
}

static void init_display() {
    esp_lcd_rgb_qemu_config_t config = {.width = WIDTH, .height = HEIGHT, .bpp = RGB_QEMU_BPP_16};
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&config, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));

    display_ = new TestDisplay();
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t* disp = lv_display_create(WIDTH, HEIGHT);
    size_t buf_size = WIDTH * 40 * sizeof(uint16_t);
    lv_display_set_buffers(disp, heap_caps_malloc(buf_size, MALLOC_CAP_DEFAULT), nullptr, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    // Fundo no lugar da interface de conversa do Xiaozhi.
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xAAAAAA), 0);
    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text(label, "chat UI (Xiaozhi)");
    lv_obj_center(label);

    xTaskCreate(lvgl_task, "lvgl", 8192, nullptr, 5, nullptr);
}

static lv_obj_t* find_nth(lv_obj_t* parent, const lv_obj_class_t* cls, int* n) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, cls) && (*n)-- == 0) return child;
        lv_obj_t* found = find_nth(child, cls, n);
        if (found) return found;
    }
    return nullptr;
}

static void tap(const char* kind, int index) {
    lv_area_t area;
    {
        DisplayLockGuard lock(display_);
        const lv_obj_class_t* cls = strcmp(kind, "item") == 0 ? &lv_checkbox_class : &lv_button_class;
        lv_obj_t* target = find_nth(lv_layer_top(), cls, &index);
        if (!target) {
            ESP_LOGW(TAG, "tap: no %s #%d on screen", kind, index);
            return;
        }
        lv_obj_get_coords(target, &area);
    }
    touch_x_ = (area.x1 + area.x2) / 2;
    touch_y_ = (area.y1 + area.y2) / 2;
    ESP_LOGI(TAG, "tap %s at (%d,%d)", kind, (int)touch_x_, (int)touch_y_);
    touch_pressed_ = true;
    vTaskDelay(pdMS_TO_TICKS(150));
    touch_pressed_ = false;
}

// Mensagens do servidor sao tratadas fora da task do WebSocket, para poder responder por ele.
static void inbox_task(void* arg) {
    std::string* text;
    while (xQueueReceive(inbox_, &text, portMAX_DELAY)) {
        cJSON* root = cJSON_Parse(text->c_str());
        const cJSON* type = cJSON_GetObjectItem(root, "type");
        if (cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "hello") == 0) {
                const cJSON* session = cJSON_GetObjectItem(root, "session_id");
                if (cJSON_IsString(session)) session_id_ = session->valuestring;
                ESP_LOGI(TAG, "server hello, session %s", session_id_.c_str());
            } else if (strcmp(type->valuestring, "mcp") == 0) {
                McpServer::GetInstance().ParseMessage(cJSON_GetObjectItem(root, "payload"));
            } else if (strcmp(type->valuestring, "wyglass_test") == 0) {
                const cJSON* kind = cJSON_GetObjectItem(root, "kind");
                const cJSON* index = cJSON_GetObjectItem(root, "index");
                tap(cJSON_IsString(kind) ? kind->valuestring : "button", cJSON_IsNumber(index) ? index->valueint : 0);
            }
        }
        cJSON_Delete(root);
        delete text;
    }
}

static void ws_event(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    static std::string rx;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "websocket connected");
        const char* hello = "{\"type\":\"hello\",\"version\":1,\"features\":{\"mcp\":true},\"transport\":\"websocket\","
                            "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}";
        esp_websocket_client_send_text(ws_, hello, strlen(hello), pdMS_TO_TICKS(2000));
        break;
    }
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code != 0x1 && data->op_code != 0x0) break;
        if (data->payload_offset == 0) rx.clear();
        rx.append(data->data_ptr, data->data_len);
        if (data->payload_offset + data->data_len >= data->payload_len) {
            auto* text = new std::string(rx);
            xQueueSend(inbox_, &text, portMAX_DELAY);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "websocket disconnected");
        break;
    default:
        break;
    }
}

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_display();
    ScreenLayout::RegisterMcpTools(display_);

    ESP_ERROR_CHECK(example_connect());

    inbox_ = xQueueCreate(8, sizeof(std::string*));
    xTaskCreate(inbox_task, "inbox", 8192, nullptr, 5, nullptr);

    esp_websocket_client_config_t config = {};
    config.uri = SERVER_URI;
    config.headers = "Authorization: Bearer qemu-test\r\nProtocol-Version: 1\r\n"
                     "Device-Id: 02:00:00:00:00:01\r\nClient-Id: qemu-screen-layout\r\n";
    config.buffer_size = 4096;
    config.reconnect_timeout_ms = 2000;
    ws_ = esp_websocket_client_init(&config);
    esp_websocket_register_events(ws_, WEBSOCKET_EVENT_ANY, ws_event, nullptr);
    ESP_ERROR_CHECK(esp_websocket_client_start(ws_));
    ESP_LOGI(TAG, "connecting to %s", SERVER_URI);
}
