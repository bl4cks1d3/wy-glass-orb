// Gerado pelo Orb Studio (studio/) a partir de studio/orb.json. Edite pelo Studio:
// mudancas feitas aqui a mao sao sobrescritas na proxima vez que ele aplicar.
#pragma once

#include <cstdint>

namespace orb_studio {

struct StateStyle {
    uint32_t color;      // cor das linhas e pontos do globo
    float speed;         // multiplica a rotacao base
    float brightness;    // brilho das linhas
    float pulse;         // amplitude do pulso (0 = sem pulso)
    uint32_t period_ms;  // periodo do pulso
    const char* text;    // texto embaixo do globo (sem acentos: fonte Montserrat)
};

// Tipos de orb (um pintor para cada em orb_ui.cc)
enum class Style { kGlobe, kRings, kPlasma, kWave, kParticles, kPulse, kAmoeba, kBrain, kShock, kZeta };
constexpr Style kStyle = Style::kGlobe;

constexpr const char* kTitle = "WY GLASS ORB";
constexpr const char* kApPrefix = "WyGlass";
constexpr const char* kDefaultServer = "";

// Descanso de tela: segundos sem conversa (0 = nunca); apagar ou so escurecer
constexpr int kRestSeconds = 60;
constexpr bool kRestScreenOff = false;

constexpr int kMeridians = 12;
constexpr int kDots = 50;
constexpr float kBaseSpeed = 0.012f;

constexpr uint32_t kLineColor = 0x00D2FF;
constexpr uint32_t kHaloColor = 0x00BFFF;
constexpr uint32_t kLabelColor = 0x48DFFF;
constexpr uint32_t kBackground[3] = {0x071B2C, 0x020711, 0x010208};
constexpr uint32_t kSphere[3] = {0x0A2940, 0x041522, 0x01060D};

constexpr StateStyle kStarting = {0x00D2FF, 3.0f, 1.0f, 0.25f, 1500, "CONECTANDO WI-FI"};
constexpr const char* kStartingReadyText = "WI-FI OK";
constexpr StateStyle kIdle = {0x00D2FF, 1.0f, 1.0f, 0.08f, 4000, "ONLINE"};
constexpr const char* kIdleNoServerText = "SEM SERVIDOR";
constexpr StateStyle kConnecting = {0x00D2FF, 4.0f, 1.0f, 0.2f, 1200, "CONECTANDO"};
constexpr StateStyle kListening = {0x00D2FF, 1.5f, 1.3f, 0.3f, 900, "OUVINDO"};
constexpr StateStyle kSpeaking = {0x00D2FF, 2.0f, 1.3f, 0.5f, 500, "FALANDO"};
constexpr StateStyle kError = {0xEF4444, 0.3f, 1.0f, 0.0f, 1000, "ERRO"};
constexpr const char* kOfflineText = "SERVIDOR OFFLINE";

}  // namespace orb_studio
