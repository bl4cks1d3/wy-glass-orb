#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>
#include "system_reset.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <esp_timer.h>
#include "i2c_device.h"
#include <esp_lcd_panel_vendor.h>
#include <esp_adc/adc_oneshot.h>
#include <driver/spi_common.h>
#include "power_save_timer.h"
#include "screen_layout.h"
#include "orb_ui.h"
#include "orb_studio_config.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#define TAG "Spotpear_ESP32_S3_1_28_BOX"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);


class Cst816d : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    Cst816d(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Cst816d() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        if (read_buffer_[0] == 0xFF)
        {
            read_buffer_[0] = 0x00;
        }
        tp_.num = read_buffer_[0] & 0x01;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};


// Wy Glass: bateria como o PowerManager do sp-esp32-s3-1.28-box do xiaozhi oficial: leitura
// bruta do ADC (sem calibracao) mapeada pela tabela medida por eles; GPIO41 alto = carregando.
class BatteryMonitor {
public:
    BatteryMonitor() {
        adc_oneshot_unit_init_cfg_t unit = {};
        unit.unit_id = ADC_UNIT_1;
        unit.ulp_mode = ADC_ULP_MODE_DISABLE;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc_));
        adc_oneshot_chan_cfg_t channel = {};
        channel.atten = ADC_ATTEN_DB_12;
        channel.bitwidth = ADC_BITWIDTH_12;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, BATTERY_ADC_CHANNEL, &channel));

        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << BATTERY_CHARGING_PIN;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io);
    }

    // Le o ADC no maximo a cada 2 s e devolve a media das ultimas leituras
    void Read(int& level, bool& charging) {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t now = esp_timer_get_time();
        if (count_ == 0 || now - last_read_us_ > 2000000) {
            int raw = 0;
            if (adc_oneshot_read(adc_, BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
                samples_[next_] = raw;
                next_ = (next_ + 1) % kSamples;
                count_ = count_ < kSamples ? count_ + 1 : kSamples;
            }
            last_read_us_ = now;
        }
        int sum = 0;
        for (int i = 0; i < count_; i++) {
            sum += samples_[i];
        }
        level = ToLevel(count_ ? sum / count_ : 0);
        charging = gpio_get_level(BATTERY_CHARGING_PIN) == 1 && level < 100;
    }

private:
    static constexpr int kSamples = 5;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    std::mutex mutex_;
    int samples_[kSamples] = {};
    int count_ = 0;
    int next_ = 0;
    int64_t last_read_us_ = 0;

    static int ToLevel(int raw) {
        static const int kTable[][2] = {{1980, 0}, {2081, 20}, {2163, 40}, {2250, 60}, {2340, 80}, {2480, 100}};
        if (raw <= kTable[0][0]) {
            return 0;
        }
        for (int i = 1; i < 6; i++) {
            if (raw < kTable[i][0]) {
                int span = kTable[i][0] - kTable[i - 1][0];
                return kTable[i - 1][1] + (raw - kTable[i - 1][0]) * (kTable[i][1] - kTable[i - 1][1]) / span;
            }
        }
        return 100;
    }
};


class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy,
                    {
                        .text_font = &font_puhui_16_4,
                        .icon_font = &font_awesome_16_4,
                        .emoji_font = font_emoji_64_init(),
                    }) {

        DisplayLockGuard lock(this);
        // Wy Glass: o orb substitui a interface de chat, que fica escondida
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        orb_ = new OrbUi(lv_screen_active());
    }

    // Tela de informacoes (arrastar para o lado)
    void ShowInfo(bool show) {
        DisplayLockGuard lock(this);
        orb_->ShowInfo(show);
    }
    bool IsInfoOpen() const { return orb_->IsInfoOpen(); }

    void SetResting(bool resting) { orb_->SetResting(resting); }

    virtual void SetStatus(const char* status) override {
        SpiLcdDisplay::SetStatus(status);
        if (strcmp(status, Lang::Strings::ERROR) == 0) {
            orb_->ShowError();
        }
    }

private:
    OrbUi* orb_ = nullptr;
};


class Spotpear_ESP32_S3_1_28_BOX : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Display* display_;
    CustomLcdDisplay* orb_display_ = nullptr;
    BatteryMonitor* battery_ = nullptr;
    esp_timer_handle_t touchpad_timer_;
    Cst816d* cst816d_ = nullptr;

    // Ultimo estado do touch, escrito pelo timer e lido pelo LVGL (telas interativas).
    static inline std::atomic<bool> touch_pressed_{false};
    static inline std::atomic<int> touch_x_{0};
    static inline std::atomic<int> touch_y_{0};
    PowerSaveTimer* power_save_timer_;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_3);
        rtc_gpio_set_direction(GPIO_NUM_3, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_3, 1);

        // Descanso de tela (Orb Studio): sem conversa por kRestSeconds, escurece ou apaga
        power_save_timer_ = new PowerSaveTimer(-1, orb_studio::kRestSeconds > 0 ? orb_studio::kRestSeconds : -1, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Descanso de tela");
            GetBacklight()->SetBrightness(orb_studio::kRestScreenOff ? 0 : 15);
            if (orb_display_) {
                orb_display_->SetResting(true);
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            if (orb_display_) {
                orb_display_->SetResting(false);
            }
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_3, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_3);
            esp_lcd_panel_disp_on_off(panel_, false); //关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeCodecI2c_Touch() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = TP_PIN_NUM_TP_SDA,
            .scl_io_num = TP_PIN_NUM_TP_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }


    static void touchpad_timer_callback(void* arg) {
        auto& board = (Spotpear_ESP32_S3_1_28_BOX&)Board::GetInstance();
        auto touchpad = board.GetTouchpad();
        static bool was_touched = false;
        static bool touched_on_layout = false;
        static int64_t touch_start_time = 0;
        static int touch_start_x = 0;
        static int touch_start_y = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;  // 触摸时长阈值，超过500ms视为长按

        touchpad->UpdateTouchPoint();
        auto touch_point = touchpad->GetTouchPoint();

        // Wy Glass: o CST816 as vezes le toques que nao existem. So conta como
        // toque depois de 3 leituras seguidas (30 ms) dentro da tela.
        static int pressed_reads = 0;
        bool raw_pressed = touch_point.num > 0 &&
            touch_point.x >= 0 && touch_point.x < DISPLAY_WIDTH &&
            touch_point.y >= 0 && touch_point.y < DISPLAY_HEIGHT;
        pressed_reads = raw_pressed ? pressed_reads + 1 : 0;
        if (raw_pressed && pressed_reads < 3 && !was_touched) {
            touch_point.num = 0;
        } else if (!raw_pressed) {
            touch_point.num = 0;
        }

        if (touch_point.num > 0) {
            touch_x_ = TOUCH_MIRROR_X ? DISPLAY_WIDTH - 1 - touch_point.x : touch_point.x;
            touch_y_ = TOUCH_MIRROR_Y ? DISPLAY_HEIGHT - 1 - touch_point.y : touch_point.y;
        }
        touch_pressed_ = touch_point.num > 0;

        // 检测触摸开始
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touched_on_layout = ScreenLayout::GetInstance().IsOpen();
            touch_start_time = esp_timer_get_time() / 1000; // 转换为毫秒
            touch_start_x = touch_x_;
            touch_start_y = touch_y_;
        }
        // 检测触摸释放
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;

            // 只有短触才触发; com uma tela interativa aberta, o toque e dela (LVGL)
            int dx = touch_x_ - touch_start_x;
            int dy = touch_y_ - touch_start_y;
            ESP_LOGI(TAG, "Toque em (%d, %d), dx %d dy %d, %d ms", (int)touch_x_, (int)touch_y_, dx, dy,
                     (int)touch_duration);
            if (!touched_on_layout && !ScreenLayout::GetInstance().IsOpen()) {
                auto display = board.orb_display_;
                if (board.power_save_timer_->IsInSleepMode()) {
                    board.power_save_timer_->WakeUp();  // descansando: o toque so acorda
                } else if (std::abs(dx) >= 50 && std::abs(dx) > std::abs(dy)) {
                    // Arrastar para qualquer lado alterna entre o orb e as informacoes
                    display->ShowInfo(!display->IsInfoOpen());
                } else if (touch_duration < TOUCH_THRESHOLD_MS) {
                    if (!display->IsInfoOpen()) {
                        Application::GetInstance().ToggleChatState();
                    } else if (touch_y_ >= OrbUi::kConfigButtonTop) {
                        // Botao CONFIGURAR: reinicia no portal, com Wi-Fi e servidor ja salvos
                        ESP_LOGI(TAG, "Abrindo o portal de configuracao pela tela de informacoes");
                        board.ResetWifiConfiguration();
                    } else {
                        display->ShowInfo(false);
                    }
                }
            }
        }
    }

    void InitializeCst816DTouchPad() {
        ESP_LOGI(TAG, "Init Cst816D");
        cst816d_ = new Cst816d(i2c_bus_, 0x15);

        // 创建定时器，10ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = touchpad_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 10 * 1000)); // 10ms = 10000us
    }

    static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
        data->point.x = touch_x_;
        data->point.y = touch_y_;
        data->state = touch_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }

    // Wy Glass: entrega o touch ao LVGL e liga as telas interativas (docs/screen-layout.md)
    void InitializeScreenLayouts() {
        if (cst816d_) {
            DisplayLockGuard lock(display_);
            lv_indev_t* indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, touch_read_cb);
        }
        ScreenLayout::RegisterMcpTools(display_);
    }

    // SPI初始化
    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_SPI_SCLK_PIN, DISPLAY_SPI_MOSI_PIN,
                                    DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GC9A01初始化
    void InitializeGc9a01Display() {
        ESP_LOGI(TAG, "Init GC9A01 display");
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISPLAY_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, 0, NULL);
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));

        ESP_LOGI(TAG, "Install GC9A01 panel driver");
        esp_lcd_panel_handle_t panel_handle = NULL;
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;    // Set to -1 if not use
        panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;           //LCD_RGB_ENDIAN_RGB;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

        uint8_t data_0x62[] = { 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70 };
        esp_lcd_panel_io_tx_param(io_handle, 0x62, data_0x62, sizeof(data_0x62));

        uint8_t data_0x63[] = { 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70 };
        esp_lcd_panel_io_tx_param(io_handle, 0x63, data_0x63, sizeof(data_0x63));

        orb_display_ = new CustomLcdDisplay(io_handle, panel_handle,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = orb_display_;

    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    }

public:
    Spotpear_ESP32_S3_1_28_BOX() : boot_button_(BOOT_BUTTON_GPIO) {

        gpio_set_direction(TP_PIN_NUM_TP_INT, GPIO_MODE_INPUT);
        int level = gpio_get_level(TP_PIN_NUM_TP_INT);
        if (level == 1) {
            InitializeCodecI2c_Touch();
            InitializeCst816DTouchPad();
        }
        InitializePowerSaveTimer();
        battery_ = new BatteryMonitor();
        InitializeCodecI2c();
        InitializeSpi();
        InitializeGc9a01Display();
        InitializeScreenLayouts();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }


    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        battery_->Read(level, charging);
        discharging = !charging;
        return true;
    }

    Cst816d* GetTouchpad() {
        return cst816d_;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(Spotpear_ESP32_S3_1_28_BOX);
