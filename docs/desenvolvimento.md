# Desenvolvimento

## Ambiente

- ESP-IDF **5.5** (testado com o v5.5.5), alvo `esp32s3`. O `sdkconfig` versionado já tem a placa e as opções do orb.
- Os componentes de `main/idf_component.yml` são baixados no primeiro build para `managed_components/` (fora do git). O `78/esp-wifi-connect` não é baixado: usa a cópia de `components/esp-wifi-connect` (`override_path`), com o portal em português.

```bash
idf.py build
```

```bash
idf.py -p COM9 -b 921600 flash monitor
```

No Windows, com o ESP-IDF instalado em `~/esp/esp-idf`, carregue o ambiente no PowerShell antes: `. $HOME\esp\esp-idf\export.ps1`.

## Opções do menuconfig

`idf.py menuconfig` → **Xiaozhi Assistant**:

| Opção | Padrão | Uso |
| --- | --- | --- |
| `BOARD_TYPE_ESP32_S3_1_28_BOX` | sim | Placa |
| `USE_ORB_WAKE_WORD` | sim | Palavra de ativação própria (MultiNet7) |
| `ORB_WAKE_WORD` | `hey orb;hi orb` | Frases, separadas por `;`, em inglês |
| `ORB_WAKE_WORD_THRESHOLD` | 20 | Limiar em %: menor ativa mais fácil e erra mais |
| `USE_AFE_WAKE_WORD` | não | WakeNet do XiaoZhi (não usar: ver Memória) |
| `USE_AUDIO_PROCESSOR` | sim | AFE com redução de ruído na conversa |

Também importam: `SR_MN_EN_MULTINET7_QUANT` (modelo em inglês), `PARTITION_TABLE_CUSTOM_FILENAME="partitions/v1/16m_orb.csv"` e `LV_USE_QRCODE`.

Trocar só as frases ou o limiar não exige outra partição; trocar o modelo exige conferir se cabe nos 4 MB da partição `model`.

## Versão

`PROJECT_VER` no `CMakeLists.txt` da raiz. Aparece na tela de informações e no log (`App version`). Atualize o [CHANGELOG](../CHANGELOG.md) junto.

## Monitor serial

A placa usa a USB-Serial/JTAG do próprio chip: no reset, a porta some e volta, e um leitor serial simples perde o começo do boot. Use o monitor do ESP-IDF, que reconecta sozinho:

```bash
idf.py -p COM9 monitor
```

Para gravar a saída num arquivo sem terminal interativo, rode `python -u -m esp_idf_monitor --port COM9 build/xiaozhi.elf` redirecionando a saída. Sem o `-u`, o Python segura a saída no buffer e o arquivo fica cortado.

Linhas úteis do log: `OrbUi: Estado N`, `Application: Servidor: ...`, `OrbWakeWord: Ativado: ...`, `Toque em (x, y), dx .. dy .., N ms`, `SystemInfo: free sram: ... minimal sram: ...`.

## Memória

A SRAM interna é o gargalo (veja [hardware.md](hardware.md#memória)). O que já deu problema:

- **Pedaços pequenos vão para a SRAM interna.** Com `SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, todo `malloc` menor que 4 KB fica na interna. O `AfeWakeWord` do XiaoZhi guardava 2 s de áudio em blocos de 1 KB (~66 KB) e derrubava a memória livre para ~6 KB. O `OrbWakeWord` não guarda áudio.
- **Buffers grandes de propósito na PSRAM:** use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`, como os canvases do globo.
- Confira `minimal sram` no log depois de qualquer mudança de áudio ou rede.

## Armadilhas conhecidas

- **`Board::GetInstance()` dentro da construção da placa trava o boot.** O `OrbUi` nasce dentro do construtor da placa; ele só chama a placa (bateria) depois que o estado sai de `kDeviceStateUnknown`.
- **Toque fantasma:** o CST816 às vezes lê toques que não existem. O toque só conta depois de 3 leituras seguidas (30 ms) dentro da tela, e o touch nunca apaga o Wi-Fi.
- **Escrita da NVS no portal:** o portal grava o servidor em `wifi/ota_url` (nome herdado do XiaoZhi, onde era a URL de OTA).

## Teste das telas no QEMU

`test/screen_layout_qemu/` é um app mínimo que roda as telas interativas no QEMU (painel RGB virtual, Ethernet emulada). Ele usa o servidor de teste `probes/esp32_screen_layout_qemu.py` do repositório wy-glass. O driver SPI do IDF trava no QEMU (o SPI2 não é emulado), por isso o teste usa o painel `esp_lcd_qemu_rgb`.

## CI

`.github/workflows/build.yml` compila o firmware com o ESP-IDF 5.5 em cada push e pull request para `main`, e publica os binários como artefato.
