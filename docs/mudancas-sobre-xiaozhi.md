# Mudanças sobre o XiaoZhi

Base: [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) **v1.6.8** (MIT), como a Spotpear distribui para a ESP32-S3-1.28-BOX (`xiaozhi-esp32-main-V3-OFF-OTA.zip`). O que ficou de fora da cópia original: `build/`, `managed_components/` (regenerados) e `releases/` (binários prontos).

As mudanças estão marcadas com comentários `Wy Glass:` no código.

## Interface

| Arquivo | Mudança |
| --- | --- |
| `main/display/orb_ui.{h,cc}` | Novo: globo holográfico no lugar do chat; tela de configuração com QR da rede do portal; tela de informações; bateria |
| `main/display/screen_layout.{h,cc}` | Novo: telas interativas declarativas (layout JSON → LVGL; toques → `notifications/ui_event`) |
| `main/boards/sp-esp32-s3-1.28-box/sp-esp32-s3-1.28-box.cc` | A interface do XiaoZhi fica escondida sob o `OrbUi`; touch como entrada do LVGL; filtro de toque fantasma (3 leituras); arrastar alterna as informações; botão CONFIGURAR; `self.screen.render` e `self.screen.close` |
| `main/boards/sp-esp32-s3-1.28-box/config.h` | `TOUCH_MIRROR_X/Y`; pinos da bateria (GPIO1, GPIO41) |
| `main/CMakeLists.txt` | Compila `screen_layout.cc`, `orb_ui.cc` e `orb_wake_word.cc` |
| `docs/screen-layout.md` | Esquema do layout e dos eventos |

## Rede e servidor

| Arquivo | Mudança |
| --- | --- |
| `main/application.{h,cc}` | Sem ativação nem OTA do XiaoZhi (`CheckNewVersion` → `ConfigureServer`): o servidor é o endereço salvo no portal, com a porta 8733 se faltar; protocolo sempre WebSocket |
| `main/boards/common/wifi_board.cc` | Portal `WyGlass-XXXX` sem alerta falado; sem endereço `ws://`/`wss://`, o portal fica no ar |
| `components/esp-wifi-connect/` | Cópia local do `78/esp-wifi-connect` 2.4.3 (via `override_path` em `main/idf_component.yml`): portal em português com aba "Servidor", validação do endereço e reinício ao salvar |

## Voz

| Arquivo | Mudança |
| --- | --- |
| `main/audio_processing/orb_wake_word.{h,cc}` | Novo: "hey orb" / "hi orb" com AFE + MultiNet7 em inglês, sem guardar o áudio da ativação |
| `main/Kconfig.projbuild` | `USE_ORB_WAKE_WORD`, `ORB_WAKE_WORD`, `ORB_WAKE_WORD_THRESHOLD` |
| `sdkconfig`, `sdkconfig.defaults.esp32s3` | WakeNet do XiaoZhi desligado e modelos "nǐ hǎo xiǎozhì"/"Hi ESP" removidos; MultiNet7 em inglês |

## Energia e sistema

| Arquivo | Mudança |
| --- | --- |
| `main/boards/sp-esp32-s3-1.28-box/sp-esp32-s3-1.28-box.cc` | `BatteryMonitor` (ADC + pino de carga, tabela do xiaozhi oficial); sem deep sleep; brilho reduzido (não apagado) ao economizar |
| `partitions/v1/16m_orb.csv` | Partição `model` de 4 MB; apps em `0x400000` e `0xA00000` |
| `sdkconfig`, `sdkconfig.defaults.esp32s3` | `LV_USE_QRCODE`, `LV_FONT_MONTSERRAT_48`, tabela de partições do orb |
| `CMakeLists.txt` | `PROJECT_VER` próprio (0.2.0) |
