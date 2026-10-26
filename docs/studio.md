# Orb Studio

Ambiente visual para personalizar, compilar e gravar o firmware do orb, e para desenhar as telas interativas. Roda no seu computador: um servidor Python local e a página no navegador.

```bash
python studio/server.py
```

Abre `http://127.0.0.1:8740`. Precisa do Python 3.10+, do `pyserial` (`pip install pyserial`) e do ESP-IDF instalado (o Studio procura em `IDF_PATH` ou em `~/esp/esp-idf`). Opções: `--port` para outra porta, `--no-browser` para não abrir o navegador.

O servidor só aceita conexões do próprio computador (`127.0.0.1`), porque ele grava arquivos e mexe na placa.

## Personalizar

Tudo o que muda a aparência e o comportamento do orb sem mexer no código:

| Grupo | Opções |
| --- | --- |
| Tipo de orb | Galeria com miniaturas animadas: globo holográfico, anéis orbitais, esfera de plasma, onda de voz, nuvem de partículas, pulso minimalista |
| Identidade | Título da tela de informações, prefixo da rede do portal (`WyGlass-XXXX`), servidor padrão |
| Descanso de tela | Depois de quantos minutos sem conversa (ou nunca) e se a tela escurece ou apaga |
| Ativação por voz | Liga/desliga, frases em inglês ("hey orb", "hi orb"), limiar |
| Globo | Meridianos, pontos, velocidade base, cor das linhas fixas, brilho em volta, cor dos textos, gradientes do fundo e da esfera |
| Estados | Para iniciando, pronto, conectando, ouvindo, falando e erro: cor, velocidade, brilho, pulso, período e texto |

A **pré-visualização** usa a mesma conta do firmware (`orb_ui.cc`), em 240×240, e mostra também a tela de configuração e a de informações. Os textos aparecem sem acentos, como na placa, porque a fonte do globo só tem ASCII.

Com o **servidor padrão** preenchido, um orb recém-gravado conecta direto nele sem pedir o endereço no portal (o portal ainda abre se não houver Wi-Fi salvo). O endereço salvo no portal tem prioridade sobre o padrão.

**Salvar** grava `studio/orb.json`. **Aplicar no firmware** também gera `main/orb_studio_config.h` e atualiza a palavra de ativação no `sdkconfig`. Depois é preciso compilar e gravar.

## Compilar e gravar

| Botão | Faz |
| --- | --- |
| Compilar | `idf.py build` |
| Gravar | `idf.py -p <porta> flash` |
| Compilar e gravar | Os dois em seguida |
| Limpar build | `idf.py fullclean` (o próximo build leva alguns minutos) |
| Parar | Interrompe a tarefa |
| Abrir monitor | Log serial da placa |
| Reiniciar a placa e abrir | Reinicia pela USB e já mostra o boot |

"Aplicar a personalização antes" (ligado por padrão) aplica o que estiver na aba Personalizar antes de compilar. A porta do orb (USB `303A`) aparece primeiro na lista. A gravação fecha o monitor sozinha, porque usa a mesma porta.

O log mostra a saída do build e a serial na mesma tela, com filtro (tudo, build, serial); erros em vermelho e avisos em amarelo.

## Telas

Editor das telas interativas que o servidor mostra com `self.screen.render` ([screen-layout.md](screen-layout.md)):

- **Elementos:** arraste da paleta para a tela (ou clique): título, texto, caixas de seleção (até 6), progresso, timer e botões (até 3).
- **Ordem:** a placa empilha sempre título, texto, caixas, progresso, timer e botões. Caixas e botões podem ser reordenados arrastando.
- **Propriedades:** `id` da tela e de cada caixa e botão (voltam nos eventos de toque), estilo dos botões, fechar ao tocar, fechar sozinha.
- **JSON:** atualizado ao vivo e editável; o que você colar ali volta para o editor. Copiar já gera o JSON compacto para o argumento `layout` de `self.screen.render`.
- **Telas salvas** ficam em `studio/layouts/<nome>.json`.

A prévia é aproximada: a placa usa as fontes e o espaçamento do LVGL.

## Tipos de orb

| Tipo | id | Desenho | Usa |
| --- | --- | --- | --- |
| Globo holográfico | `globe` | Meridianos girando, paralelos e pontos de luz | Meridianos, pontos |
| Anéis orbitais | `rings` | Três órbitas inclinadas com um cometa cada, em volta de um núcleo | — |
| Esfera de plasma | `plasma` | Luz suave com ondas e redemoinho por dentro (calculada por pixel com tabelas) | — |
| Onda de voz | `wave` | Três ondas circulares entrelaçadas | — |
| Nuvem de partículas | `particles` | Esfera de pontos 3D inclinada, girando | Pontos (×3, até 300) |
| Pulso minimalista | `pulse` | Núcleo que respira e dois anéis que se expandem no ritmo do pulso | Período do pulso |
| Ameba | `amoeba` | Personagem verde com manchas, braços e pés; ondula e respira. Olhos e boca mudam com o estado: felizes (pronto), abertos com boca em "o" (ouvindo), boca mexendo (falando), olhando de lado (conectando), olhos em X (erro) | Velocidade e pulso; as cores do corpo são fixas |
| Cérebro alienígena | `brain` | Inspirado no Brainiac: esfera verde com circuitos roxos, pulsos de dados e três diodos vermelhos que respiram, acendem, piscam em sequência ou giram conforme o estado | Velocidade e pulso |
| Super Choque | `shock` | Inspirado no Super Choque: núcleo azul com raios de estática e faíscas amarelas; 3 a 6 raios conforme o estado, vermelhos no erro | Velocidade (ritmo do tremor) e pulso |
| Robô infiltrador | `zeta` | Inspirado no Projeto Zetta: cabeça metálica, visor com olhos ciano que piscam, abrem ou ficam vermelhos, scanner ao conectar, boca-equalizador ao falar e linha de holograma | Velocidade e pulso |

Cor, velocidade, brilho, pulso e texto de cada estado valem para todos os tipos de luz; a ameba usa só velocidade, pulso e texto. O tipo é escolhido na compilação: trocar exige compilar e gravar de novo.

### Criar um tipo de orb

Um tipo é um **pintor**: a cada quadro (~20 por segundo) ele escreve "luz" de 0 a 1 num quadrado de 192×192 em volta do globo, e o firmware pinta essa luz na cor do estado por cima do fundo. O mesmo pintor existe em C (placa) e em JavaScript (pré-visualização), com a mesma conta.

1. **Registro do Studio** (`studio/server.py`, `STYLES`): `"meu_tipo": ("kMeuTipo", "Nome")`. O gerador acrescenta `kMeuTipo` ao `enum class Style` do cabeçalho.
2. **Firmware** (`main/display/orb_ui.{h,cc}`):
   - declare `void PaintMeuTipo();` no `orb_ui.h`;
   - escreva o pintor no `orb_ui.cc` com as primitivas `Splat` (ponto), `Disc` (disco), `Glow` (brilho gaussiano), `Circle` (anel) e `AddLight`; centro em `kCx - kBoxX`, `kCy - kBoxY`, raio `kR`; animação por `angle_` (gira com a velocidade do estado) e `phase_ms_ / pulse_period_ms_` (ciclo do pulso);
   - acrescente o `case` no `switch` do `RenderGlobe`;
   - se o tipo quiser a esfera, os paralelos ou o contorno fixos, inclua-o em `kSphereFill`, `kParallels` ou `kOutline`.
3. **Pré-visualização** (`studio/web/globe.js`, `STYLES`): uma entrada com `name`, `description`, as mesmas flags `sphere`/`parallels`/`outline` e `paint(p)` usando `p.splat`, `p.disc`, `p.glow`, `p.circle`, `p.angle` e `p.phase`.
4. Reinicie o Studio: o tipo aparece na galeria.

**Arte fixa** (cérebro, choque e robô): o que não se mexe é desenhado uma vez no boot sobre o fundo (`BakeStyle`, com `BaseBlend`/`BaseDot`; no Studio, `bake` e `p.baseBlend`/`p.baseDot`), e o pintor de cada quadro só desenha o que anima.

**Tipos em cores** (como a ameba): em vez de luz, o pintor desenha direto na imagem, depois do fundo. No firmware ele roda depois da composição em `RenderGlobe` e usa `BlendPixel`, `Dot` e `Ellipse` com cores RGB; no Studio a entrada tem `direct: true` e usa `p.blendRgb`, `p.dot` e `p.ellipse`. O estado atual está em `last_state_` (C) e `p.state` (JS), para mudar a expressão.

Custo: o pintor roda no ESP32-S3 a cada 50 ms junto com áudio e Wi-Fi. Pontos e linhas (alguns milhares de `Splat`) são baratos; contas por pixel precisam de tabelas pré-calculadas no boot, como a esfera de plasma faz.

## Orbs em arquivo

**Usar no orb**, em cada cartão da galeria, troca o orb sem compilar num clique: gera o `.orb` do tipo com as cores e ritmos da aba, marca como orb atual (`studio/library/current.txt`, que o `orb_gateway` serve) e, se a placa estiver na USB, grava na hora. Sem USB, a placa troca sozinha pelo Wi-Fi em até um minuto. Clicar no desenho do cartão só escolhe o tipo **compilado**, que vale ao compilar e gravar e só aparece se nenhum `.orb` estiver instalado.

"Gerar .orb deste tipo" renderiza o tipo escolhido (com as cores e ritmos da aba) em 2 s por estado, a 15 quadros por segundo, e grava `studio/library/<tipo>.orb`. Na aba Compilar e gravar, "Gravar orb pela USB" grava só esse arquivo na placa, em ~8 s, sem compilar. Formato e detalhes em [formato-orb.md](formato-orb.md).

## Arquivos

| Arquivo | Papel |
| --- | --- |
| `studio/server.py` | Servidor local: configuração, geração, tarefas do ESP-IDF, serial |
| `studio/web/` | Página (HTML, CSS e JavaScript sem build) |
| `studio/orb.json` | Personalização (fonte da verdade) |
| `studio/layouts/` | Telas desenhadas |
| `studio/orbpack.py` | Monta, confere e grava arquivos `.orb` (`info`, `flash`) |
| `studio/library/` | Arquivos `.orb` gerados (fora do git) |
| `main/orb_studio_config.h` | Gerado: aparência e textos do globo, rede do portal, servidor padrão |
