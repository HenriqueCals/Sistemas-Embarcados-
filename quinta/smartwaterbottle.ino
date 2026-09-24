#include "HX711.h"

const int LOADCELL_DOUT_PIN = 8;
const int LOADCELL_SCK_PIN = 9;

const int LED_LEVE  = 4;
const int LED_MEDIO = 3;
const int LED_ALTO  = 2;

const int BUZZER = 6;

const float PESO_MINIMO = 250.00;
const float LIMITE_LEVE = 10000.00;
const float LIMITE_MEDIO = 25000.00;

HX711 scale;

float calibration_factor = 1.0;

void apagarLeds() {
  digitalWrite(LED_LEVE, LOW);
  digitalWrite(LED_MEDIO, LOW);
  digitalWrite(LED_ALTO, LOW);
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

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  Serial.println("Before setting up the scale:");
  Serial.print("read: \t\t");
  Serial.println(scale.read());

  scale.set_scale(calibration_factor);
  scale.tare();

  Serial.println("Scale is zeroed. Place a weight on the scale...");
}

void loop() {
  if (scale.is_ready()) {
    float peso = scale.get_units(5);

    Serial.print("Leitura: ");
    Serial.print((peso/100)-5, 2);
    Serial.print(" ml -> ");

    apagarLeds();

    if (peso < PESO_MINIMO) {
      Serial.println("vazio");
    } else {
      noTone(BUZZER);
      if (peso < LIMITE_LEVE) {
        tone(BUZZER,1000,100);
        digitalWrite(LED_LEVE, HIGH);
        Serial.println("LEVE");
      } else if (peso < LIMITE_MEDIO) {
        digitalWrite(LED_MEDIO, HIGH);
        Serial.println("MEDIO");
      } else {
        digitalWrite(LED_ALTO, HIGH);
        Serial.println("ALTO");
      }
    }
  } else {
    Serial.println("HX711 not found.");
    apagarLeds();
    noTone(BUZZER);
  }

  delay(500);
}
