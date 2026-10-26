# Formato .orb

Um arquivo `.orb` é a animação de um orb, estado por estado, pronta para a placa tocar. Trocar de orb é gravar outro arquivo: não precisa compilar o firmware.

```
Orb Studio (JS/CSS) ──renderiza──► .orb (quadros JPEG por estado) ──USB/Wi-Fi──► partição orbs ──► placa toca
```

Com um `.orb` válido gravado, a placa toca o arquivo no lugar do orb compilado (`orb_studio::kStyle`). Sem arquivo, ou com um arquivo inválido, volta ao orb compilado.

## Estrutura

Todos os números em little-endian.

| Offset | Tamanho | Conteúdo |
| --- | --- | --- |
| 0 | 4 | `ORB1` |
| 4 | 4 | Tamanho total do arquivo, em bytes |
| 8 | 2 | Tamanho do cabeçalho JSON (H), até 8192 |
| 10 | 2 | Reservado (0) |
| 12 | H | Cabeçalho JSON (UTF-8) |
| 12+H | 4 | Número de quadros (N), até 4000 |
| 16+H | 8×N | Para cada quadro: offset (a partir do início do arquivo) e tamanho |
| … | … | Quadros JPEG |

Cabeçalho:

```json
{
  "v": 1,
  "id": "amoeba",
  "name": "Ameba",
  "width": 192,
  "height": 192,
  "fps": 15,
  "background": ["#071B2C", "#020711", "#010208"],
  "states": {"starting": [0, 30], "idle": [30, 30], "connecting": [60, 30],
             "listening": [90, 30], "speaking": [120, 30], "error": [150, 30]}
}
```

| Campo | Regra |
| --- | --- |
| `v` | `1` |
| `width`, `height` | `192` (o quadrado do globo, em `(24, 14)` na tela de 240×240) |
| `fps` | 1 a 30; o recomendado é 15 |
| `background` | Gradiente do fundo da tela (centro, meio, borda), atrás e em volta do quadrado |
| `states` | `[primeiro quadro, quantidade]` por estado. `idle` é obrigatório; um estado ausente usa os quadros do `idle` |

Estados e quando a placa usa cada um: `starting` (ligando e conectando no Wi-Fi), `idle` (pronto), `connecting` (abrindo a conversa), `listening` (ouvindo), `speaking` (falando), `error` (erro grave).

Os quadros são **JPEG baseline** (não progressivo) de 192×192. A borda do quadro deve combinar com o `background`, porque fora do quadrado a placa desenha só o gradiente.

## Na placa

- **Partição** `orbs` (dados, subtipo `0x40`, `0xA00000`, 6 MB), no lugar do antigo `ota_1`. O arquivo fica no início dela. Regravar o firmware (`idf.py flash`) não apaga o orb.
- **Player** (`main/display/orb_pack.{h,cc}`, `OrbUi::RenderPack`): no boot, lê e confere o cabeçalho; a cada tick, calcula o quadro pelo relógio real (`elapsed × fps`, em laço dentro do estado) e só decodifica quando o quadro muda. Ao trocar de estado, a animação começa do primeiro quadro do estado novo.
- **Decodificação:** `esp_jpeg` com o TJpgDec da ROM, direto em RGB565 no canvas do globo. Medido na placa: **~37 ms por quadro**, incluindo a leitura da flash; a 15 qps, ~14 quadros por segundo efetivos. O log mostra `Orb de arquivo: X ms por quadro` a cada 150 quadros.
- **Memória:** o quadro lido vai para a PSRAM; a área de trabalho do decodificador (3,1 KB) é alocada uma vez na SRAM interna.

Tamanho típico: 6 estados × 2 s × 15 qps = 180 quadros de 5 a 8 KB, **~1,2 MB**. Cabe com folga nos 6 MB.

## Troca pelo Wi-Fi

A placa confere o servidor (o `orb_gateway`, no mesmo endereço do WebSocket) no boot, a cada minuto quando está em "pronto" e na hora quando o servidor manda a ferramenta MCP `self.orb.update` (troca por voz):

```
GET http://<servidor>:8733/orbs/current  ->  {"id": "zeta", "name": "...", "size": 895123, "sha256": "..."}  (ou null)
GET http://<servidor>:8733/orbs/zeta.orb ->  o arquivo
```

Se o `sha256` for diferente do registrado (NVS `orb/sha256`), ela primeiro calcula o sha256 do que já está na partição: se bater (o orb foi gravado pela USB), só registra. Senão, baixa o arquivo para a partição `orbs`, confere o sha256 e troca de orb **sem reiniciar**; a tela mostra `TROCANDO ORB` enquanto baixa. Os 4 bytes `ORB1` são gravados por último: um download cortado deixa o arquivo inválido e a placa volta ao orb compilado, nunca a um orb quebrado. `null` (nenhum orb atual) deixa a placa com o que tem.

Código: `main/orb_sync.{h,cc}` (consulta e download), `OrbUi::SetSwapping` e `OrbUi::ReloadPack` (troca sem reiniciar). Medido na placa: 874 KB baixados e trocados em poucos segundos, dentro do minuto da consulta.

O orb atual é o `current.txt` da pasta da biblioteca. Quem escreve: o botão **Usar no orb** do Studio e a ferramenta de voz `trocar_orb` do gateway.

## Ferramentas

```bash
python studio/orbpack.py info studio/library/amoeba.orb
```

```bash
python studio/orbpack.py flash studio/library/amoeba.orb --port COM9
```

No Orb Studio: "Gerar .orb deste tipo" (aba Personalizar) renderiza o tipo escolhido e grava em `studio/library/`; "Gravar orb pela USB" (aba Compilar e gravar) grava o arquivo na placa em ~8 s.
