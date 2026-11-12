# Ponte MQTT — usar o webapp de qualquer lugar (inclusive no iPhone)

O Arduino Uno/Nano não tem Wi-Fi, e o Safari do iPhone não suporta Web Serial nem Web Bluetooth. A solução é deixar o Arduino ligado por USB a um PC que roda o `ponte.py`. A ponte publica as linhas da Serial num broker MQTT na internet, e o webapp se conecta ao mesmo broker por WebSocket seguro, de qualquer rede.

```
Arduino ──USB──► PC (ponte.py) ──MQTT/TLS :8883──► broker.emqx.io ◄──WSS :8884── webapp (Safari, Chrome…)
```

**O firmware (`.ino`) não muda.** A ponte repassa o texto da Serial sem alterações, e o webapp usa o mesmo parser do USB e do Bluetooth.

## Uso rápido (broker público, sem cadastro)

Requer Python 3.8 ou superior. Na raiz do repositório:

```bash
pip install -r quinta/ponte-mqtt/requirements.txt
python quinta/ponte-mqtt/ponte.py
```

A ponte mostra algo assim:

```
  Broker:          broker.emqx.io:8883
  ID da garrafa:   garrafa-exemplo123
  Abra no celular para configurar o webapp:
  https://henriquecals.github.io/Sistemas-Embarcados-/quinta/webapp/#url=wss%3A%2F%2Fbroker.emqx.io%3A8084%2Fmqtt&id=garrafa-exemplo123

20:05:15 Serial aberta em COM3 (9600 baud)
20:05:16 Conectado ao broker broker.emqx.io:8883 (topicos em garrafa/garrafa-exemplo123/)
```

**Abra esse link no iPhone.** Você pode mandá-lo para você mesmo por WhatsApp ou e-mail. O webapp salva a configuração e já conecta, e o indicador no topo muda para **Remoto**. Deixe a janela da ponte aberta: enquanto ela rodar, a garrafa fica acessível de qualquer lugar.

- **ID da garrafa:** gerado aleatoriamente na primeira execução e salvo em `quinta/ponte-mqtt/.garrafa-id`, arquivo que não vai para o Git. Ele se mantém nas execuções seguintes, então o link continua válido.
- **Configurar à mão:** no app, toque em **Configurar conexão remota → Usar broker público** e digite o ID da garrafa.
- **Porta do Arduino:** detectada automaticamente. Se houver mais de uma, use `--serial COM3` (veja as portas com `--listar`).
- **Monitor Serial:** feche o da Arduino IDE e não conecte o webapp por USB no mesmo PC. Só um programa pode usar a porta por vez.
- **Reinício do Arduino:** ao abrir a porta, o Arduino reinicia (comportamento normal do USB). O total continua salvo na EEPROM.
- **Ver o que chega e encerrar:** use `-v` para ver cada linha recebida e `Ctrl+C` para encerrar.
- **Quedas:** se o cabo sair, a ponte publica `offline` e tenta reabrir a porta a cada 3 s. Se a internet cair, ela reconecta ao broker sozinha.

### Segurança do broker público

O `broker.emqx.io` é aberto: qualquer pessoa pode se conectar a ele. A única proteção é o **ID aleatório**, porque quem souber o ID consegue ver as leituras e enviar `TARA` ou `ZERAR`. Por isso:

- não use IDs fáceis como `garrafa1`;
- não publique o link de configuração em lugar público;
- se o ID vazar, apague o `.garrafa-id` para gerar outro e reconfigure o celular com o novo link.

Para um projeto de disciplina isso costuma bastar. Se precisar de privacidade de verdade, use um broker com senha (veja abaixo).

## Tópicos

| Tópico | Sentido | Conteúdo |
|---|---|---|
| `garrafa/<id>/serial` | ponte → app | Linhas da Serial, separadas por `\n`, agrupadas a cada 0,5 s |
| `garrafa/<id>/status` | ponte → app | `online` (ponte rodando e Arduino conectado) ou `offline`. Mensagem retida; se a ponte cair, o broker publica `offline` (*last will*) |
| `garrafa/<id>/cmd` | app → ponte | `TARA` ou `ZERAR`. Qualquer outro texto é ignorado |

## Link de configuração

A ponte monta o link a partir do endereço do webapp e de parâmetros no fragmento (`#`), que não são enviados ao servidor do GitHub Pages:

```
<webapp>#url=<wss do broker>&id=<id da garrafa>[&user=<usuário>]
```

Ao abrir o link, o webapp salva a configuração no `localStorage`, remove os parâmetros da barra de endereço e conecta. **A senha nunca vai no link.** Com um broker com senha, o app abre o formulário para você digitá-la.

Se o webapp estiver hospedado em outro endereço, use `--app-url https://…/quinta/webapp/`.

## Usando um broker com senha (opcional)

Qualquer broker com MQTT sobre TLS e **WebSocket seguro (`wss://`)** funciona. A página é servida via HTTPS, e o navegador bloqueia `ws://` sem TLS. Exemplo com o HiveMQ Cloud (plano Serverless, gratuito):

1. Em <https://console.hivemq.cloud>, crie um cluster **Serverless** e, em **Access Management**, uma credencial com permissão de *Publish and Subscribe*.
2. Rode a ponte com o host do cluster:
   ```bash
   python quinta/ponte-mqtt/ponte.py --host abc123.s1.eu.hivemq.cloud --user garrafa --password SUA_SENHA
   ```
3. Abra no celular o link que a ponte mostrar e digite a senha no app.

Para outros brokers cujo WebSocket não fique em `wss://<host>:8884/mqtt`, informe o endereço com `--wss`.

| Opção | Variável | Padrão |
|---|---|---|
| `--host` | `MQTT_HOST` | `broker.emqx.io` |
| `--port` | `MQTT_PORT` | `8883` |
| `--user` / `--password` | `MQTT_USER` / `MQTT_PASS` | — |
| `--id` | `GARRAFA_ID` | gerado e salvo em `.garrafa-id` |
| `--wss` | `MQTT_WSS` | deduzido do host |
| `--app-url` | `GARRAFA_APP_URL` | GitHub Pages do repositório |
| `--serial` | `GARRAFA_SERIAL` | detecção automática |
| `--sem-tls` | — | TLS ligado (desligue só com broker local de teste) |

## Solução de problemas

| Sintoma | Causa provável | Solução |
|---|---|---|
| "Ainda sem conexao com broker…" | Sem internet, ou a rede bloqueia a porta 8883 (comum em redes de faculdade ou empresa) | Teste em outra rede, por exemplo o roteador do celular. |
| "Erro na Serial" | Monitor Serial da IDE aberto, ou porta errada | Feche a IDE e confira com `--listar`. |
| App em "Aguardando dados…" | ID diferente nos dois lados, ou a rede do celular bloqueia a porta 8084 | Abra de novo o link da ponte e teste no 4G. |
| App mostra "A ponte no PC está offline" | `ponte.py` parado ou Arduino desconectado | Rode a ponte e confira o cabo. |

## Consumo

O Arduino imprime cerca de 2 linhas a cada 500 ms. A ponte agrupa as linhas e envia cerca de 2 mensagens por segundo, menos de 1 GB por mês ligada sem parar.
