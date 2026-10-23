# Arquitetura

O código é o do xiaozhi-esp32 1.6.8, com a interface, a configuração e a ativação trocadas. A lista de arquivos alterados está em [mudancas-sobre-xiaozhi.md](mudancas-sobre-xiaozhi.md).

## Peças

| Peça | Arquivo | Faz |
| --- | --- | --- |
| Aplicação | `main/application.{h,cc}` | Máquina de estados, áudio (Opus, AFE), protocolo, palavra de ativação |
| Placa | `main/boards/sp-esp32-s3-1.28-box/` | Tela, touch e gestos, bateria, codec, botão, energia |
| Rede | `main/boards/common/wifi_board.cc` | Conecta no Wi-Fi ou abre o portal |
| Portal | `components/esp-wifi-connect/` | Ponto de acesso `WyGlass-XXXX`, servidor web e DNS do portal |
| Interface | `main/display/orb_ui.{h,cc}` | Globo, configuração (QR), informações, bateria |
| Telas do servidor | `main/display/screen_layout.{h,cc}` | Layout JSON → LVGL, toques → MCP |
| Palavra de ativação | `main/audio_processing/orb_wake_word.{h,cc}` | "hey orb" com AFE + MultiNet7 |
| Protocolo | `main/protocols/websocket_protocol.cc` | WebSocket do XiaoZhi v1 |
| Ferramentas MCP | `main/mcp_server.cc` | `self.*` que o servidor chama no orb |

## Boot

```
app_main → Application::Start
  Board (tela, touch, bateria, codec)        OrbUi nasce aqui; o timer dele desenha o globo
  estado starting
  WifiBoard::StartNetwork
    sem rede salva ou servidor invalido ──► portal (estado configuring, fica ate reiniciar)
    conecta no Wi-Fi (60 s; se falhar ──► portal)
  ConfigureServer: wifi/ota_url → websocket/url (sem ativacao, sem OTA)
  WebsocketProtocol, AFE da conversa, OrbWakeWord
  estado idle → loop de eventos
```

## Estados

`kDeviceStateStarting` → `Idle` ⇄ `Connecting` → `Listening` ⇄ `Speaking`; `WifiConfiguring` no portal; `FatalError`.

O `OrbUi` não recebe eventos: um `lv_timer` de 50 ms lê `Application::GetDeviceState()` e redesenha. Assim a interface não precisa de ganchos no código do XiaoZhi. A única ligação direta é a falha de conexão: `CustomLcdDisplay::SetStatus` avisa o `OrbUi` quando o status é o erro do XiaoZhi.

## Tarefas e núcleos

| Tarefa | Núcleo | O que faz |
| --- | --- | --- |
| LVGL (esp_lvgl_port) | — | Desenho; `OrbUi::Tick` roda aqui |
| `audio_loop` | 1 | Lê o microfone e alimenta AFE ou palavra de ativação; toca a fila de saída |
| `orb_wake_word` | 1 | AFE → MultiNet7 enquanto a detecção está ligada |
| Loop principal | — | `Schedule()` e eventos de envio de áudio |
| `touchpad_timer` (esp_timer, 10 ms) | — | Lê o CST816, filtra toque fantasma, decide toque ou arrasto |

## Globo

Desenhado pixel a pixel na PSRAM, sem a API de desenho do LVGL:

1. No boot, `StaticPixel` calcula uma vez o fundo da tela e a base do globo (gradientes, paralelos, contorno) em RGB565 e RGB888.
2. A cada quadro (~20 fps), `RenderGlobe` zera um buffer de luz, espalha os 12 meridianos e os 50 pontos com anti-aliasing bilinear, e compõe base + luz × cor do estado num canvas de 192×192.
3. Só o quadrado do globo é invalidado; o fundo é um canvas fixo de 240×240.

Ritmo, brilho e pulso vêm do estado (`OrbUi::ApplyState`).

## Palavra de ativação

A Espressif não tem modelo WakeNet pronto para "orb". O `OrbWakeWord` usa o **MultiNet7 em inglês** (`mn7_en`), que reconhece frases escritas em texto (a esp-sr converte em fonemas com `flite_g2p`). Um AFE sem WakeNet faz o pré-processamento (VAD) e entrega blocos de 512 amostras ao MultiNet, com janelas de 3 s. A detecção roda só em "pronto"; durante a conversa quem usa o microfone é o AFE da conversa.

## Configuração (NVS)

| Namespace / chave | Quem grava | Uso |
| --- | --- | --- |
| `wifi/ssid...` (lista) | Portal | Redes salvas |
| `wifi/ota_url` | Portal (aba Servidor) | Endereço do servidor |
| `wifi/force_ap` | Botão CONFIGURAR, BOOT | Abre o portal no próximo boot |
| `websocket/url`, `token`, `version` | `ConfigureServer` | Lidos pelo protocolo |
