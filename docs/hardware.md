# Hardware

Placa Spotpear **ESP32-S3-1.28-BOX** ([wiki da Spotpear](https://spotpear.com/wiki/ESP32-S3-N16R8-AI-DeepSeek-XiaoZhi-XiaGe-Qwen-DouBao-1.28-inch-Round-LCD-BOX-TouchScreen.html)).

| Item | Valor |
| --- | --- |
| Chip | ESP32-S3 (QFN56) rev v0.2, dual core 240 MHz, Wi-Fi e BLE 5 |
| Memória | 16 MB de flash quad, 8 MB de PSRAM embutida |
| Tela | LCD redondo GC9A01, 240×240, SPI a 40 MHz, backlight ativo em LOW |
| Touch | CST816 (I2C), lido por polling a cada 10 ms |
| Áudio | Codec ES8311 (I2S a 24 kHz), microfone e alto-falante com amplificador |
| Energia | Bateria com carregador; ADC de tensão e pino de carga |
| USB | USB-C, interface USB-Serial/JTAG nativa do ESP32-S3 (VID `303A`, PID `1001`) |

## Pinos

Definidos em `main/boards/sp-esp32-s3-1.28-box/config.h`.

| Função | GPIO |
| --- | --- |
| LCD SCLK / MOSI / CS / DC / RST | 4 / 2 / 5 / 47 / 38 |
| Backlight | 42 |
| Touch SDA / SCL / RST / INT | 11 / 7 / 6 / 12 |
| ES8311 I2S MCLK / WS / BCLK / DIN / DOUT | 16 / 45 / 9 / 10 / 8 |
| ES8311 I2C SDA / SCL | 15 / 14 |
| Amplificador do alto-falante | 46 |
| Bateria (ADC1 canal 0) | 1 |
| Carregando (alto = carregando) | 41 |
| Manter ligado (power hold, RTC) | 3 |
| Botão BOOT | 0 |
| LED | 48 |

Os pinos de bateria e carga vêm do `sp-esp32-s3-1.28-box` do xiaozhi-esp32 oficial; a versão da Spotpear não lia a bateria.

## Bateria

Leitura bruta do ADC (12 bits, atenuação de 12 dB, sem calibração), média das últimas 5 leituras (uma a cada 2 s), convertida pela tabela do xiaozhi oficial:

| ADC | 1980 | 2081 | 2163 | 2250 | 2340 | 2480 |
| --- | --- | --- | --- | --- | --- | --- |
| % | 0 | 20 | 40 | 60 | 80 | 100 |

Entre os pontos, interpolação linear. Se a porcentagem não bater com a sua bateria, ajuste a tabela em `BatteryMonitor::ToLevel` (`sp-esp32-s3-1.28-box.cc`).

## Partições

`partitions/v1/16m_orb.csv`:

| Nome | Offset | Tamanho | Conteúdo |
| --- | --- | --- | --- |
| nvs | `0x9000` | 16 KB | Wi-Fi, servidor e ajustes |
| otadata | `0xD000` | 8 KB | Qual app inicia |
| phy_init | `0xF000` | 4 KB | Calibração do rádio |
| model | `0x10000` | 4 MB (`0x3F0000`) | Modelos da esp-sr (MultiNet7 em inglês, ~2,7 MB) |
| ota_0 | `0x400000` | 6 MB | App (~4,5 MB) |
| ota_1 | `0xA00000` | 6 MB | Livre (sem OTA por enquanto) |

A NVS fica no mesmo lugar que no firmware de fábrica e nas versões anteriores, então trocar de firmware não apaga o Wi-Fi nem o servidor salvos.

## Memória

A SRAM interna é o recurso apertado: Wi-Fi, áudio e LVGL disputam ~170 KB. Em "pronto", com o detector de "hey orb" ligado, sobram cerca de **70 KB**; durante o carregamento do modelo no boot o mínimo chega perto de 8 KB por um instante. O globo, os buffers de tela e os modelos ficam na PSRAM. Detalhes e armadilhas em [desenvolvimento.md](desenvolvimento.md#memória).
