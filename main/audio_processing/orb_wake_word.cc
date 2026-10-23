#include "orb_wake_word.h"

#include <esp_log.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <model_path.h>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1

#define TAG "OrbWakeWord"

OrbWakeWord::OrbWakeWord() {
    event_group_ = xEventGroupCreate();
}

OrbWakeWord::~OrbWakeWord() {
    if (multinet_data_ != nullptr) {
        multinet_->destroy(multinet_data_);
    }
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    vEventGroupDelete(event_group_);
}

void OrbWakeWord::Initialize(AudioCodec* codec) {
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;

    srmodel_list_t* models = esp_srmodel_init("model");
    char* mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (mn_name == nullptr) {
        ESP_LOGE(TAG, "Modelo MultiNet em ingles nao encontrado na particao model");
        return;
    }
    multinet_ = esp_mn_handle_from_name(mn_name);
    // Janela de 3 s por tentativa; depois o detector reinicia sozinho (TIMEOUT)
    multinet_data_ = multinet_->create(mn_name, 3000);
    multinet_->set_det_threshold(multinet_data_, CONFIG_ORB_WAKE_WORD_THRESHOLD / 100.0f);

    // Frases separadas por ";" no menuconfig, ex.: "hey orb;hi orb"
    esp_mn_commands_alloc(multinet_, multinet_data_);
    std::stringstream ss(CONFIG_ORB_WAKE_WORD);
    std::string phrase;
    while (std::getline(ss, phrase, ';')) {
        if (phrase.empty()) {
            continue;
        }
        phrases_.push_back(phrase);
        if (esp_mn_commands_add(phrases_.size(), phrase.c_str()) != ESP_OK) {
            ESP_LOGE(TAG, "Frase recusada pelo MultiNet: %s", phrase.c_str());
        }
    }
    esp_mn_commands_update();
    ESP_LOGI(TAG, "Modelo %s, frases \"%s\", limiar %d%%", mn_name, CONFIG_ORB_WAKE_WORD,
             CONFIG_ORB_WAKE_WORD_THRESHOLD);

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_config->wakenet_init = false;
    afe_config->aec_init = codec_->input_reference();
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);

    xTaskCreatePinnedToCore([](void* arg) {
        static_cast<OrbWakeWord*>(arg)->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "orb_wake_word", 4096 * 2, this, 3, nullptr, 1);
}

void OrbWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void OrbWakeWord::StartDetection() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void OrbWakeWord::StopDetection() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    if (multinet_data_ != nullptr) {
        multinet_->clean(multinet_data_);
    }
}

bool OrbWakeWord::IsDetectionRunning() {
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void OrbWakeWord::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    afe_iface_->feed(afe_data_, data.data());
}

size_t OrbWakeWord::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
}

void OrbWakeWord::AudioDetectionTask() {
    ESP_LOGI(TAG, "Detector pronto: fetch %d, multinet %d amostras",
             afe_iface_->get_fetch_chunksize(afe_data_), multinet_->get_samp_chunksize(multinet_data_));

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }

        esp_mn_state_t state = multinet_->detect(multinet_data_, res->data);
        if (state == ESP_MN_STATE_TIMEOUT) {
            multinet_->clean(multinet_data_);
        } else if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* results = multinet_->get_results(multinet_data_);
            if (results->num > 0 && results->command_id[0] >= 1 &&
                results->command_id[0] <= (int)phrases_.size()) {
                last_detected_wake_word_ = phrases_[results->command_id[0] - 1];
                ESP_LOGI(TAG, "Ativado: \"%s\" (%.2f)", last_detected_wake_word_.c_str(), results->prob[0]);
                StopDetection();
                if (wake_word_detected_callback_) {
                    wake_word_detected_callback_(last_detected_wake_word_);
                }
            } else {
                multinet_->clean(multinet_data_);
            }
        }
    }
}
