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
| Identidade | Título da tela de informações, prefixo da rede do portal (`WyGlass-XXXX`), servidor padrão |
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

## Arquivos

| Arquivo | Papel |
| --- | --- |
| `studio/server.py` | Servidor local: configuração, geração, tarefas do ESP-IDF, serial |
| `studio/web/` | Página (HTML, CSS e JavaScript sem build) |
| `studio/orb.json` | Personalização (fonte da verdade) |
| `studio/layouts/` | Telas desenhadas |
| `main/orb_studio_config.h` | Gerado: aparência e textos do globo, rede do portal, servidor padrão |
