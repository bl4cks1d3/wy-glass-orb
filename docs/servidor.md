# Servidor

O orb não tem inteligência própria: ele abre um WebSocket com o servidor configurado no portal e troca áudio e mensagens de controle. O servidor do Wy Glass é o **`orb_gateway.py`**, no repositório [wy-glass](https://github.com/bl4cks1d3/wy-glass).

```
orb (este firmware) ──ws://ip:8733/orb──► orb_gateway.py ──► Gemini Live (orb_live.py)
  Opus 16 kHz (mic)                          │                 + prompt e ferramentas do live_agent
  Opus 24 kHz (fala)  ◄──────────────────────┘
```

## orb_gateway

```bash
python orb_gateway.py
```

Configuração em `config.json > orb_gateway` (padrões entre parênteses):

| Chave | Uso |
| --- | --- |
| `host`, `port`, `path` | Onde escuta (`0.0.0.0`, `8733`, `/orb`) |
| `mode` | `live` (padrão) conversa com o Gemini Live; `eco` devolve o que você falou, para testar microfone e alto-falante |
| `tokens` | Tokens aceitos em `Authorization: Bearer`; vazio aceita qualquer aparelho da rede |
| `live.blocked_tools` | Ferramentas fora do orb (padrão: `terminal`, `delegate_task`, `pc_system`, `pc_keyboard`, `pc_click`) |
| `live.silence_ms` | Silêncio que encerra a fala (`500`) |
| `live.idle_close_seconds` | Fecha a conversa sem fala (`45`) |
| `live.keep_session_minutes` | Sessão do Gemini aberta depois de desconectar, para o próximo toque responder na hora (`10`) |

Credenciais, voz e modelo vêm do mesmo `config.json` do Live dos óculos.

### Teste

- Navegador: `http://<ip>:8733/health` → `{"ok": true, "mode": "live", "orbs": [...]}`, com os orbs conectados agora.
- Terminal: `python orb_gateway.py --testar ws://<ip>:8733/orb` faz o papel de um orb e confere o `hello`.

## Protocolo

O mesmo WebSocket do xiaozhi-esp32, versão 1 ([websocket.md](websocket.md), em chinês). Resumo do que este firmware usa:

**Conexão:** o orb conecta quando a conversa começa (toque ou "hey orb") e fecha quando ela termina. Cabeçalhos: `Authorization: Bearer <token>` (se houver), `Protocol-Version: 1`, `Device-Id: <MAC>`, `Client-Id: <UUID>`.

| Sentido | Mensagem |
| --- | --- |
| Orb → servidor | `{"type":"hello","version":1,"transport":"websocket","features":{"mcp":true},"audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}` |
| Servidor → orb | `{"type":"hello","transport":"websocket","session_id":"...","audio_params":{"format":"opus","sample_rate":24000,"channels":1,"frame_duration":60}}` |
| Orb → servidor | `{"type":"listen","state":"start","mode":"auto"}`, depois quadros binários Opus de 60 ms |
| Servidor → orb | `{"type":"tts","state":"start"}`, quadros binários Opus, `{"type":"tts","state":"stop"}` |
| Orb → servidor | `{"type":"abort"}` (toque durante a fala) |
| Ambos | `{"type":"mcp","payload":{...}}` (JSON-RPC: ferramentas `self.*` do orb, como `self.screen.render`) |

Regras que importam para um servidor:

- **Ritmo:** mande o áudio da resposta em tempo real (no máximo ~0,3 s adiantado). A fila do orb guarda só 2,4 s e descarta o excesso.
- **Half-duplex:** em modo `auto`, o orb para de mandar microfone entre `tts start` e `tts stop`, e volta a ouvir (novo `listen start`) depois do `stop`.
- **Taxa de saída:** o `sample_rate` do `hello` do servidor é a taxa do Opus da resposta; 24 kHz evita reamostragem no orb.

## Endereço do servidor

Fica na NVS do orb (`wifi/ota_url`, salvo pelo portal) e é copiado para `websocket/url` no boot. Sem porta, o orb completa com `:8733`. Endereços que não começam com `ws://` ou `wss://` abrem o portal.
