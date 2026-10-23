# Backup da flash de fábrica — ESP32-S3-1.28-BOX

Dump completo da flash (16 MB) da placa como chegou, feito em 2026-10-23 com `esptool` v5.5.0 pela porta USB-Serial/JTAG nativa.

## Arquivos

| Arquivo | No git | Conteúdo |
| --- | --- | --- |
| `fabrica-16MB-sem-nvs.bin.gz` | Sim | A imagem de fábrica com a NVS apagada (`0x9000`–`0xCFFF` em `0xFF`), compactada (~2,6 MB). Restaura o firmware de fábrica; o Wi-Fi é configurado de novo |
| `fabrica-full-16MB.bin` | **Não** | A imagem exata, com a NVS. Pode conter a senha do Wi-Fi e credenciais do XiaoZhi; fica só no seu disco (`.gitignore`). Guarde uma cópia fora do repositório |

| Imagem | SHA-256 |
| --- | --- |
| Completa (`fabrica-full-16MB.bin`) | `50cb47a6a35b21dc0e362b36198f76e43e300300c61f7970bb19b1628a3776ad` |
| Sem NVS (descompactada) | `08ee5c55ef9722a83039639e49d708dd77119f49031ebf573534d60f543efc6d` |

Fora da NVS, as duas imagens são idênticas byte a byte.

## O que veio na placa

| Item | Valor |
| --- | --- |
| Chip | ESP32-S3 (QFN56) rev v0.2, PSRAM 8 MB, flash 16 MB quad 3,3 V |
| MAC | `90:70:69:f4:6d:30` |
| Firmware | `xiaozhi` 2.4.2, ESP-IDF v5.5.4, build de Sep 10 2026 (em `ota_0`; `ota_1` vazio) |

| Partição | Offset | Tamanho |
| --- | --- | --- |
| nvs | `0x9000` | 16 KB |
| otadata | `0xD000` | 8 KB |
| phy_init | `0xF000` | 4 KB |
| ota_0 | `0x20000` | 4032 KB |
| ota_1 | `0x410000` | 4032 KB |
| assets | `0x800000` | 8 MB |

Uma segunda leitura completa só diferiu em dois setores da NVS (`0xA000`–`0xBFFF`), que o firmware regrava a cada boot. Bootloader, tabela de partições, apps e assets saíram idênticos nas duas leituras.

## Fazer o backup de outra placa

```bash
python -m esptool --chip esp32s3 --port COM9 --baud 921600 read-flash 0 0x1000000 backup/fabrica-full-16MB.bin
```

## Restaurar

Troque `COM9` pela porta da placa. No Windows, a porta precisa estar com o driver "Dispositivo Serial USB" ([primeiro-uso.md](../docs/primeiro-uso.md#a-placa-não-aparece-como-porta-com)).

Com a imagem limpa, do git:

```bash
python -c "import gzip,shutil;shutil.copyfileobj(gzip.open('backup/fabrica-16MB-sem-nvs.bin.gz'),open('backup/fabrica-16MB-sem-nvs.bin','wb'))"
```

```bash
python -m esptool --chip esp32s3 --port COM9 --baud 921600 write-flash 0x0 backup/fabrica-16MB-sem-nvs.bin
```

Com a imagem completa, se você a tiver:

```bash
python -m esptool --chip esp32s3 --port COM9 --baud 921600 write-flash 0x0 backup/fabrica-full-16MB.bin
```

A gravação escreve os 16 MB inteiros, incluindo a tabela de partições de fábrica; o firmware do orb é substituído por completo.
