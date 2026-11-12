- **Requisitos**
  - Medir o volume de água na garrafa (em mL e em porcentagem) e a temperatura da água em tempo real
  - Calcular a meta diária de hidratação com base no consumo detectado ao longo do dia (a partir da variação de volume entre uma leitura e outra)
  - Registrar o total de água já ingerido e comparar com a meta diária
  - Emitir lembretes quando o usuário ficar muito tempo sem beber ou estiver abaixo do ritmo esperado para atingir a meta
  - Exibir na tela o volume atual, a temperatura e o progresso em relação à meta
  - Reiniciar a contagem de consumo automaticamente a cada novo dia
- **Componentes**
  - Sensor de temperatura à prova d'água (ex.: DS18B20)
  - Célula de carga com módulo amplificador (ex.: HX711) para pesar a água e converter em volume
  - Display para mostrar volume, temperatura e progresso (ex.: OLED ou LCD)
  - Microcontrolador (ex.: ESP32 ou Arduino) para ler os sensores e controlar o display
  - Bateria recarregável e módulo de carga
  - Protoboard, jumpers e resistores para montagem do protótipo
- **Especificações**
  - Faixa de temperatura da água suportada (ex.: 0 °C a 60 °C)
  - Capacidade máxima de volume da garrafa (ex.: 500 mL ou 1 L)
  - Precisão da medição de volume (ex.: ±X mL)
  - Display legível com atualização em tempo real do volume, temperatura e meta
  - Cálculo do consumo médio e da meta diária de água por usuário
  - Bateria recarregável com autonomia mínima (ex.: X horas de uso)
  - Reset diário automático da contagem de consumo

---

## Detecção de consumo

O firmware (`smartwaterbottle.ino`) não mede o gole diretamente. Ele compara o peso da garrafa **antes de ela sair da base** com o peso **depois que ela volta e assenta**. A diferença é o quanto foi bebido (ou reabastecido).

A ideia é simples. A dificuldade está em decidir **quando uma leitura é confiável**: água balançando, mão apoiada na garrafa, picos de ruído do HX711 e a deriva do sensor com a temperatura geram leituras erradas. Se uma delas virar referência, o erro entra no total. Por isso a detecção usa as camadas abaixo.

### 1. Leitura filtrada pela mediana

Cada leitura é a **mediana de 5 amostras** do HX711, e não a média. Um pico isolado (um valor absurdo no meio de valores normais) fica numa das pontas da lista ordenada e é descartado, enquanto na média ele puxaria o resultado.

### 2. Janela de estabilidade

O peso só é aceito quando as **últimas 6 leituras (~3 s) estão todas próximas entre si** (diferença entre a maior e a menor ≤ ~1,5 ml). O valor usado é a **média dessas 6 leituras**.

A versão anterior só comparava cada leitura com a anterior. Uma mudança lenta, como a garrafa assentando ou a mão encostada, mudava pouco a cada passo e passava como estável. A janela olha o intervalo todo e pega esse caso.

### 3. Máquina de estados

```
                 peso estável
 SEM_REFERENCIA ──────────────► NA_BASE ◄──────────────────────┐
  (ligou agora)    "Peso inicial    │                           │ voltou e assentou:
                    registrado"     │ base ficou vazia           │ compara com a referência
                                    ▼                           │
                               FORA_DA_BASE ────────────────────┘
                              (pessoa bebendo)
```

Quando a garrafa volta e o peso estabiliza, o firmware compara com a referência:

| Situação | O que acontece |
|---|---|
| Ficou fora **menos de 1 s** | É um tranco. Nada é contado |
| Peso **caiu mais de 5 ml** | **Gole**: soma ao total, salva na EEPROM, imprime `Bebeu: X ml` |
| Peso **subiu mais de 5 ml** | **Reabastecimento**: imprime `Copo reabastecido`. O total não muda |
| Variação **maior que 1 L** | Implausível (outra garrafa, objeto estranho). Só atualiza a referência |
| Variação **menor que 5 ml** | Mantém a referência antiga, para que goles pequenos se acumulem |

### 4. Casos especiais

- **Goles pequenos não se perdem.** Um gole de 3 ml fica abaixo do limiar, mas como a referência não muda, o próximo gole é medido a partir do mesmo ponto. Três goles de 3 ml viram uma diferença de 9 ml e são contados.
- **Mão apoiada ao recolocar.** Se a referência for capturada com a mão empurrando a garrafa, ela fica alta demais. Como a referência não acompanha quedas enquanto a garrafa está na base, o excesso é descontado na retirada seguinte e o total fecha certo.
- **Reabastecer sem tirar da base.** Uma subida estável acima de 20 ml com a garrafa parada é tratada como reabastecimento. O limiar é maior que o do gole porque a garrafa pode ser tocada enquanto está na base.
- **Auto-zero.** Toda vez que a base fica vazia e estável perto de 0, o zero do HX711 é corrigido aos poucos. Isso compensa a deriva térmica da célula de carga, que antes só era zerada ao ligar.

### Limitação conhecida

Se a pessoa **bebe e reabastece na mesma retirada**, a balança só vê o peso final, e o gole se perde. Nenhum algoritmo baseado apenas em peso resolve isso. Seria preciso um sinal extra, como um botão de "reabasteci" ou um sensor na tampa.

### Parâmetros e calibração

Os limiares ficam no topo do `.ino`, em unidades brutas do HX711 (100 unidades ≈ 1 ml):

| Constante | Valor | ≈ ml | Função |
|---|---|---|---|
| `JANELA_ESTABILIDADE` | 6 leituras | — | Tamanho da janela (~3 s) |
| `TOLERANCIA_ESTAVEL` | 150 | 1,5 | Variação máxima aceita dentro da janela |
| `LIMIAR_GOLE` | 500 | 5 | Mínimo para contar gole ou reabastecimento após retirada |
| `LIMIAR_REABASTECER_NA_BASE` | 2000 | 20 | Mínimo para reabastecimento sem retirar |
| `DIFERENCA_MAXIMA` | 100000 | 1000 | Acima disso a variação é ignorada |
| `TEMPO_MIN_FORA` | 1000 ms | — | Retiradas mais curtas são tranco |
| `BANDA_ZERO` / `GANHO_ZERO` | 200 / 0,5 | 2 | Faixa e intensidade do auto-zero |

**Como ajustar ao seu sensor:**

1. Deixe a garrafa parada na base e observe as linhas `Leitura:` na Serial por um minuto.
2. Anote quanto o valor oscila. Essa oscilação é o ruído da sua célula de carga.
3. Use para `TOLERANCIA_ESTAVEL` um valor um pouco acima dessa oscilação.
4. Use para `LIMIAR_GOLE` entre 3 e 5 vezes essa oscilação.

Se aparecerem goles falsos, aumente `LIMIAR_GOLE`. Se goles reais demorarem a ser registrados, reduza `JANELA_ESTABILIDADE` ou aumente `TOLERANCIA_ESTAVEL`.

### Comparação com a versão anterior

Mesmos cenários simulados nas duas versões do firmware (com ruído, picos e deriva):

| Cenário | Esperado | Antes | Agora |
|---|---|---|---|
| Goles e reabastecimento normais | 200 ml | 198,9 ml | 199,0 ml |
| Mão apoiada ao recolocar | 90 ml | 90,0 ml | 90,0 ml |
| Tranco + três goles de 3 ml | 9 ml | 3,1 ml | 6,0 ml¹ |
| Reabastecer sem tirar da base, depois beber 40 ml | 40 ml | 0 ml | 40,0 ml |
| Objeto de 3 kg colocado na base | 30 ml | 2580 ml | 30,0 ml |

¹ Os 3 ml restantes ficam acumulados e entram na próxima retirada.

Detalhes de implementação e o contrato da Serial usado pelo webapp estão em [`webapp/README.md`](webapp/README.md#como-o-firmware-funciona).
