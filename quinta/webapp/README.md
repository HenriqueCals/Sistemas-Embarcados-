# Webapp — Garrafa Inteligente

Interface mobile, minimalista, que mostra em tempo real os dados calculados pelo `smartwaterbottle.ino`: volume atual na garrafa, nível (LEVE / MÉDIO / ALTO), goles detectados, reabastecimentos e total de água consumida.

O app **não exige nenhuma alteração no código do Arduino**: ele lê exatamente o texto que o sketch já imprime na Serial (9600 baud) e o interpreta no navegador.

---

## Sumário

0. [Contexto do projeto (para retomar o trabalho)](#contexto-do-projeto-para-retomar-o-trabalho)
1. [Como usar](#como-usar)
   - [Requisitos](#requisitos)
   - [Abrindo o app](#abrindo-o-app)
   - [Conectando ao Arduino](#conectando-ao-arduino)
   - [Modo demonstração](#modo-demonstração)
   - [Instalando no celular](#instalando-no-celular)
2. [A interface](#a-interface)
3. [O que foi feito](#o-que-foi-feito)
   - [Arquivos](#arquivos)
   - [Como os dados chegam ao app](#como-os-dados-chegam-ao-app)
   - [Parser da Serial](#parser-da-serial)
   - [Conexões](#conexões)
   - [Modo demonstração (detalhes)](#modo-demonstração-detalhes)
   - [Armazenamento local](#armazenamento-local)
   - [Design](#design)
4. [Limitações conhecidas](#limitações-conhecidas)
5. [Solução de problemas](#solução-de-problemas)

---

## Contexto do projeto (para retomar o trabalho)

> Esta seção existe para dar contexto completo a quem (pessoa ou IA) for continuar o projeto sem ter acompanhado o histórico. Mantenha-a atualizada quando algo relevante mudar.

### Visão geral

- **Repositório:** `Sistemas-Embarcados-`, um repositório de disciplina de Sistemas Embarcados. Cada pasta é o projeto de uma turma ou dia: `terca/` e `quinta/`. **Este documento trata apenas de `quinta/`.**
- **Projeto de quinta:** uma **garrafa/copo de água inteligente**. Uma célula de carga pesa o recipiente sobre uma base e, pela variação de peso, o Arduino detecta quanto a pessoa bebeu. O total consumido é acumulado.
- **Partes do projeto:**
  1. `quinta/smartwaterbottle.ino`: firmware do Arduino. É a **fonte da verdade** do comportamento.
  2. `quinta/webapp/`: webapp mobile que lê a Serial do Arduino e exibe os dados.
  3. `quinta/README.md`: requisitos, componentes e especificações *planejados* para o projeto.

### Estrutura de arquivos

```
Sistemas-Embarcados-/
├── terca/
│   └── README.md               # outro projeto, não relacionado
└── quinta/
    ├── README.md               # requisitos/componentes planejados (ver ressalva abaixo)
    ├── smartwaterbottle.ino    # firmware real
    └── webapp/
        ├── index.html          # app completo (HTML+CSS+JS, sem dependências)
        ├── manifest.webmanifest
        ├── icon.svg
        └── README.md           # este arquivo
```

### Ressalva importante sobre o `quinta/README.md`

O `quinta/README.md` descreve uma versão **planejada e mais ambiciosa** do que a implementada: sensor de temperatura DS18B20, display OLED/LCD, ESP32, bateria, meta diária calculada, lembretes e reset diário. **A lista de componentes desse README deve ser ignorada.** O dono do projeto pediu para usar o `.ino` como referência. O hardware real é apenas o descrito abaixo.

| Requisito do `quinta/README.md` | Situação atual |
|---|---|
| Medir volume em mL | ✅ Implementado no `.ino` (`paraMl`) |
| Medir volume em % | ❌ Não há capacidade máxima definida |
| Temperatura da água | ❌ Sem sensor de temperatura no `.ino` |
| Registrar total ingerido | ✅ `totalConsumido` no `.ino` |
| Meta diária | ⚠️ Só no webapp: valor fixo editável, sem cálculo automático |
| Lembretes por tempo sem beber | ❌ Não implementado (o buzzer só toca no nível LEVE) |
| Exibir em tela | ✅ Via webapp (não há display físico) |
| Reset diário automático | ⚠️ Só a lista de eventos do webapp; o total do Arduino fica salvo na EEPROM e não zera |

### Hardware real (conforme o `.ino`)

| Componente | Pinos | Função |
|---|---|---|
| Arduino (Uno/Nano, AVR) | — | Microcontrolador. Usa `tone()` e só a Serial nativa |
| Célula de carga + HX711 | DOUT → 8, SCK → 9 | Pesa o recipiente. Biblioteca `HX711.h` (bogde) |
| LED "leve" | 4 | Aceso quando o nível está baixo |
| LED "médio" | 3 | Aceso quando o nível está médio |
| LED "alto" | 2 | Aceso quando o nível está alto |
| Buzzer | 6 | Bipe de 1000 Hz por 100 ms ao entrar no nível LEVE e depois a cada 30 s enquanto continuar LEVE (não bipa com volume negativo) |
| (opcional) módulo BLE HM-10 | TX/RX (0/1) | Só para o webapp via Bluetooth. Não está no `.ino` porque é transparente |

Não há Wi-Fi, BLE ou display no firmware. **A única saída de dados é `Serial` a 9600 baud, em texto.**

### Como o firmware funciona

**Unidades.** `calibration_factor = 1.0`, então `scale.get_units()` devolve o valor **bruto** do HX711 após a tara. Cada leitura é a **mediana de 5 amostras** (`lerMediana()`), que descarta picos isolados do sensor. O código chama esse valor de `peso`. A conversão para mL é empírica:

```
ml = peso / 100 - 5        // paraMl()
gole (ml) = diferença_de_peso / 100   // o offset -5 se cancela
```

**Limiares** (em unidades brutas e o equivalente aproximado em mL):

| Constante | Bruto | ≈ mL | Uso |
|---|---|---|---|
| `PESO_MINIMO` | 250 | — | Abaixo disso, a base é considerada **vazia** (recipiente retirado) |
| `LIMITE_LEVE` | 10000 | 95 | Abaixo disso, nível **LEVE** (LED 4 + buzzer) |
| `LIMITE_MEDIO` | 25000 | 245 | Abaixo disso, **MEDIO** (LED 3). Acima, **ALTO** (LED 2) |
| `JANELA_ESTABILIDADE` | 6 leituras | — | Janela de ~3 s usada para decidir se o peso está estável |
| `TOLERANCIA_ESTAVEL` | 150 | 1,5 | Variação máxima (máx − mín) aceita dentro da janela |
| `LIMIAR_GOLE` | 500 | 5 | Diferença mínima, após retirar e recolocar, para contar gole ou reabastecimento |
| `LIMIAR_REABASTECER_NA_BASE` | 2000 | 20 | Subida mínima com a garrafa parada na base para contar reabastecimento |
| `DIFERENCA_MAXIMA` | 100000 | 1000 | Acima disso a variação é implausível (troca de garrafa, objeto estranho) e é ignorada |
| `TEMPO_MIN_FORA` | 1000 ms | — | Retiradas mais curtas são tratadas como tranco |
| `BANDA_ZERO` | 200 | 2 | Com a base vazia e estável dentro dessa faixa, o zero é corrigido (auto-zero) |

Observação: entre 250 e 500 brutos, `paraMl()` dá um valor negativo, e `garrafaNaBase()` também trata isso como "retirado".

**Ciclo do `loop()`** (a cada ~500 ms):
1. Lê 5 amostras do HX711 e usa a mediana (até 1 s de espera por amostra; se o sensor não responder, imprime `HX711 not found.`). Imprime a leitura e o nível e acende o LED correspondente. No nível LEVE, bipa ao entrar no nível e depois a cada 30 s.
2. Chama `registrarConsumo(peso)`.
3. Imprime o total consumido.

**Estabilidade.** As últimas 6 leituras ficam numa janela. O peso só é considerado estável quando **todas** estão a até 150 de distância entre si (máx − mín). O valor usado é a **média da janela**. Isso evita aceitar um peso que ainda está mudando devagar, como água balançando ou mão apoiada. A janela recomeça quando a garrafa entra ou sai da base.

**Máquina de estados de `registrarConsumo()`** (`SEM_REFERENCIA` → `NA_BASE` ⇄ `FORA_DA_BASE`):
1. **Base vazia:** se estava `NA_BASE`, passa para `FORA_DA_BASE` e marca o início da retirada. Se a base vazia estiver estável perto de 0, corrige o zero do HX711 aos poucos (**auto-zero**, compensa a deriva térmica).
2. **Peso instável:** nada acontece.
3. **Primeiro peso estável (`SEM_REFERENCIA`):** vira `leituraReferencia` e imprime `Peso inicial registrado`.
4. **Peso estável depois de uma retirada de pelo menos 1 s:**
   - Referência − peso > 5 ml: **gole**. Soma ao `totalConsumido`, salva na EEPROM e imprime `Bebeu`.
   - Referência − peso < −5 ml: imprime `Copo reabastecido`. O total não muda.
   - Diferença > 1000 ml: imprime `Variacao implausivel ignorada`.
   - Nesses três casos a referência passa a ser o peso atual. Se a diferença for pequena (±5 ml), a **referência é mantida**, então goles pequenos se acumulam e são contados numa retirada seguinte.
   - Retiradas com menos de 1 s são tratadas como tranco: nada é avaliado.
5. **Garrafa parada na base:** a referência não acompanha quedas. Se ela tiver sido capturada com a mão apoiada, o excesso é descontado na retirada seguinte e o total fecha certo. Só uma subida estável acima de 20 ml (água despejada sem tirar a garrafa) imprime `Copo reabastecido` e atualiza a referência.

**Limitação:** se a pessoa bebe e reabastece **na mesma retirada**, a balança só vê o saldo final, e esse gole se perde.

**Persistência:** `totalConsumido` é salvo na EEPROM a cada gole e recuperado no `setup()`, então sobrevive a resets e quedas de energia. Não há RTC, então não há reset diário.

**Tara na inicialização:** o `setup()` avisa na Serial e pisca os LEDs por 5 s para a garrafa ser retirada antes do `tare()`. Se ela continuar na base, o peso dela vira o zero e as medições ficam erradas.

### Contrato de saída Serial (o que o webapp depende)

Exemplo ilustrativo de saída (formato exato do `.ino`, mas os valores são fictícios):

```
Initializing the scale...
Before setting up the scale:
read: 		-123456
Scale is zeroed. Place a weight on the scale...
Leitura: 445.10 ml -> ALTO
Peso inicial registrado: 445.10 ml
Total de agua consumida: 0.00 ml
Leitura: -4.80 ml -> vazio
Total de agua consumida: 0.00 ml
Leitura: 398.70 ml -> ALTO
Bebeu: 46.40 ml
Total de agua consumida: 46.40 ml
```

O webapp faz parsing exatamente dessas strings (veja [Parser da Serial](#parser-da-serial)). **Qualquer alteração no texto dos `Serial.print` quebra o app**, a menos que o array `RULES` em `index.html` seja ajustado junto. Se o firmware evoluir, uma melhoria natural seria emitir JSON por linha (ex.: `{"ml":398.7,"nivel":"ALTO","total":46.4}`) e adaptar o parser.

### Decisões tomadas

| Decisão | Motivo |
|---|---|
| Ignorar a lista de componentes do `quinta/README.md` | Pedido explícito do dono do projeto. O `.ino` é a referência |
| Preservar o texto da Serial ao evoluir o `.ino` | O app lê exatamente essas linhas. A lógica de detecção pode mudar, mas as mensagens devem continuar iguais |
| Webapp em HTML único, sem build nem frameworks | Simples de hospedar (GitHub Pages) e de manter numa disciplina |
| Web Serial (USB) e Web Bluetooth (BLE UART) | São as únicas formas de um navegador falar com um Arduino sem Wi-Fi |
| Modo demonstração que replica a lógica do `.ino` | Permite testar e apresentar sem o hardware |
| Meta diária só no app (padrão 2000 ml) | O firmware não tem esse conceito |
| Interface em português (pt-BR), minimalista | Pedido do dono do projeto |

### Estado atual e próximos passos possíveis

**Estado:**
- Firmware funcional com a detecção de goles (último commit relevante: "adicionado monitoramento de consumo").
- Webapp criado e documentado. Sintaxe JS validada; testado apenas via modo demonstração, **ainda não com hardware real**.

**Ideias de evolução** (nenhuma implementada):
- Calibrar o HX711 (`calibration_factor`) e trocar `paraMl()` por uma conversão em gramas, com 1 g ≈ 1 mL.
- Adicionar sensor de temperatura e a capacidade da garrafa, para mostrar a %. Isso exigiria novas linhas na Serial e novas regras no parser.
- Usar um RTC para o reset diário (e um comando para zerar o total salvo na EEPROM).
- Lembretes de hidratação, no buzzer e/ou como notificação no webapp.
- Mover o BLE para `SoftwareSerial`, para usar USB e Bluetooth ao mesmo tempo.
- Migrar para ESP32 (BLE ou Wi-Fi nativos) e emitir JSON.

---

## Como usar

### Requisitos

| Item | Detalhe |
|---|---|
| Navegador | **Google Chrome** (Android ou desktop) ou outro baseado em Chromium (Edge, Opera). Safari/iOS e Firefox não suportam Web Serial nem Web Bluetooth. |
| Contexto seguro | A página precisa ser aberta via **HTTPS** ou **localhost**. Abrir o `index.html` direto do disco (`file://`) desabilita USB e Bluetooth. |
| Para Bluetooth | Um módulo **BLE** de serial transparente (ex.: HM-10, AT-09, ou um ESP32 rodando Nordic UART). Módulos **HC-05/HC-06 não funcionam** — são Bluetooth clássico, e navegadores só acessam BLE. |
| Para USB | Cabo USB do Arduino ligado ao computador, ou ao celular Android com adaptador OTG. |

### Abrindo o app

**Opção A — GitHub Pages (recomendado para usar no celular)**

1. No GitHub, vá em *Settings → Pages* do repositório.
2. Em *Source*, escolha a branch `main` e a pasta raiz.
3. Acesse `https://<usuario>.github.io/<repositorio>/quinta/webapp/` no Chrome do celular.

**Opção B — servidor local (para testar no computador)**

Na raiz do repositório:

```bash
npx serve quinta/webapp
# ou
python -m http.server 8000 --directory quinta/webapp
```

Depois abra `http://localhost:3000` (serve) ou `http://localhost:8000` (python) no Chrome.

### Conectando ao Arduino

#### Via Bluetooth (BLE)

1. Ligue o módulo BLE ao Arduino:

   | Módulo BLE | Arduino |
   |---|---|
   | VCC | 5V (ou 3.3V, conforme o módulo) |
   | GND | GND |
   | RXD | TX (pino 1) — use divisor de tensão se o módulo for 3.3V |
   | TXD | RX (pino 0) |

   > Como o módulo fica nos pinos 0/1 (a mesma Serial do USB), desconecte o RXD do módulo ao gravar o sketch.

2. Ligue o Arduino e confirme que o módulo está piscando (anunciando).
3. No app, toque em **Conectar via Bluetooth**.
4. Escolha o módulo na lista (geralmente aparece como `HMSoft`, `BT05`, `MLT-BT05` ou similar).
5. O indicador no topo muda para **Bluetooth** com um ponto verde.

#### Via USB

1. Conecte o Arduino por USB (no celular, use um adaptador OTG).
2. Feche o Monitor Serial da Arduino IDE, porque só um programa pode usar a porta de cada vez.
3. No app, toque em **Conectar via USB** e escolha a porta do Arduino.
4. O indicador no topo muda para **USB** com um ponto verde.

#### Desconectando

Toque em **Desconectar**, no fim da tela.

### Modo demonstração

Toque em **Modo demonstração** para ver o app funcionando sem hardware. Ele simula uma garrafa sendo retirada da base, bebida e devolvida, e às vezes reabastecida. A simulação usa a mesma lógica do `.ino` (veja [detalhes](#modo-demonstração-detalhes)).

### Instalando no celular

Com o app aberto no Chrome do Android, toque no menu ⋮ e depois em **Adicionar à tela inicial** (ou **Instalar app**). O app passa a abrir em tela cheia, como um aplicativo nativo.

---

## A interface

```
┌──────────────────────────────┐
│ Garrafa          ● Bluetooth │  ← status da conexão
├──────────────────────────────┤
│            ╭────╮            │
│           │ 640  │           │  ← total consumido (ml)
│           │  ml  │           │     anel = progresso da meta
│            ╰────╯            │
│   32% da meta de 2000 ml     │  ← toque para editar a meta
├──────────────┬───────────────┤
│ NA GARRAFA   │ ÚLTIMO GOLE   │
│ 312 ml       │ 48 ml         │
│ ▬▬▬ ▬▬▬ ░░░  │ há 3 min      │
│ nível médio  │               │
├──────────────┴───────────────┤
│ ATIVIDADE            Limpar  │
│ ↓ Bebeu 48 ml         14:32  │
│ ↑ Garrafa reabastecida 14:10 │
│ • Peso inicial: 450 ml 13:55 │
└──────────────────────────────┘
```

| Elemento | Origem do dado | Observação |
|---|---|---|
| Total consumido (anel) | `Total de agua consumida: X ml` | Calculado pelo Arduino. Reinicia se o Arduino reiniciar. |
| Meta diária | Definida no app | Padrão de 2000 ml, editável e salva no aparelho. |
| Na garrafa | `Leitura: X ml -> NIVEL` | Mostra `—` quando a garrafa está fora da base (`vazio`). |
| Barras de nível | Nível da mesma linha | Imitam os LEDs: 1 barra laranja = LEVE, 2 = MÉDIO, 3 = ALTO. |
| Último gole | `Bebeu: X ml` | Mostra também há quanto tempo foi. |
| Atividade | `Bebeu`, `Copo reabastecido`, `Peso inicial registrado` | Até 30 eventos visíveis, salvos por dia. |
| Alerta vermelho | `HX711 not found.` | Aparece quando o sensor de peso não responde. |
| Ponto amarelo + "Aguardando dados…" | Ausência de linhas por mais de 5 s | A conexão está aberta, mas nada está chegando. |

---

## O que foi feito

### Arquivos

```
quinta/webapp/
├── index.html            # App completo (HTML + CSS + JS, sem dependências)
├── manifest.webmanifest  # Permite instalar como app (PWA)
├── icon.svg              # Ícone (gota d'água)
└── README.md             # Este documento
```

O app é **um único arquivo HTML**, sem frameworks, sem etapa de build e sem bibliotecas externas. Funciona servido por qualquer servidor estático.

### Como os dados chegam ao app

```
HX711 ──► Arduino (smartwaterbottle.ino)
              │  Serial.print(...) @ 9600 baud
              ├──► USB ─────────────► Web Serial API ─────┐
              └──► módulo BLE (TX) ─► Web Bluetooth API ──┤
                                                          ▼
                                     parser de linhas (feed → handleLine)
                                                          ▼
                                            estado (state) → render()
```

Independentemente do meio (USB, BLE ou demo), o texto recebido passa pela mesma função `feed()`. Ela acumula os pedaços em um buffer, separa por `\n` e envia cada linha completa para `handleLine()`.

### Parser da Serial

Cada linha impressa pelo `.ino` é reconhecida por uma expressão regular:

| Linha no `.ino` | Regex | Efeito no app |
|---|---|---|
| `Leitura: 312.40 ml -> MEDIO` | `/^Leitura:\s*(-?[\d.]+)\s*ml\s*->\s*(\w+)/` | Atualiza volume atual e nível. Se for `vazio`, o volume vira `—`. |
| `Total de agua consumida: 640.00 ml` | `/^Total de agua consumida:\s*(-?[\d.]+)/` | Atualiza o total e o anel. |
| `Bebeu: 48.20 ml` | `/^Bebeu:\s*([\d.]+)/` | Adiciona evento de gole e atualiza "Último gole". |
| `Copo reabastecido` | `/^Copo reabastecido/` | Adiciona evento de reabastecimento. |
| `Peso inicial registrado: 450.00 ml` | `/^Peso inicial registrado:\s*(-?[\d.]+)/` | Adiciona evento de início. |
| `HX711 not found.` | `/HX711 not found/` | Mostra o alerta e limpa a leitura atual. |

As demais linhas (`Initializing the scale...`, `read: ...`, `Variacao implausivel ignorada` etc.) são ignoradas.

> **Importante:** se o texto de algum `Serial.print` do `.ino` for alterado, a regex correspondente em `RULES` (no `index.html`) precisa ser ajustada também.

### Conexões

**USB — Web Serial API**
- `navigator.serial.requestPort()` abre o seletor de portas.
- A porta é aberta com `baudRate: 9600`, o mesmo valor de `Serial.begin(9600)`.
- O fluxo de bytes passa por um `TextDecoderStream` e é lido em loop.
- Ao desconectar, o leitor é cancelado e a porta é fechada corretamente. Se o cabo for removido, o app detecta e volta ao estado desconectado.

**Bluetooth — Web Bluetooth API**
- `requestDevice({ acceptAllDevices: true })` lista os dispositivos BLE próximos.
- O app tenta, em ordem, dois serviços UART comuns:

  | Padrão | Serviço | Característica (notify) |
  |---|---|---|
  | HM-10 / AT-09 / BT05 | `0000ffe0-…` | `0000ffe1-…` |
  | Nordic UART (ESP32, nRF) | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | `6e400003-…` |

- As notificações recebidas são decodificadas com `TextDecoder` em modo streaming, para não quebrar caracteres entre pacotes, e enviadas ao `feed()`.
- O evento `gattserverdisconnected` detecta quedas de conexão.

**Detecção de suporte**: se o navegador não tiver Web Serial ou Web Bluetooth, o botão correspondente fica desabilitado e uma nota explica o motivo: falta de HTTPS ou navegador incompatível.

### Modo demonstração (detalhes)

O modo demo é uma **tradução direta da lógica do `.ino` para JavaScript**:

- Usa as mesmas constantes do `.ino` (`PESO_MINIMO`, `LIMITE_LEVE`, `LIMITE_MEDIO`, `JANELA`, `TOLERANCIA_ESTAVEL`, `LIMIAR_GOLE`, `LIMIAR_REABASTECER_NA_BASE`, `DIFERENCA_MAXIMA`, `TEMPO_MIN_FORA`, `BANDA_ZERO`, `GANHO_ZERO`).
- Usa a mesma conversão: `paraMl(peso) = peso / 100 - 5`.
- Reimplementa `registrarConsumo()`: janela de estabilidade, máquina de estados, tranco, auto-zero, goles acumulados e reabastecimento.
- Simula `lerMediana()`: 5 amostras com ruído e picos ocasionais (5%), além de uma deriva lenta do zero, para exercitar a mediana e o auto-zero.
- A cada 500 ms (como o `INTERVALO_LEITURA` do `loop()`), gera um peso simulado e **imprime as mesmas linhas** que o Arduino imprimiria.

Essas linhas passam pelo mesmo parser usado com o hardware real. Assim, o modo demo também serve de teste do parser.

### Armazenamento local

Usa o `localStorage` do navegador, apenas no próprio aparelho:

| Chave | Conteúdo |
|---|---|
| `goal` | Meta diária em ml. |
| `day` | `{ date, events }`: eventos do dia (até 200). Se a data mudar, a lista recomeça. |

O botão **Limpar** apaga os eventos do dia.

### Design

- Layout em coluna única, com largura máxima de 440 px, pensado para celular.
- Respeita as áreas seguras de telas com notch (`env(safe-area-inset-*)`).
- Tema **claro/escuro automático**, conforme o sistema (`prefers-color-scheme`).
- Fonte do sistema e números tabulares, para que os valores não "pulem" ao mudar.
- Paleta neutra, com um único azul de destaque (água). Laranja aparece só no nível LEVE e vermelho só no erro do sensor.
- O anel de progresso é um SVG animado via `stroke-dashoffset`.

---

## Limitações conhecidas

- **Total depende do Arduino**: o `totalConsumido` é calculado e salvo na EEPROM pelo Arduino, e o app só exibe o valor recebido.
- **Sem reset diário no Arduino**: o total só zera quando o Arduino reinicia. O app zera a lista de eventos a cada dia, mas o número do anel é o que o Arduino envia.
- **iPhone/iOS**: o Safari não suporta Web Serial nem Web Bluetooth. Em iOS seria necessário um app nativo ou um navegador como o Bluefy (só para BLE).
- **Pinos 0/1 compartilhados**: com o módulo BLE nos pinos 0/1, USB e Bluetooth disputam a mesma Serial. Para usar os dois, seria preciso mover o BLE para `SoftwareSerial` e duplicar os `print` no `.ino`.
- **Não testado com hardware real** até o momento. A validação foi feita via modo demonstração.

---

## Solução de problemas

| Sintoma | Causa provável | Solução |
|---|---|---|
| Botões USB/Bluetooth desabilitados | Página aberta via `file://` ou navegador incompatível | Sirva via HTTPS ou localhost e use o Chrome. |
| Módulo não aparece na lista Bluetooth | É um HC-05/HC-06 (Bluetooth clássico) | Troque por um módulo BLE (HM-10 ou similar). |
| "Dispositivo sem serviço UART compatível" | O módulo usa UUIDs diferentes | Adicione os UUIDs do módulo em `BLE_UARTS` no `index.html`. |
| Conecta, mas fica em "Aguardando dados…" | Baud rate do módulo BLE diferente de 9600, ou TX/RX invertidos | Configure o módulo para 9600 (`AT+BAUD4` no HM-10) e confira a fiação. |
| Porta USB não abre | Monitor Serial da Arduino IDE está aberto | Feche o Monitor Serial. |
| Valores estranhos ou negativos | Célula de carga sem calibração (`calibration_factor = 1.0`) | Calibre o HX711 e ajuste `calibration_factor` e `paraMl()` no `.ino`. |
| Alerta "HX711 não encontrado" | Fiação do HX711 (pinos 8/9) solta | Verifique DOUT → 8, SCK → 9, VCC e GND. |
