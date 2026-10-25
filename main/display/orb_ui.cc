#include "orb_ui.h"
#include "application.h"
#include "board.h"
#include "orb_studio_config.h"
#include "settings.h"
#include "system_info.h"

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#define TAG "OrbUi"

namespace {

constexpr uint32_t kTickMs = 50;
constexpr int kScreen = 240;

// Globo: centro um pouco acima do meio, para o texto caber embaixo
constexpr float kCx = 120.0f;
constexpr float kCy = 110.0f;
constexpr float kR = 86.0f;

// Area redesenhada a cada quadro (esfera + brilho do contorno)
constexpr int kBox = 192;
constexpr int kBoxX = 120 - kBox / 2;
constexpr int kBoxY = 110 - kBox / 2;

// Aparencia do globo e textos: main/orb_studio_config.h, gerado pelo Orb Studio
constexpr int kMeridians = orb_studio::kMeridians;
constexpr int kMeridianSamples = 280;
constexpr int kDots = orb_studio::kDots;
constexpr float kBaseSpeed = orb_studio::kBaseSpeed;  // rad por quadro (0,012 = 0,24 rad/s)

struct Rgb {
    float r, g, b;
};

constexpr Rgb Hex(uint32_t hex) {
    return {(float)((hex >> 16) & 0xFF), (float)((hex >> 8) & 0xFF), (float)(hex & 0xFF)};
}

Rgb Lerp(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb Add(Rgb a, Rgb b, float k) {
    return {a.r + b.r * k, a.g + b.g * k, a.b + b.b * k};
}

// Gradiente de tres paradas (0, mid, 1)
Rgb Gradient(Rgb c0, Rgb c1, Rgb c2, float mid, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < mid ? Lerp(c0, c1, t / mid) : Lerp(c1, c2, (t - mid) / (1.0f - mid));
}

uint16_t Pack565(int r, int g, int b) {
    r = std::min(r, 255);
    g = std::min(g, 255);
    b = std::min(b, 255);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr Rgb kLine = Hex(orb_studio::kLineColor);

// Fundo e partes fixas do globo (esfera, paralelos, contorno) no pixel (x, y)
Rgb StaticPixel(int x, int y) {
    float px = x + 0.5f, py = y + 0.5f;

    float bg_t = std::hypot(px - 120.0f, py - 120.0f) / 170.0f;
    Rgb c = Gradient(Hex(orb_studio::kBackground[0]), Hex(orb_studio::kBackground[1]),
                     Hex(orb_studio::kBackground[2]), 0.45f, bg_t);

    float dx = px - kCx, dy = py - kCy;
    float d = std::hypot(dx, dy);

    // Brilho difuso atras do orb
    float halo = 0.12f * std::exp(-(d / (1.05f * kR)) * (d / (1.05f * kR)) * 2.0f);
    c = Add(c, Hex(orb_studio::kHaloColor), halo);

    float inside = std::clamp(kR + 0.5f - d, 0.0f, 1.0f);
    if (inside > 0) {
        float gx = kCx - 0.3f * kR, gy = kCy - 0.35f * kR;
        float t = std::hypot(px - gx, py - gy) / (1.3f * kR);
        Rgb sphere = Gradient(Hex(orb_studio::kSphere[0]), Hex(orb_studio::kSphere[1]),
                              Hex(orb_studio::kSphere[2]), 0.65f, t);

        // Paralelos: linhas horizontais dentro da esfera
        for (int i = -5; i <= 5; i++) {
            float lat = (i / 6.0f) * (float)M_PI_2;
            float ly = kCy + std::sin(lat) * kR;
            float rx = std::cos(lat) * kR;
            float cover = 1.0f - std::fabs(py - ly) / 1.1f;
            if (cover > 0 && std::fabs(dx) <= rx) {
                sphere = Add(sphere, kLine, 0.45f * cover);
            }
        }
        c = Lerp(c, sphere, inside);
    }

    // Contorno com brilho
    float edge = std::fabs(d - kR);
    float outline = 0.65f * std::clamp(1.0f - edge / 0.9f, 0.0f, 1.0f) + 0.3f * std::exp(-edge / 3.5f);
    return Add(c, kLine, outline);
}

lv_obj_t* MakeLabel(lv_obj_t* parent, lv_color_t color, int letter_space) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_letter_space(label, letter_space, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

lv_obj_t* MakeCircle(lv_obj_t* parent, int diameter, lv_color_t color) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return obj;
}

const lv_color_t kCyan = lv_color_hex(orb_studio::kLineColor);
const lv_color_t kRed = lv_color_hex(0xEF4444);
const lv_color_t kLabel = lv_color_hex(orb_studio::kLabelColor);

}  // namespace

OrbUi::OrbUi(lv_obj_t* parent) {
    tint_ = kCyan;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x020711), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    BuildGlobe();
    BuildConfig();
    BuildInfo();
    lv_label_set_text(label_, orb_studio::kStarting.text);
    lv_timer_create(OnTimer, kTickMs, this);
}

void OrbUi::BuildGlobe() {
    background_buf_ = (uint16_t*)heap_caps_malloc(kScreen * kScreen * 2, MALLOC_CAP_SPIRAM);
    base_ = (uint8_t*)heap_caps_malloc(kBox * kBox * 3, MALLOC_CAP_SPIRAM);
    light_ = (uint8_t*)heap_caps_malloc(kBox * kBox, MALLOC_CAP_SPIRAM);
    globe_buf_ = (uint16_t*)heap_caps_malloc(kBox * kBox * 2, MALLOC_CAP_SPIRAM);
    if (!background_buf_ || !base_ || !light_ || !globe_buf_) {
        ESP_LOGE(TAG, "Sem PSRAM para o orb");
        return;
    }

    for (int y = 0; y < kScreen; y++) {
        for (int x = 0; x < kScreen; x++) {
            Rgb c = StaticPixel(x, y);
            background_buf_[y * kScreen + x] = Pack565((int)c.r, (int)c.g, (int)c.b);
            int bx = x - kBoxX, by = y - kBoxY;
            if (bx >= 0 && bx < kBox && by >= 0 && by < kBox) {
                uint8_t* p = &base_[(by * kBox + bx) * 3];
                p[0] = (uint8_t)std::min(c.r, 255.0f);
                p[1] = (uint8_t)std::min(c.g, 255.0f);
                p[2] = (uint8_t)std::min(c.b, 255.0f);
            }
        }
    }

    background_ = lv_canvas_create(root_);
    lv_canvas_set_buffer(background_, background_buf_, kScreen, kScreen, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(background_, 0, 0);

    globe_ = lv_canvas_create(root_);
    lv_canvas_set_buffer(globe_, globe_buf_, kBox, kBox, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(globe_, kBoxX, kBoxY);
    RenderGlobe();

    // Microfone aberto: anel vermelho fino na borda, controlado so pelo firmware
    mic_ring_ = lv_arc_create(root_);
    lv_obj_remove_style_all(mic_ring_);
    lv_obj_set_size(mic_ring_, 236, 236);
    lv_obj_center(mic_ring_);
    lv_obj_remove_flag(mic_ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(mic_ring_, 0, 360);
    lv_obj_set_style_arc_width(mic_ring_, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(mic_ring_, kRed, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(mic_ring_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(mic_ring_, LV_OBJ_FLAG_HIDDEN);

    error_dot_ = MakeCircle(root_, 10, kRed);
    lv_obj_align(error_dot_, LV_ALIGN_TOP_MID, -46, 11);
    lv_obj_add_flag(error_dot_, LV_OBJ_FLAG_HIDDEN);

    battery_label_ = MakeLabel(root_, kLabel, 0);
    lv_obj_set_style_text_opa(battery_label_, LV_OPA_80, 0);
    lv_obj_align(battery_label_, LV_ALIGN_TOP_MID, 0, 6);

    label_ = MakeLabel(root_, kLabel, 3);
    lv_obj_set_style_text_opa(label_, LV_OPA_80, 0);
    lv_obj_align(label_, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void OrbUi::BuildConfig() {
    config_ = lv_obj_create(root_);
    lv_obj_remove_style_all(config_);
    lv_obj_set_size(config_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(config_, lv_color_hex(0x020711), 0);
    lv_obj_set_style_bg_opa(config_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(config_, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(config_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* ring = lv_arc_create(config_);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 228, 228);
    lv_obj_center(ring);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_obj_set_style_arc_width(ring, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, kCyan, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_60, LV_PART_MAIN);

    config_title_ = MakeLabel(config_, lv_color_white(), 1);
    lv_obj_align(config_title_, LV_ALIGN_TOP_MID, 0, 30);

    qrcode_ = lv_qrcode_create(config_);
    lv_qrcode_set_size(qrcode_, 104);
    lv_qrcode_set_dark_color(qrcode_, lv_color_black());
    lv_qrcode_set_light_color(qrcode_, lv_color_white());
    lv_obj_set_style_border_color(qrcode_, lv_color_white(), 0);
    lv_obj_set_style_border_width(qrcode_, 6, 0);
    lv_obj_align(qrcode_, LV_ALIGN_CENTER, 0, -4);

    config_ssid_ = MakeLabel(config_, kLabel, 1);
    lv_obj_align(config_ssid_, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_t* hint = MakeLabel(config_, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(hint, "Abra 192.168.4.1");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
}

void OrbUi::BuildInfo() {
    info_ = lv_obj_create(root_);
    lv_obj_remove_style_all(info_);
    lv_obj_set_size(info_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(info_, lv_color_hex(0x020711), 0);
    lv_obj_set_style_bg_opa(info_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(info_, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* ring = lv_arc_create(info_);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 232, 232);
    lv_obj_center(ring);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_obj_set_style_arc_width(ring, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, kCyan, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_40, LV_PART_MAIN);

    lv_obj_t* title = MakeLabel(info_, kLabel, 3);
    lv_label_set_text(title, orb_studio::kTitle);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    info_text_ = MakeLabel(info_, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_width(info_text_, 210);
    lv_obj_set_style_text_line_space(info_text_, -1, 0);
    lv_obj_align(info_text_, LV_ALIGN_TOP_MID, 0, 40);

    // Botao CONFIGURAR: o toque e tratado pela placa (abaixo de kConfigButtonTop)
    lv_obj_t* button = lv_obj_create(info_);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 116, 28);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, kCyan, 0);
    lv_obj_set_style_bg_color(button, kCyan, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, 0);
    lv_obj_remove_flag(button, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, kConfigButtonTop + 2);
    lv_obj_t* button_label = MakeLabel(button, kLabel, 2);
    lv_label_set_text(button_label, "CONFIGURAR");
    lv_obj_center(button_label);
}

void OrbUi::ShowInfo(bool show) {
    info_open_ = show;
    if (show) {
        UpdateInfo();
        lv_obj_remove_flag(info_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(info_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OrbUi::UpdateBattery() {
    int level = 0;
    bool charging = false, discharging = false;
    if (!Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char* icon = charging      ? LV_SYMBOL_CHARGE
                       : level >= 90 ? LV_SYMBOL_BATTERY_FULL
                       : level >= 60 ? LV_SYMBOL_BATTERY_3
                       : level >= 35 ? LV_SYMBOL_BATTERY_2
                       : level >= 15 ? LV_SYMBOL_BATTERY_1
                                     : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(battery_label_, "%s %d%%", icon, level);
    lv_obj_set_style_text_color(battery_label_, level <= 20 && !charging ? kRed : kLabel, 0);
}

void OrbUi::UpdateInfo() {
    const esp_app_desc_t* app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);

    int level = 0;
    bool charging = false, discharging = false;
    Board::GetInstance().GetBatteryLevel(level, charging, discharging);

    auto& wifi = WifiStation::GetInstance();
    std::string ssid = wifi.IsConnected() ? wifi.GetSsid().substr(0, 16) : "desconectado";
    std::string ip = wifi.IsConnected() ? wifi.GetIpAddress() : "-";
    int rssi = wifi.IsConnected() ? wifi.GetRssi() : 0;

    // Servidor sem o ws:// e sem o caminho, para caber na tela
    Settings websocket("websocket", false);
    std::string server = websocket.GetString("url");
    size_t scheme = server.find("://");
    if (scheme != std::string::npos) {
        server = server.substr(scheme + 3);
    }
    server = server.substr(0, server.find('/'));

    int64_t up = esp_timer_get_time() / 1000000;
    char text[400];
    snprintf(text, sizeof(text),
             "FW %s  %s\n"
             "ESP32-S3 r%d.%d  %luMB/%uMB\n"
             "MAC %s\n"
             "WiFi %s %ddBm\n"
             "IP %s\n"
             "SRV %s\n"
             "BAT %d%%%s\n"
             "RAM %uK  PSRAM %uK\n"
             "LIGADO %d:%02d:%02d",
             app->version, app->date,
             chip.revision / 100, chip.revision % 100,
             (unsigned long)(flash / (1024 * 1024)), (unsigned)(esp_psram_get_size() / (1024 * 1024)),
             SystemInfo::GetMacAddress().c_str(),
             ssid.c_str(), rssi,
             ip.c_str(),
             server.empty() ? "-" : server.c_str(),
             level, charging ? " carregando" : "",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60));
    lv_label_set_text(info_text_, text);
}

void OrbUi::RenderGlobe() {
    if (!globe_buf_) {
        return;
    }
    memset(light_, 0, kBox * kBox);

    auto add = [this](int x, int y, float a) {
        if (x < 0 || y < 0 || x >= kBox || y >= kBox || a <= 0) {
            return;
        }
        uint8_t& v = light_[y * kBox + x];
        v = (uint8_t)std::min(255, v + (int)(a * 255.0f));
    };
    // Ponto com anti-aliasing: divide a luz entre os 4 pixels vizinhos
    auto splat = [&add](float x, float y, float a) {
        int x0 = (int)std::floor(x - 0.5f), y0 = (int)std::floor(y - 0.5f);
        float fx = x - 0.5f - x0, fy = y - 0.5f - y0;
        add(x0, y0, a * (1 - fx) * (1 - fy));
        add(x0 + 1, y0, a * fx * (1 - fy));
        add(x0, y0 + 1, a * (1 - fx) * fy);
        add(x0 + 1, y0 + 1, a * fx * fy);
    };

    const float cx = kCx - kBoxX, cy = kCy - kBoxY;

    // Meridianos girando
    for (int i = 0; i < kMeridians; i++) {
        float phi = (i / (float)kMeridians) * (float)M_PI + angle_;
        float s = std::sin(phi);
        float depth = std::fabs(s);
        float alpha = (0.12f + depth * 0.5f) * (0.8f + depth * 0.5f) * 0.55f;
        for (int j = 0; j <= kMeridianSamples; j++) {
            float theta = (j / (float)kMeridianSamples) * (float)M_PI;
            splat(cx + kR * std::sin(theta) * s, cy + kR * std::cos(theta), alpha);
        }
    }

    // Pontos luminosos na face visivel
    for (int i = 0; i < kDots; i++) {
        float theta = i * 2.399f;
        float y = 1.0f - (i / (float)(kDots - 1)) * 2.0f;
        float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float x = std::cos(theta + angle_ * 0.5f) * ring;
        float z = std::sin(theta + angle_ * 0.5f) * ring;
        if (z < -0.1f) {
            continue;
        }
        float px = cx + x * kR, py = cy + y * kR;
        float radius = z > 0.7f ? 1.6f : 1.0f;
        float alpha = 0.3f + z * 0.6f;
        for (int yy = (int)(py - radius - 1); yy <= (int)(py + radius + 1); yy++) {
            for (int xx = (int)(px - radius - 1); xx <= (int)(px + radius + 1); xx++) {
                float d = std::hypot(xx + 0.5f - px, yy + 0.5f - py);
                add(xx, yy, alpha * std::clamp(radius + 0.5f - d, 0.0f, 1.0f));
            }
        }
    }

    // Compoe base + luz na cor do estado
    float wave = std::sin(2.0f * (float)M_PI * phase_ms_ / pulse_period_ms_);
    float gain = brightness_ * (1.0f + pulse_ * wave);
    int tr = (int)(tint_.red * gain), tg = (int)(tint_.green * gain), tb = (int)(tint_.blue * gain);
    for (int k = 0; k < kBox * kBox; k++) {
        const uint8_t* b = &base_[k * 3];
        int l = light_[k];
        globe_buf_[k] = Pack565(b[0] + ((l * tr) >> 8), b[1] + ((l * tg) >> 8), b[2] + ((l * tb) >> 8));
    }
    lv_obj_invalidate(globe_);
}

void OrbUi::ShowConfig(bool show) {
    if (show) {
        auto& ap = WifiConfigurationAp::GetInstance();
        std::string ssid = ap.GetSsid();
        // Rede aberta do portal; o celular entra e o portal abre sozinho
        std::string qr = "WIFI:T:nopass;S:" + ssid + ";;";
        lv_qrcode_update(qrcode_, qr.c_str(), qr.size());
        lv_label_set_text(config_ssid_, ssid.c_str());
        bool has_wifi = !SsidManager::GetInstance().GetSsidList().empty();
        lv_label_set_text(config_title_, has_wifi ? "Configurar servidor" : "Configurar Wi-Fi");
        lv_obj_remove_flag(config_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(config_, LV_OBJ_FLAG_HIDDEN);
    }
}

void OrbUi::ApplyState(int state) {
    ShowConfig(state == kDeviceStateWifiConfiguring);
    bool busy = state == kDeviceStateConnecting || state == kDeviceStateListening ||
                state == kDeviceStateSpeaking;
    if (busy) {
        lv_obj_add_flag(error_dot_, LV_OBJ_FLAG_HIDDEN);
    }
    if (state == kDeviceStateListening) {
        lv_obj_remove_flag(mic_ring_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mic_ring_, LV_OBJ_FLAG_HIDDEN);
    }

    auto use = [this](const orb_studio::StateStyle& style, const char* text) {
        tint_ = lv_color_hex(style.color);
        speed_ = style.speed;
        brightness_ = style.brightness;
        pulse_ = style.pulse;
        pulse_period_ms_ = style.period_ms > 0 ? style.period_ms : 1000;
        lv_label_set_text(label_, text);
    };
    switch (state) {
        case kDeviceStateStarting:
            use(orb_studio::kStarting, wifi_connected_ ? orb_studio::kStartingReadyText : orb_studio::kStarting.text);
            break;
        case kDeviceStateWifiConfiguring:
            speed_ = 1.0f; pulse_ = 0.0f;
            break;
        case kDeviceStateIdle: {
            Settings websocket("websocket", false);
            use(orb_studio::kIdle, websocket.GetString("url").empty() ? orb_studio::kIdleNoServerText : orb_studio::kIdle.text);
            break;
        }
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            use(orb_studio::kConnecting, orb_studio::kConnecting.text);
            break;
        case kDeviceStateListening:
            use(orb_studio::kListening, orb_studio::kListening.text);
            break;
        case kDeviceStateSpeaking:
            use(orb_studio::kSpeaking, orb_studio::kSpeaking.text);
            break;
        case kDeviceStateFatalError:
            use(orb_studio::kError, orb_studio::kError.text);
            break;
        default:
            break;
    }
}

void OrbUi::Tick() {
    int state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateStarting) {
        bool connected = WifiStation::GetInstance().IsConnected();
        if (connected != wifi_connected_) {
            wifi_connected_ = connected;
            last_state_ = -1;
        }
    }
    if (state != last_state_) {
        ESP_LOGI(TAG, "Estado %d", state);
        ApplyState(state);
        last_state_ = state;
    }
    if (error_pending_.exchange(false)) {
        lv_obj_remove_flag(error_dot_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_, orb_studio::kOfflineText);
    }

    ticks_++;
    // A placa so existe depois do estado inicial: Board::GetInstance() antes disso
    // trava o boot (o OrbUi nasce dentro do construtor da placa)
    bool board_ready = state != kDeviceStateUnknown;
    if (board_ready && (ticks_ % 100 == 1 || battery_pending_)) {  // ~5 s
        battery_pending_ = false;
        UpdateBattery();
    } else if (!board_ready) {
        battery_pending_ = true;
    }
    if (info_open_) {
        if (ticks_ % 20 == 0) {  // ~1 s
            UpdateInfo();
        }
        return;  // globo coberto: nao desenha
    }
    if (state == kDeviceStateWifiConfiguring) {
        return;
    }
    angle_ = std::fmod(angle_ + kBaseSpeed * speed_, 2.0f * (float)M_PI);
    phase_ms_ = (phase_ms_ + kTickMs) % pulse_period_ms_;
    RenderGlobe();
}

void OrbUi::OnTimer(lv_timer_t* timer) {
    static_cast<OrbUi*>(lv_timer_get_user_data(timer))->Tick();
}
