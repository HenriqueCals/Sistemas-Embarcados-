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

const float VARIACAO_MINIMA = 300.00;
const unsigned long TEMPO_ESTABILIZACAO = 2000;

// Intervalo entre leituras. O get_units(5) ja leva ~500 ms com o HX711 a 10 Hz,
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

float leituraReferencia = 0;
bool temReferencia = false;
bool copoFoiRetirado = false;
float totalConsumido = 0;

float ultimaLeitura = 0;
unsigned long inicioEstavel = 0;

bool estavaLeve = false;
unsigned long ultimoBipe = 0;
unsigned long ultimoCiclo = 0;

float paraMl(float peso) {
  return (peso / 100) - 5;
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
  bool leve = peso >= PESO_MINIMO && peso < LIMITE_LEVE && paraMl(peso) >= 0;

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

void registrarConsumo(float peso) {
  if (peso < PESO_MINIMO || paraMl(peso) < 0) {
    if (temReferencia) {
      copoFoiRetirado = true;
    }
    ultimaLeitura = peso;
    inicioEstavel = millis();
    return;
  }

  if (abs(peso - ultimaLeitura) > VARIACAO_MINIMA) {
    inicioEstavel = millis();
  }
  ultimaLeitura = peso;

  if (millis() - inicioEstavel < TEMPO_ESTABILIZACAO) {
    return;
  }

  if (!temReferencia) {
    leituraReferencia = peso;
    temReferencia = true;
    Serial.print("Peso inicial registrado: ");
    Serial.print(paraMl(peso), 2);
    Serial.println(" ml");
    return;
  }

  if (!copoFoiRetirado) {
    return;
  }

  float diferenca = leituraReferencia - peso;

  if (diferenca > VARIACAO_MINIMA) {
    float bebido = diferenca / 100;
    totalConsumido += bebido;
    salvarTotal();
    Serial.print("Bebeu: ");
    Serial.print(bebido, 2);
    Serial.println(" ml");
  } else if (diferenca < -VARIACAO_MINIMA) {
    Serial.println("Copo reabastecido");
  }

  leituraReferencia = peso;
  copoFoiRetirado = false;
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

  if (scale.wait_ready_timeout(TIMEOUT_HX711)) {
    float peso = scale.get_units(5);

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
