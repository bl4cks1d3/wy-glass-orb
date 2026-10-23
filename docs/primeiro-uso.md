# Primeiro uso

## 1. Backup da placa de fábrica

Gravar este firmware apaga o que veio na placa. Faça o backup antes ([backup/README.md](../backup/README.md)):

```bash
python -m esptool --chip esp32s3 --port COM9 --baud 921600 read-flash 0 0x1000000 backup/fabrica-full-16MB.bin
```

## A placa não aparece como porta COM

No Windows, a placa aparece no Gerenciador de Dispositivos, mas às vezes sem porta COM. Isso acontece quando um driver ADB do Android (por exemplo, `oem61.inf` do Google) se declara para **qualquer** porta serial USB (`USB\Class_02&SubClass_02`) e pega a interface serial do ESP32-S3.

Para conferir: Gerenciador de Dispositivos → "Android ADB Interface" → Propriedades → Detalhes → IDs de hardware. Se aparecer `USB\VID_303A&PID_1001&MI_00`, é a placa.

Para corrigir, só nesse item: Driver → Atualizar driver → Procurar no computador → Permitir que eu escolha → desmarque "Mostrar hardware compatível" → **Microsoft** → **Dispositivo Serial USB**. A porta COM aparece na hora. O ADB dos celulares não é afetado.

## 2. Compilar e gravar

```bash
idf.py build
```

```bash
idf.py -p COM9 flash monitor
```

Se a gravação não conectar, segure **BOOT**, aperte e solte **RESET**, solte BOOT e tente de novo.

## 3. Ligar o servidor

O orb conversa com o `orb_gateway` do Wy Glass ([servidor.md](servidor.md)). No PC ou Pi do Wy Glass:

```bash
python orb_gateway.py
```

Ele imprime o endereço a usar no orb, por exemplo `ws://192.168.0.35:8733/orb`.

## 4. Configurar pelo celular

Sem servidor salvo, o orb abre o **portal de configuração**: a tela mostra "Configurar Wi-Fi" (ou "Configurar servidor", se o Wi-Fi já estiver salvo), um QR code e o nome da rede `WyGlass-XXXX`.

1. Escaneie o QR com o celular, ou entre na rede aberta `WyGlass-XXXX`.
2. O portal abre sozinho; se não abrir, acesse `192.168.4.1`.
3. Aba **Servidor**: o endereço do `orb_gateway`, por exemplo `ws://192.168.0.35:8733/orb`. Sem a porta, o orb usa a 8733. Salvar.
4. Aba **Wi-Fi**: escolha a rede de 2,4 GHz e a senha. Conectar.

O orb reinicia, conecta no Wi-Fi e mostra o globo com **ONLINE**. A rede precisa ser 2,4 GHz (o ESP32-S3 não usa 5 GHz), mas o servidor pode estar na de 5 GHz do mesmo roteador.

## 5. Trocar Wi-Fi ou servidor depois

- **Pela tela:** arraste para o lado para abrir as informações e toque em **CONFIGURAR**. O orb reinicia no portal, com as redes e o servidor salvos já preenchidos.
- **Pelo botão:** segure **BOOT** ao ligar.

Para sair do portal sem mudar nada, desligue e ligue: no boot seguinte ele volta ao normal.

O portal também abre sozinho se o endereço salvo não começar com `ws://` ou `wss://`.

## Testar se o servidor responde

No navegador: `http://<ip-do-servidor>:8733/health`. Ou, no repositório do Wy Glass:

```bash
python orb_gateway.py --testar ws://192.168.0.35:8733/orb
```

Se o orb mostrar **SERVIDOR OFFLINE**, confira o endereço (com a porta) e se o firewall do PC libera o Python em redes privadas.
