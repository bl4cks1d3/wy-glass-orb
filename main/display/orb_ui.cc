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

// O que cada tipo de orb tem de fixo (desenhado uma vez no boot)
constexpr auto kStyle = orb_studio::kStyle;
constexpr bool kSphereFill = kStyle == orb_studio::Style::kGlobe || kStyle == orb_studio::Style::kPlasma ||
                             kStyle == orb_studio::Style::kParticles;
constexpr bool kParallels = kStyle == orb_studio::Style::kGlobe;
constexpr bool kOutline = kStyle == orb_studio::Style::kGlobe || kStyle == orb_studio::Style::kParticles;

// Seno em 256 passos, para os efeitos calculados por pixel
float kSinLut[256];

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

    float inside = kSphereFill ? std::clamp(kR + 0.5f - d, 0.0f, 1.0f) : 0.0f;
    if (inside > 0) {
        float gx = kCx - 0.3f * kR, gy = kCy - 0.35f * kR;
        float t = std::hypot(px - gx, py - gy) / (1.3f * kR);
        Rgb sphere = Gradient(Hex(orb_studio::kSphere[0]), Hex(orb_studio::kSphere[1]),
                              Hex(orb_studio::kSphere[2]), 0.65f, t);

        // Paralelos: linhas horizontais dentro da esfera
        for (int i = -5; kParallels && i <= 5; i++) {
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
    if (!kOutline) {
        return c;
    }
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

    for (int i = 0; i < 256; i++) {
        kSinLut[i] = std::sin(i * 2.0f * (float)M_PI / 256.0f);
    }
    if (kStyle == orb_studio::Style::kAmoeba) {
        amoeba_angle_ = (uint8_t*)heap_caps_malloc(kBox * kBox, MALLOC_CAP_SPIRAM);
        amoeba_dist_ = (uint16_t*)heap_caps_malloc(kBox * kBox * 2, MALLOC_CAP_SPIRAM);
        const float cx = kCx - kBoxX, cy = kCy - kBoxY, to_lut = 256.0f / (2.0f * (float)M_PI);
        for (int y = 0; y < kBox && amoeba_dist_; y++) {
            for (int x = 0; x < kBox; x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                amoeba_angle_[y * kBox + x] = (uint8_t)((int)std::lround(std::atan2(dy, dx) * to_lut) & 255);
                amoeba_dist_[y * kBox + x] = (uint16_t)(std::hypot(dx, dy) * 4.0f);
            }
        }
    }
    if (kStyle == orb_studio::Style::kPlasma) {
        plasma_base_ = (uint8_t*)heap_caps_malloc(kBox * kBox, MALLOC_CAP_SPIRAM);
        plasma_ripple_ = (uint8_t*)heap_caps_malloc(kBox * kBox, MALLOC_CAP_SPIRAM);
        plasma_swirl_ = (uint8_t*)heap_caps_malloc(kBox * kBox, MALLOC_CAP_SPIRAM);
        const float cx = kCx - kBoxX, cy = kCy - kBoxY, to_lut = 256.0f / (2.0f * (float)M_PI);
        for (int y = 0; y < kBox && plasma_swirl_; y++) {
            for (int x = 0; x < kBox; x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = std::hypot(dx, dy);
                float r = d / kR, inside = std::clamp(kR + 0.5f - d, 0.0f, 1.0f);
                float base = inside * (0.18f + 0.82f * std::pow(std::max(0.0f, 1.0f - r), 1.8f));
                int k = y * kBox + x;
                plasma_base_[k] = (uint8_t)(base * 255.0f);
                plasma_ripple_[k] = (uint8_t)((int)(d * 0.25f * to_lut) & 255);
                plasma_swirl_[k] = (uint8_t)((int)((3.0f * std::atan2(dy, dx) + d * 0.07f) * to_lut) & 255);
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

    switch (kStyle) {
        case orb_studio::Style::kGlobe: PaintGlobe(); break;
        case orb_studio::Style::kRings: PaintRings(); break;
        case orb_studio::Style::kPlasma: PaintPlasma(); break;
        case orb_studio::Style::kWave: PaintWave(); break;
        case orb_studio::Style::kParticles: PaintParticles(); break;
        case orb_studio::Style::kPulse: PaintPulse(); break;
        case orb_studio::Style::kAmoeba: break;  // pintada depois, em cores, sobre a base
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
    if (kStyle == orb_studio::Style::kAmoeba) {
        PaintAmoeba();
    }
    lv_obj_invalidate(globe_);
}

// ------------------------------------------------------------------ pintores
// Coordenadas dentro do quadrado do globo (kBox x kBox); luz de 0 a 1 por pixel.

void OrbUi::AddLight(int x, int y, float a) {
    if (x < 0 || y < 0 || x >= kBox || y >= kBox || a <= 0) {
        return;
    }
    uint8_t& v = light_[y * kBox + x];
    v = (uint8_t)std::min(255, v + (int)(a * 255.0f));
}

// Ponto com anti-aliasing: divide a luz entre os 4 pixels vizinhos
void OrbUi::Splat(float x, float y, float a) {
    int x0 = (int)std::floor(x - 0.5f), y0 = (int)std::floor(y - 0.5f);
    float fx = x - 0.5f - x0, fy = y - 0.5f - y0;
    AddLight(x0, y0, a * (1 - fx) * (1 - fy));
    AddLight(x0 + 1, y0, a * fx * (1 - fy));
    AddLight(x0, y0 + 1, a * (1 - fx) * fy);
    AddLight(x0 + 1, y0 + 1, a * fx * fy);
}

void OrbUi::Disc(float px, float py, float radius, float a) {
    for (int yy = (int)(py - radius - 1); yy <= (int)(py + radius + 1); yy++) {
        for (int xx = (int)(px - radius - 1); xx <= (int)(px + radius + 1); xx++) {
            float d = std::hypot(xx + 0.5f - px, yy + 0.5f - py);
            AddLight(xx, yy, a * std::clamp(radius + 0.5f - d, 0.0f, 1.0f));
        }
    }
}

// Brilho redondo e suave (gaussiano)
void OrbUi::Glow(float px, float py, float sigma, float peak) {
    int r = (int)(sigma * 3.0f);
    float k = 1.0f / (2.0f * sigma * sigma);
    for (int yy = (int)py - r; yy <= (int)py + r; yy++) {
        for (int xx = (int)px - r; xx <= (int)px + r; xx++) {
            float dx = xx + 0.5f - px, dy = yy + 0.5f - py;
            AddLight(xx, yy, peak * std::exp(-(dx * dx + dy * dy) * k));
        }
    }
}

void OrbUi::Circle(float px, float py, float radius, float a) {
    int n = std::max(24, (int)(2.0f * (float)M_PI * radius));
    for (int j = 0; j < n; j++) {
        float t = j * 2.0f * (float)M_PI / n;
        Splat(px + radius * std::cos(t), py + radius * std::sin(t), a);
    }
}

// Globo holografico: meridianos girando e pontos na face visivel
void OrbUi::PaintGlobe() {
    const float cx = kCx - kBoxX, cy = kCy - kBoxY;
    for (int i = 0; i < kMeridians; i++) {
        float phi = (i / (float)kMeridians) * (float)M_PI + angle_;
        float s = std::sin(phi);
        float depth = std::fabs(s);
        float alpha = (0.12f + depth * 0.5f) * (0.8f + depth * 0.5f) * 0.55f;
        for (int j = 0; j <= kMeridianSamples; j++) {
            float theta = (j / (float)kMeridianSamples) * (float)M_PI;
            Splat(cx + kR * std::sin(theta) * s, cy + kR * std::cos(theta), alpha);
        }
    }
    for (int i = 0; i < kDots; i++) {
        float theta = i * 2.399f;
        float y = 1.0f - (i / (float)std::max(1, kDots - 1)) * 2.0f;
        float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float x = std::cos(theta + angle_ * 0.5f) * ring;
        float z = std::sin(theta + angle_ * 0.5f) * ring;
        if (z < -0.1f) {
            continue;
        }
        Disc(cx + x * kR, cy + y * kR, z > 0.7f ? 1.6f : 1.0f, 0.3f + z * 0.6f);
    }
}

// Aneis orbitais: tres orbitas inclinadas com um cometa cada, em volta de um nucleo
void OrbUi::PaintRings() {
    const float cx = kCx - kBoxX, cy = kCy - kBoxY;
    static const float kA[3] = {0.98f, 0.80f, 0.62f};
    static const float kRatio[3] = {0.36f, 0.55f, 0.28f};
    static const float kRot[3] = {0.35f, -0.65f, 1.30f};
    static const float kSpin[3] = {2.4f, -3.6f, 5.0f};
    Glow(cx, cy, 9.0f, 0.95f);
    for (int o = 0; o < 3; o++) {
        float a = kR * kA[o], b = a * kRatio[o];
        float cr = std::cos(kRot[o]), sr = std::sin(kRot[o]);
        auto point = [&](float t, float& x, float& y) {
            float ex = a * std::cos(t), ey = b * std::sin(t);
            x = cx + ex * cr - ey * sr;
            y = cy + ex * sr + ey * cr;
        };
        float x, y;
        int n = (int)(2.0f * (float)M_PI * a);
        for (int j = 0; j < n; j++) {
            point(j * 2.0f * (float)M_PI / n, x, y);
            Splat(x, y, 0.10f);
        }
        float head = angle_ * kSpin[o] + o * 2.1f, dir = kSpin[o] > 0 ? 1.0f : -1.0f;
        for (int k = 0; k < 90; k++) {
            float w = 1.0f - k / 90.0f;
            point(head - dir * k * 0.02f, x, y);
            Splat(x, y, 0.85f * w * w);
        }
        point(head, x, y);
        Disc(x, y, 1.8f, 0.9f);
    }
}

// Esfera de plasma: luz suave com ondas e redemoinho, por pixel (tabelas do boot)
void OrbUi::PaintPlasma() {
    if (!plasma_swirl_) {
        return;
    }
    const float to_lut = 256.0f / (2.0f * (float)M_PI);
    uint8_t ripple_phase = (uint8_t)((int)(angle_ * 4.0f * to_lut) & 255);
    uint8_t swirl_phase = (uint8_t)((int)(angle_ * 2.2f * to_lut) & 255);
    for (int k = 0; k < kBox * kBox; k++) {
        int base = plasma_base_[k];
        if (base == 0) {
            continue;
        }
        float ripple = kSinLut[(uint8_t)(plasma_ripple_[k] - ripple_phase)];
        float swirl = kSinLut[(uint8_t)(plasma_swirl_[k] + swirl_phase)];
        int l = (int)(base * (0.75f + 0.15f * ripple + 0.12f * swirl));
        light_[k] = (uint8_t)std::clamp(l, 0, 255);
    }
}

// Onda de voz: tres ondas circulares entrelacadas
void OrbUi::PaintWave() {
    const float cx = kCx - kBoxX, cy = kCy - kBoxY, r0 = kR * 0.6f;
    Glow(cx, cy, 14.0f, 0.35f);
    for (int s = 0; s < 3; s++) {
        float alpha = 0.6f - s * 0.15f;
        for (int j = 0; j < 540; j++) {
            float th = j * 2.0f * (float)M_PI / 540.0f;
            float rr = r0 + 9.0f * std::sin(3.0f * th + angle_ * 2.2f + s * 2.1f) * std::sin(angle_ * 0.9f + s) +
                       5.0f * std::sin(5.0f * th - angle_ * 3.3f + s * 1.3f);
            Splat(cx + rr * std::cos(th), cy + rr * std::sin(th), alpha);
        }
    }
}

// Nuvem de particulas: esfera de pontos 3D inclinada, girando
void OrbUi::PaintParticles() {
    const float cx = kCx - kBoxX, cy = kCy - kBoxY, radius = kR * 0.92f;
    int n = kDots > 0 ? std::min(kDots * 3, 300) : 150;
    float ct = std::cos(0.45f), st = std::sin(0.45f);
    for (int i = 0; i < n; i++) {
        float y = 1.0f - (i / (float)std::max(1, n - 1)) * 2.0f;
        float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float th = i * 2.399f + angle_;
        float x = std::cos(th) * ring, z = std::sin(th) * ring;
        float y2 = y * ct - z * st, z2 = y * st + z * ct;
        float depth = (z2 + 1.0f) * 0.5f;
        Disc(cx + x * radius, cy + y2 * radius, 0.6f + 1.3f * depth, 0.12f + 0.8f * depth);
    }
}

// Pulso minimalista: nucleo que respira e dois aneis que se expandem
void OrbUi::PaintPulse() {
    const float cx = kCx - kBoxX, cy = kCy - kBoxY;
    float frac = phase_ms_ / (float)pulse_period_ms_;
    float core = 14.0f + 4.0f * std::sin(2.0f * (float)M_PI * frac);
    Glow(cx, cy, core * 0.6f, 0.6f);
    Disc(cx, cy, core, 0.8f);
    for (int r = 0; r < 2; r++) {
        float f = std::fmod(frac + r * 0.5f, 1.0f);
        float w = 1.0f - f;
        Circle(cx, cy, core + (kR - core) * f, 0.55f * w * std::sqrt(w));
    }
}

// ------------------------------------------------------------------ ameba
// Personagem: corpo verde-agua ondulando, contorno escuro, bracos e pes, manchas, olhos e boca.
// O estado muda a expressao; a velocidade do estado faz ondular e o pulso faz respirar.

namespace {

constexpr uint32_t kAmoebaLight = 0xA8F7E6;
constexpr uint32_t kAmoebaShadow = 0x4FD3BA;
constexpr uint32_t kAmoebaLine = 0x1C2B2B;
constexpr uint32_t kAmoebaTeeth = 0xF4FBF9;

struct Spot {
    float x, y, r;  // em fracoes do raio do corpo
    uint32_t color;
};

constexpr Spot kSpots[] = {
    {-0.10f, -0.62f, 0.12f, 0xF2876B}, {0.26f, -0.74f, 0.07f, 0xF48FB1}, {-0.58f, -0.24f, 0.10f, 0xF5A651},
    {0.60f, -0.22f, 0.10f, 0xF5A651},  {-0.54f, 0.30f, 0.10f, 0xF5A651}, {0.58f, 0.30f, 0.06f, 0xF48FB1},
    {-0.30f, 0.58f, 0.06f, 0xF48FB1},  {0.38f, 0.56f, 0.06f, 0xF48FB1},  {0.00f, 0.50f, 0.09f, 0x35B6C9},
    {-0.66f, 0.02f, 0.04f, 0x3BBFA5},  {0.42f, -0.48f, 0.035f, 0x3BBFA5},
};

// Saliencia suave no contorno (bracos e pes), por angulo
float Bump(float th, float center, float width, float height) {
    float d = std::remainder(th - center, 2.0f * (float)M_PI);
    return height * std::exp(-(d * d) / (width * width));
}

Rgb Unpack565(uint16_t v) {
    return {(float)((v >> 11) << 3), (float)(((v >> 5) & 0x3F) << 2), (float)((v & 0x1F) << 3)};
}

}  // namespace

void OrbUi::BlendPixel(int x, int y, uint32_t rgb, float a) {
    if (x < 0 || y < 0 || x >= kBox || y >= kBox || a <= 0.0f) {
        return;
    }
    uint16_t& px = globe_buf_[y * kBox + x];
    Rgb c = Lerp(Unpack565(px), Hex(rgb), std::min(a, 1.0f));
    px = Pack565((int)c.r, (int)c.g, (int)c.b);
}

void OrbUi::Dot(float px, float py, float radius, uint32_t rgb, float a) {
    for (int yy = (int)(py - radius - 1); yy <= (int)(py + radius + 1); yy++) {
        for (int xx = (int)(px - radius - 1); xx <= (int)(px + radius + 1); xx++) {
            float d = std::hypot(xx + 0.5f - px, yy + 0.5f - py);
            BlendPixel(xx, yy, rgb, a * std::clamp(radius + 0.5f - d, 0.0f, 1.0f));
        }
    }
}

// Elipse cheia; half = 1 so a metade de baixo (sorriso), -1 so a de cima
void OrbUi::Ellipse(float px, float py, float rx, float ry, uint32_t rgb, int half) {
    for (int yy = (int)(py - ry - 1); yy <= (int)(py + ry + 1); yy++) {
        float fy = yy + 0.5f - py;
        if ((half > 0 && fy < 0) || (half < 0 && fy > 0)) {
            continue;
        }
        for (int xx = (int)(px - rx - 1); xx <= (int)(px + rx + 1); xx++) {
            float fx = xx + 0.5f - px;
            float e = std::sqrt((fx * fx) / (rx * rx) + (fy * fy) / (ry * ry));
            BlendPixel(xx, yy, rgb, std::clamp((1.0f - e) * std::min(rx, ry) + 0.5f, 0.0f, 1.0f));
        }
    }
}

void OrbUi::PaintAmoeba() {
    if (!amoeba_dist_) {
        return;
    }
    const float cx = kCx - kBoxX, cy = kCy - kBoxY;
    const float tau = 2.0f * (float)M_PI;
    float frac = phase_ms_ / (float)pulse_period_ms_;
    float breath = std::sin(tau * frac);
    float body = kR * 0.74f * (1.0f + 0.06f * pulse_ * breath);

    // Contorno de ameba: ondula com angle_; bracos dos lados e pes embaixo (y cresce para baixo)
    float rt[256];
    for (int i = 0; i < 256; i++) {
        float th = i * tau / 256.0f;
        float r = body * (1.0f + 0.05f * std::sin(7.0f * th + angle_ * 1.5f) + 0.03f * std::sin(3.0f * th - angle_ * 2.0f));
        r += Bump(th, 0.30f, 0.16f, 13.0f) + Bump(th, (float)M_PI - 0.30f, 0.16f, 13.0f);
        r += Bump(th, 1.30f, 0.13f, 11.0f) + Bump(th, (float)M_PI - 1.30f, 0.13f, 11.0f);
        rt[i] = r;
    }

    // Corpo: preenchimento com luz de cima/esquerda e contorno escuro de ~3 px
    const Rgb light = Hex(kAmoebaLight), shadow = Hex(kAmoebaShadow), line = Hex(kAmoebaLine);
    for (int y = 0; y < kBox; y++) {
        for (int x = 0; x < kBox; x++) {
            int k = y * kBox + x;
            float d = amoeba_dist_[k] * 0.25f, r = rt[amoeba_angle_[k]];
            if (d > r + 1.0f) {
                continue;
            }
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float shade = std::clamp(0.55f - (dx * 0.45f + dy * 0.6f) / (2.0f * body), 0.0f, 1.0f);
            Rgb c = Lerp(shadow, light, shade);
            float edge = r - d;
            c = Lerp(c, line, std::clamp(3.2f - edge, 0.0f, 1.0f));
            float cover = std::clamp(r + 1.0f - d, 0.0f, 1.0f);
            Rgb under = Unpack565(globe_buf_[k]);
            c = Lerp(under, c, cover);
            globe_buf_[k] = Pack565((int)c.r, (int)c.g, (int)c.b);
        }
    }

    // Manchas, com contorno fino
    for (const Spot& s : kSpots) {
        float sx = cx + s.x * body, sy = cy + s.y * body, sr = s.r * body;
        Dot(sx, sy, sr + 1.2f, kAmoebaLine);
        Dot(sx, sy, sr, s.color);
    }

    // Rosto: olhos e boca conforme o estado
    float eye_y = cy - 0.14f * body, eye_dx = 0.30f * body, mouth_y = cy + 0.12f * body;
    int state = last_state_;
    auto happy_eye = [&](float ex) {  // olho fechado e feliz: curva para baixo
        for (int i = -8; i <= 8; i++) {
            float t = i / 8.0f;
            Dot(ex + t * 0.12f * body, eye_y + 0.06f * body * t * t, 1.6f, kAmoebaLine);
        }
    };
    auto open_eye = [&](float ex, float look) {
        Ellipse(ex + look, eye_y, 0.065f * body, 0.10f * body, kAmoebaLine);
        Dot(ex + look - 0.02f * body, eye_y - 0.035f * body, 0.025f * body, 0xFFFFFF);
    };
    auto x_eye = [&](float ex) {
        for (int i = -6; i <= 6; i++) {
            float t = i / 6.0f * 0.07f * body;
            Dot(ex + t, eye_y + t, 1.5f, kAmoebaLine);
            Dot(ex + t, eye_y - t, 1.5f, kAmoebaLine);
        }
    };
    auto smile = [&](float open) {  // sorriso aberto com dentes em cima
        float rx = 0.27f * body, ry = (0.06f + 0.16f * open) * body;
        Ellipse(cx, mouth_y, rx, ry, kAmoebaLine, 1);
        Ellipse(cx, mouth_y, rx - 2.5f, std::max(1.0f, ry - 2.5f), 0x7A2E3A, 1);
        Ellipse(cx, mouth_y, rx - 2.5f, std::max(1.0f, (ry - 2.5f) * 0.38f), kAmoebaTeeth, 1);
    };

    switch (state) {
        case kDeviceStateListening:
            open_eye(cx - eye_dx, 0.0f);
            open_eye(cx + eye_dx, 0.0f);
            Ellipse(cx, mouth_y + 0.05f * body, 0.07f * body, 0.08f * body, kAmoebaLine);
            Ellipse(cx, mouth_y + 0.05f * body, 0.045f * body, 0.055f * body, 0x7A2E3A);
            break;
        case kDeviceStateSpeaking: {
            happy_eye(cx - eye_dx);
            happy_eye(cx + eye_dx);
            float talk = 0.5f + 0.5f * std::sin(tau * frac * 2.0f);
            smile(talk);
            break;
        }
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading: {
            float look = 0.04f * body * std::sin(angle_ * 3.0f);
            open_eye(cx - eye_dx, look);
            open_eye(cx + eye_dx, look);
            for (int i = -6; i <= 6; i++) {
                Dot(cx + i * 0.02f * body, mouth_y + 0.04f * body, 1.5f, kAmoebaLine);
            }
            break;
        }
        case kDeviceStateFatalError:
            x_eye(cx - eye_dx);
            x_eye(cx + eye_dx);
            for (int i = -8; i <= 8; i++) {
                float t = i / 8.0f;
                Dot(cx + t * 0.16f * body, mouth_y + 0.10f * body - 0.06f * body * (1 - t * t), 1.6f, kAmoebaLine);
            }
            break;
        default:
            happy_eye(cx - eye_dx);
            happy_eye(cx + eye_dx);
            smile(1.0f);
            break;
    }
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
