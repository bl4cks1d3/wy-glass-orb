# Wy Glass Orb

Firmware do **orb** do [Wy Glass](https://github.com/bl4cks1d3/wy-glass): a placa redonda Spotpear ESP32-S3-1.28-BOX vira um terminal de voz do assistente. Ela ouve pelo microfone, fala pelo alto-falante e mostra o estado da conversa num globo holográfico na tela. A inteligência (Gemini Live, ferramentas, agentes) roda no servidor do Wy Glass, no PC ou num Raspberry Pi.

Baseado no [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v1.6.8 (MIT), na versão que a Spotpear distribui para esta placa, sem nada da nuvem do XiaoZhi: sem ativação, sem OTA de terceiros e sem palavra de ativação chinesa.

**Versão atual:** 0.2.0 ([CHANGELOG](CHANGELOG.md)).

## O que ele faz

- **Globo holográfico** girando desde o boot, que muda de ritmo e brilho conforme o estado: pronto, conectando, ouvindo, falando.
- **Voz:** toque na tela ou diga **"hey orb"** (detectado no próprio chip) para conversar com o Gemini Live do Wy Glass.
- **Configuração pelo celular:** sem servidor configurado, o orb abre a rede `WyGlass-XXXX` e mostra um QR code; o portal em português pede o Wi-Fi e o endereço do servidor.
- **Bateria** no topo da tela.
- **Tela de informações** (arraste para o lado): firmware, chip, MAC, Wi-Fi, IP, servidor, bateria, memória e tempo ligado, com botão **CONFIGURAR** que reabre o portal.
- **Telas interativas** desenhadas pelo servidor (layout JSON por MCP), por cima do globo.

## Hardware

Spotpear **ESP32-S3-1.28-BOX** (ESP32-S3 N16R8: 16 MB de flash, 8 MB de PSRAM), LCD redondo GC9A01 240×240, touch CST816, codec ES8311 com microfone e alto-falante, bateria com carregador, USB-C. Pinos em [docs/hardware.md](docs/hardware.md).

## Começo rápido

Precisa do [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/) (testado com o v5.5.5).

```bash
idf.py build
```

```bash
idf.py -p COM9 flash monitor
```

Antes da primeira gravação, faça o backup da placa de fábrica ([backup/README.md](backup/README.md)). No Windows, se a placa não aparecer como porta COM, veja [docs/primeiro-uso.md](docs/primeiro-uso.md#a-placa-não-aparece-como-porta-com).

Depois de gravar, siga o [primeiro uso](docs/primeiro-uso.md): conectar pelo QR, informar o Wi-Fi e o servidor (`ws://<ip-do-servidor>:8733/orb`).

## Documentação

| Documento | Conteúdo |
| --- | --- |
| [Primeiro uso](docs/primeiro-uso.md) | Gravar, configurar Wi-Fi e servidor pelo portal, trocar depois |
| [Uso](docs/uso.md) | Telas, estados, gestos, voz, bateria, informações |
| [Servidor](docs/servidor.md) | O `orb_gateway` do Wy Glass, protocolo WebSocket, teste de conexão |
| [Hardware](docs/hardware.md) | Pinos, partições, memória |
| [Arquitetura](docs/arquitetura.md) | Como o código está organizado e o que roda onde |
| [Desenvolvimento](docs/desenvolvimento.md) | Compilar, opções do menuconfig, monitor serial, armadilhas conhecidas |
| [Mudanças sobre o XiaoZhi](docs/mudancas-sobre-xiaozhi.md) | Cada arquivo alterado em relação ao xiaozhi-esp32 1.6.8 |
| [Telas interativas](docs/screen-layout.md) | Esquema do layout JSON e dos eventos de toque |
| [Protocolo WebSocket](docs/websocket.md) / [MCP](docs/mcp-protocol.md) | Documentos do XiaoZhi (em chinês) |
| [Backup](backup/README.md) | Imagem de fábrica e como restaurar |

## Licença

MIT, como o projeto original ([LICENSE](LICENSE)). Créditos ao [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) e à Spotpear; os READMEs originais estão em [docs/xiaozhi/](docs/xiaozhi/).
