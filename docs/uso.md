# Uso

## Tela principal: o globo

Um globo holográfico em wireframe (meridianos girando, paralelos e pontos luminosos), com o estado escrito embaixo e a bateria no topo.

| Estado | Globo | Texto |
| --- | --- | --- |
| Iniciando / conectando no Wi-Fi | Gira rápido | `CONECTANDO WI-FI`, depois `WI-FI OK` |
| Pronto | Gira devagar, pulso leve | `ONLINE` |
| Conectando no servidor | Gira 4× mais rápido | `CONECTANDO` |
| Ouvindo | Mais claro, pulsando, **anel vermelho na borda** | `OUVINDO` |
| Falando | Pulsa no ritmo da fala | `FALANDO` |
| Última conexão falhou | Ponto vermelho no topo | `SERVIDOR OFFLINE` |
| Erro grave | Vermelho | `ERRO` |

O anel vermelho de microfone aberto é controlado só pelo firmware: o servidor não consegue abrir o microfone sem ele aparecer.

## Conversar

- **Toque** na tela, ou diga **"hey orb"** / **"hi orb"** (em inglês: "rei órb"). Com a voz, espere o "ding" e o `OUVINDO` antes de falar.
- Fale normalmente; o servidor detecta o fim da fala e responde pelo alto-falante.
- Depois da resposta, o orb volta a ouvir sozinho.
- **Tocar enquanto ele fala** corta a resposta. Enquanto fala, o orb não ouve, para não se interromper com a própria voz.
- **Encerrar:** toque enquanto ele ouve, diga "tchau", ou fique 45 s em silêncio.

## Gestos

| Gesto | Onde | Faz |
| --- | --- | --- |
| Toque curto | Globo | Abre a conversa / encerra / corta a fala |
| Arrastar para o lado | Qualquer lugar | Alterna entre o globo e as informações |
| Toque no meio | Informações | Volta ao globo |
| Toque em **CONFIGURAR** | Informações (embaixo) | Reinicia no portal de configuração |
| Segurar BOOT ao ligar | Botão | Abre o portal de configuração |

Toques muito curtos (menos de 30 ms) são ignorados: o touch desta placa às vezes lê toques que não existem.

## Tela de informações

| Linha | Exemplo |
| --- | --- |
| Firmware e data do build | `FW 0.2.0  Oct 23 2026` |
| Chip, flash e PSRAM | `ESP32-S3 r0.2  16MB/8MB` |
| MAC | `MAC 90:70:69:f4:6d:30` |
| Wi-Fi e sinal | `WiFi MinhaRede -46dBm` |
| IP | `IP 192.168.0.53` |
| Servidor | `SRV 192.168.0.35:8733` |
| Bateria | `BAT 76% carregando` |
| Memória livre | `RAM 70K  PSRAM 5900K` |
| Tempo ligado | `LIGADO 0:12:31` |

Atualiza a cada segundo enquanto estiver aberta.

## Descanso de tela

Sem conversa por um tempo (padrão: 1 minuto; ajustável no Studio: nunca, 1, 2, 5, 10, 15 ou 30 min), a tela escurece ou apaga e o orb para de desenhar. Um toque só acorda (não abre conversa); "hey orb" e conversas vindas do servidor também acordam.

## Trocar de orb

O desenho do orb pode ser trocado sem gravar o firmware: pelo Studio (**Usar no orb**), ou por voz durante a conversa ("troca o orb para o robô"). A tela mostra `TROCANDO ORB` por alguns segundos enquanto baixa.

## Bateria

Ícone e porcentagem no topo do globo; fica **vermelho** em 20% ou menos e mostra um **raio** carregando. O orb não entra em deep sleep (veja Descanso de tela).

## Telas do servidor

O servidor pode desenhar telas próprias por cima do globo (confirmações, listas, timer), pela ferramenta MCP `self.screen.render`. Enquanto uma delas estiver aberta, o toque vai para ela. Esquema em [screen-layout.md](screen-layout.md).
