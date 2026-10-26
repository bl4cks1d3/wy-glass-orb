#ifndef ORB_UI_H
#define ORB_UI_H

#include <lvgl.h>
#include <atomic>
#include <cstdint>

class OrbPack;

// Wy Glass: orb holografico (globo em wireframe girando) no lugar da interface
// de chat do Xiaozhi, e a tela de configuracao (QR da rede do portal). Segue o
// estado da Application sozinho, por um timer do LVGL. As telas interativas
// (ScreenLayout) ficam por cima, no lv_layer_top().
class OrbUi {
public:
    // Chamar com o lock do LVGL tomado.
    explicit OrbUi(lv_obj_t* parent);

    // Qualquer task: a ultima conexao com o servidor falhou.
    void ShowError() { error_pending_ = true; }

    // Tela de informacoes (modelo, firmware, rede, bateria). Chamar com o lock do LVGL.
    void ShowInfo(bool show);
    bool IsInfoOpen() const { return info_open_; }

    // Toques abaixo desta linha, com as informacoes abertas, sao o botao CONFIGURAR
    static constexpr int kConfigButtonTop = 186;

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* background_ = nullptr;
    lv_obj_t* globe_ = nullptr;
    lv_obj_t* mic_ring_ = nullptr;
    lv_obj_t* error_dot_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* config_ = nullptr;
    lv_obj_t* config_title_ = nullptr;
    lv_obj_t* config_ssid_ = nullptr;
    lv_obj_t* qrcode_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* info_ = nullptr;
    lv_obj_t* info_text_ = nullptr;

    // Buffers na PSRAM: fundo da tela (RGB565), base do globo (RGB888, ja com
    // esfera, paralelos e contorno), luz das linhas do quadro atual e o globo
    // pronto para o LVGL (RGB565).
    uint16_t* background_buf_ = nullptr;
    uint8_t* base_ = nullptr;
    uint8_t* light_ = nullptr;
    uint16_t* globe_buf_ = nullptr;
    // Esfera de plasma: brilho fixo e fases por pixel, calculados uma vez
    uint8_t* plasma_base_ = nullptr;
    uint8_t* plasma_ripple_ = nullptr;
    uint8_t* plasma_swirl_ = nullptr;
    // Ameba: angulo (0-255) e distancia (x4) de cada pixel ao centro
    uint8_t* amoeba_angle_ = nullptr;
    uint16_t* amoeba_dist_ = nullptr;
    OrbPack* pack_ = nullptr;
    int pack_state_ = -1;
    int pack_frame_ = -1;
    int64_t pack_start_us_ = 0;
    int64_t decode_us_ = 0;
    int decodes_ = 0;

    float angle_ = 0.0f;
    float speed_ = 1.0f;
    float brightness_ = 1.0f;
    float pulse_ = 0.0f;
    uint32_t pulse_period_ms_ = 3000;
    uint32_t phase_ms_ = 0;
    lv_color_t tint_;
    int last_state_ = -1;
    bool wifi_connected_ = false;
    std::atomic<bool> error_pending_{false};
    std::atomic<bool> info_open_{false};
    uint32_t ticks_ = 0;
    bool battery_pending_ = true;

    void BuildGlobe();
    void BuildConfig();
    void BuildInfo();
    void UpdateBattery();
    void UpdateInfo();
    void RenderGlobe();

    // Pintores dos tipos de orb (orb_studio::kStyle): escrevem luz em light_
    void AddLight(int x, int y, float a);
    void Splat(float x, float y, float a);
    void Disc(float x, float y, float radius, float a);
    void Glow(float x, float y, float sigma, float peak);
    void Circle(float x, float y, float radius, float a);
    void PaintGlobe();
    void PaintRings();
    void PaintPlasma();
    void PaintWave();
    void PaintParticles();
    void PaintPulse();

    // Ameba: personagem colorido, pintado direto em globe_buf_ depois da base
    void PaintAmoeba();
    void BlendPixel(int x, int y, uint32_t rgb, float a);
    void Dot(float x, float y, float radius, uint32_t rgb, float a = 1.0f);
    void Ellipse(float x, float y, float rx, float ry, uint32_t rgb, int half = 0, float a = 1.0f);

    // Tipos com arte fixa: desenhada uma vez no boot em base_ (cerebro, choque, robo)
    void BakeStyle();
    void BaseBlend(int x, int y, uint32_t rgb, float a);
    void BaseDot(float x, float y, float radius, uint32_t rgb, float a = 1.0f);
    // Orb de arquivo (.orb na particao orbs): toca os quadros no lugar do pintor
    void RenderPack();
    void PaintBrain();
    void PaintShock();
    void PaintZeta();
    void ApplyState(int state);
    void ShowConfig(bool show);
    void Tick();
    static void OnTimer(lv_timer_t* timer);
};

#endif
