#ifndef ORB_WAKE_WORD_H
#define ORB_WAKE_WORD_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_afe_sr_models.h>
#include <esp_mn_iface.h>

#include <string>
#include <vector>
#include <functional>

#include "audio_codec.h"
#include "wake_word.h"

// Wy Glass: palavra de ativacao propria ("hey orb") com o MultiNet7 em ingles da esp-sr.
// A Espressif nao tem modelo WakeNet pronto para "orb"; o MultiNet reconhece frases
// escritas em texto (g2p interno). O AFE so faz o pre-processamento (sem WakeNet).
// Diferente do AfeWakeWord, nao guarda o audio da ativacao: o servidor do Wy Glass nao
// usa, e os ~66 KB em pedacos pequenos esgotavam a SRAM interna.
class OrbWakeWord : public WakeWord {
public:
    OrbWakeWord();
    ~OrbWakeWord();

    void Initialize(AudioCodec* codec) override;
    void Feed(const std::vector<int16_t>& data) override;
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override;
    void StartDetection() override;
    void StopDetection() override;
    bool IsDetectionRunning() override;
    size_t GetFeedSize() override;
    void EncodeWakeWordData() override {}
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override { return false; }
    const std::string& GetLastDetectedWakeWord() const override { return last_detected_wake_word_; }

private:
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_data_ = nullptr;
    std::vector<std::string> phrases_;
    EventGroupHandle_t event_group_;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;

    void AudioDetectionTask();
};

#endif
