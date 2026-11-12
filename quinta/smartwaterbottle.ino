#include "HX711.h"
#include <EEPROM.h>

const int LOADCELL_DOUT_PIN = 8;
const int LOADCELL_SCK_PIN = 9;

const int LED_LEVE  = 4;
const int LED_MEDIO = 3;
const int LED_ALTO  = 2;

const int BUZZER = 6;

const float PESO_MINIMO = 250.00;
const float LIMITE_LEVE = 10000.00;
const float LIMITE_MEDIO = 25000.00;

// --- Deteccao de consumo ---
// Uma leitura so vale quando as ultimas JANELA_ESTABILIDADE leituras ficam
// todas dentro de TOLERANCIA_ESTAVEL (max - min). A referencia e a media da janela.
const int JANELA_ESTABILIDADE = 6;              // 6 leituras x 500 ms = 3 s
const float TOLERANCIA_ESTAVEL = 150.00;        // ~1,5 ml
// Diferenca minima, apos retirar e recolocar, para contar gole ou reabastecimento.
// Diferencas menores mantem a referencia, entao pequenos goles se acumulam.
const float LIMIAR_GOLE = 500.00;               // ~5 ml
// Subida minima com a garrafa parada na base para considerar reabastecimento
// (agua despejada sem retirar). Maior que LIMIAR_GOLE porque a garrafa pode ser tocada.
const float LIMIAR_REABASTECER_NA_BASE = 2000.00; // ~20 ml
// Diferencas acima disso sao implausiveis (troca de garrafa, objeto estranho).
const float DIFERENCA_MAXIMA = 100000.00;       // ~1 L
// Retiradas mais curtas que isso sao tratadas como tranco, nao como gole.
const unsigned long TEMPO_MIN_FORA = 1000;

// --- Auto-zero ---
// Com a base vazia e estavel dentro desta faixa, o zero do HX711 e corrigido
// aos poucos, compensando a deriva termica da celula de carga.
const float BANDA_ZERO = 200.00;
const float GANHO_ZERO = 0.5;

// Cada leitura e a mediana de 5 amostras, que descarta picos isolados do HX711.
const int AMOSTRAS_MEDIANA = 5;

// Intervalo entre leituras. As 5 amostras ja levam ~500 ms com o HX711 a 10 Hz,
// entao o ciclo e controlado por tempo em vez de somar um delay fixo.
const unsigned long INTERVALO_LEITURA = 500;

// Tempo maximo esperando o HX711 ficar pronto antes de considerar que ele sumiu.
const unsigned long TIMEOUT_HX711 = 1000;

// Com o nivel LEVE, bipa ao entrar no nivel e depois so a cada esse intervalo.
const unsigned long INTERVALO_BIPE = 30000;

// Tempo para retirar a garrafa da base antes da tara.
const unsigned long TEMPO_AVISO_TARA = 5000;

// EEPROM: assinatura + total consumido (float).
const int EEPROM_ENDERECO_ASSINATURA = 0;
const int EEPROM_ENDERECO_TOTAL = 2;
const uint16_t EEPROM_ASSINATURA = 0xA71D;

HX711 scale;

float calibration_factor = 1.0;

enum Estado { SEM_REFERENCIA, NA_BASE, FORA_DA_BASE };
Estado estado = SEM_REFERENCIA;

float leituraReferencia = 0;
float totalConsumido = 0;

unsigned long inicioFora = 0;
unsigned long ultimoFora = 0;

float janela[JANELA_ESTABILIDADE];
int indiceJanela = 0;
int preenchidas = 0;
bool janelaComGarrafa = false;

bool estavaLeve = false;
unsigned long ultimoBipe = 0;
unsigned long ultimoCiclo = 0;

float paraMl(float peso) {
  return (peso / 100) - 5;
}

bool garrafaNaBase(float peso) {
  return peso >= PESO_MINIMO && paraMl(peso) >= 0;
}

// Le AMOSTRAS_MEDIANA valores e devolve a mediana. Retorna false se o HX711 nao responder.
bool lerMediana(float &peso) {
  float amostras[AMOSTRAS_MEDIANA];
  for (int i = 0; i < AMOSTRAS_MEDIANA; i++) {
    if (!scale.wait_ready_timeout(TIMEOUT_HX711)) {
      return false;
    }
    amostras[i] = scale.get_units(1);
  }

  // Insertion sort: poucas amostras, sem alocacao.
  for (int i = 1; i < AMOSTRAS_MEDIANA; i++) {
    float x = amostras[i];
    int j = i - 1;
    while (j >= 0 && amostras[j] > x) {
      amostras[j + 1] = amostras[j];
      j--;
    }
    amostras[j + 1] = x;
  }

  peso = amostras[AMOSTRAS_MEDIANA / 2];
  return true;
}

void limparJanela() {
  preenchidas = 0;
}

// A janela guarda so leituras do mesmo tipo (com garrafa ou base vazia).
void adicionarNaJanela(float peso, bool comGarrafa) {
  if (comGarrafa != janelaComGarrafa) {
    limparJanela();
    janelaComGarrafa = comGarrafa;
  }
  janela[indiceJanela] = peso;
  indiceJanela = (indiceJanela + 1) % JANELA_ESTABILIDADE;
  if (preenchidas < JANELA_ESTABILIDADE) {
    preenchidas++;
  }
}

// Estavel = janela cheia e todas as leituras dentro de TOLERANCIA_ESTAVEL.
bool janelaEstavel(float &media) {
  if (preenchidas < JANELA_ESTABILIDADE) {
    return false;
  }
  float minimo = janela[0];
  float maximo = janela[0];
  float soma = 0;
  for (int i = 0; i < JANELA_ESTABILIDADE; i++) {
    minimo = min(minimo, janela[i]);
    maximo = max(maximo, janela[i]);
    soma += janela[i];
  }
  media = soma / JANELA_ESTABILIDADE;
  return (maximo - minimo) <= TOLERANCIA_ESTAVEL;
}

// Corrige aos poucos o zero da balanca enquanto a base esta vazia e estavel.
void ajustarZero(float media) {
  if (fabs(media) > BANDA_ZERO) {
    return;
  }
  long correcao = (long)(media * scale.get_scale() * GANHO_ZERO);
  if (correcao == 0) {
    return;
  }
  scale.set_offset(scale.get_offset() + correcao);
  // As leituras da janela usam o zero antigo: recomeca para nao corrigir duas vezes.
  limparJanela();
}

void apagarLeds() {
  digitalWrite(LED_LEVE, LOW);
  digitalWrite(LED_MEDIO, LOW);
  digitalWrite(LED_ALTO, LOW);
}

void carregarTotal() {
  uint16_t assinatura;
  EEPROM.get(EEPROM_ENDERECO_ASSINATURA, assinatura);

  if (assinatura == EEPROM_ASSINATURA) {
    EEPROM.get(EEPROM_ENDERECO_TOTAL, totalConsumido);
    if (isnan(totalConsumido) || totalConsumido < 0) {
      totalConsumido = 0;
    }
  } else {
    // Primeira execucao (EEPROM nunca usada por este sketch).
    totalConsumido = 0;
    EEPROM.put(EEPROM_ENDERECO_ASSINATURA, EEPROM_ASSINATURA);
    EEPROM.put(EEPROM_ENDERECO_TOTAL, totalConsumido);
  }
}

void salvarTotal() {
  // put() so regrava os bytes que mudaram, poupando a EEPROM.
  EEPROM.put(EEPROM_ENDERECO_TOTAL, totalConsumido);
}

void avisarTara() {
  Serial.println("Retire a garrafa da base: a balanca sera zerada em 5 segundos...");

  // Pisca os LEDs durante o aviso.
  unsigned long inicio = millis();
  bool aceso = false;
  while (millis() - inicio < TEMPO_AVISO_TARA) {
    aceso = !aceso;
    digitalWrite(LED_LEVE, aceso);
    digitalWrite(LED_MEDIO, aceso);
    digitalWrite(LED_ALTO, aceso);
    delay(250);
  }
  apagarLeds();
}

void atualizarBuzzer(float peso) {
  bool leve = garrafaNaBase(peso) && peso < LIMITE_LEVE;

  if (!leve) {
    estavaLeve = false;
    return;
  }

  if (!estavaLeve || millis() - ultimoBipe >= INTERVALO_BIPE) {
    tone(BUZZER, 1000, 100);
    ultimoBipe = millis();
  }
  estavaLeve = true;
}

// Compara o peso estavel apos recolocar a garrafa com a referencia de antes da retirada.
void avaliarRetirada(float media) {
  float diferenca = leituraReferencia - media;

  if (fabs(diferenca) > DIFERENCA_MAXIMA) {
    Serial.println("Variacao implausivel ignorada");
    leituraReferencia = media;
  } else if (diferenca > LIMIAR_GOLE) {
    float bebido = diferenca / 100;
    totalConsumido += bebido;
    salvarTotal();
    Serial.print("Bebeu: ");
    Serial.print(bebido, 2);
    Serial.println(" ml");
    leituraReferencia = media;
  } else if (diferenca < -LIMIAR_GOLE) {
    Serial.println("Copo reabastecido");
    leituraReferencia = media;
  }
  // Diferenca pequena: mantem a referencia. Assim goles menores que o limiar
  // se acumulam e sao contados na proxima retirada, em vez de se perderem.
}

void registrarConsumo(float peso) {
  bool naBase = garrafaNaBase(peso);
  adicionarNaJanela(peso, naBase);

  float media;
  bool estavel = janelaEstavel(media);

  if (!naBase) {
    if (estado == NA_BASE) {
      estado = FORA_DA_BASE;
      inicioFora = millis();
    }
    if (estado == FORA_DA_BASE) {
      ultimoFora = millis();
    }
    if (estavel) {
      ajustarZero(media);
    }
    return;
  }

  if (!estavel) {
    return;
  }

  if (estado == SEM_REFERENCIA) {
    leituraReferencia = media;
    estado = NA_BASE;
    Serial.print("Peso inicial registrado: ");
    Serial.print(paraMl(media), 2);
    Serial.println(" ml");
    return;
  }

  if (estado == FORA_DA_BASE) {
    estado = NA_BASE;
    if (ultimoFora - inicioFora >= TEMPO_MIN_FORA) {
      avaliarRetirada(media);
      return;
    }
    // Retirada curta demais: foi um tranco. Segue como se nao tivesse saido.
  }

  // Garrafa parada na base. A referencia nao acompanha quedas: se ela foi
  // capturada com a mao apoiada, o excesso e descontado na proxima retirada.
  // So uma subida grande e estavel (agua despejada na base) muda a referencia.
  if (media - leituraReferencia > LIMIAR_REABASTECER_NA_BASE) {
    Serial.println("Copo reabastecido");
    leituraReferencia = media;
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Initializing the scale...");

  pinMode(LED_LEVE, OUTPUT);
  pinMode(LED_MEDIO, OUTPUT);
  pinMode(LED_ALTO, OUTPUT);
  apagarLeds();

  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);

  carregarTotal();
  Serial.print("Total salvo recuperado: ");
  Serial.print(totalConsumido, 2);
  Serial.println(" ml");

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  avisarTara();

  if (scale.wait_ready_timeout(TIMEOUT_HX711)) {
    Serial.println("Before setting up the scale:");
    Serial.print("read: \t\t");
    Serial.println(scale.read());
  }

  scale.set_scale(calibration_factor);
  scale.tare();

  Serial.println("Scale is zeroed. Place a weight on the scale...");
}

void loop() {
  if (millis() - ultimoCiclo < INTERVALO_LEITURA) {
    return;
  }
  ultimoCiclo = millis();

  float peso;
  if (lerMediana(peso)) {
    Serial.print("Leitura: ");
    Serial.print(paraMl(peso), 2);
    Serial.print(" ml -> ");

    apagarLeds();

    if (peso < PESO_MINIMO) {
      Serial.println("vazio");
    } else if (peso < LIMITE_LEVE) {
      digitalWrite(LED_LEVE, HIGH);
      Serial.println("LEVE");
    } else if (peso < LIMITE_MEDIO) {
      digitalWrite(LED_MEDIO, HIGH);
      Serial.println("MEDIO");
    } else {
      digitalWrite(LED_ALTO, HIGH);
      Serial.println("ALTO");
    }

    atualizarBuzzer(peso);
    registrarConsumo(peso);

    Serial.print("Total de agua consumida: ");
    Serial.print(totalConsumido, 2);
    Serial.println(" ml");
  } else {
    Serial.println("HX711 not found.");
    apagarLeds();
    noTone(BUZZER);
    estavaLeve = false;
  }
}
