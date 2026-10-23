# Changelog

## 0.2.0 — 2026-10-23

- Bateria no topo do globo (ADC do GPIO1, carregando pelo GPIO41).
- Tela de informações ao arrastar para o lado: firmware, chip, MAC, Wi-Fi, IP, servidor, bateria, memória, tempo ligado.
- Botão CONFIGURAR na tela de informações: reabre o portal com as configurações salvas.
- Palavra de ativação "hey orb" / "hi orb" no chip (MultiNet7 em inglês); partição de modelos de 4 MB.
- Versão própria do firmware (antes aparecia a 1.6.8 do XiaoZhi).

## 0.1.0 — 2026-10-23

Primeira versão sobre o xiaozhi-esp32 1.6.8 da Spotpear.

- Globo holográfico no lugar da interface de chat, com ritmo e brilho por estado.
- Tela de configuração com QR code; portal `WyGlass-XXXX` em português, com aba Servidor.
- Portal fica no ar até haver um servidor `ws://`/`wss://` válido; porta 8733 por padrão.
- Sem ativação, OTA e MQTT da nuvem do XiaoZhi: conecta direto no servidor configurado.
- Palavra de ativação do XiaoZhi desligada (esgotava a SRAM interna).
- Filtro de toque fantasma; o touch não apaga mais o Wi-Fi; sem deep sleep.
- Telas interativas por MCP (`self.screen.render`).
