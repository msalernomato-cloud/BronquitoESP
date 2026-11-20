// Prueba del servo de la válvula: SOLO manda PWM al GPIO5 (50 Hz) y lo mueve entre posiciones.
// Monitor serie a 115200. Escribí un número (ej. 5.5) para fijar ese duty cycle (%) y dejarlo quieto;
// escribí "auto" para volver a recorrer las posiciones.

const int PIN_SERVO = 5;
const int FRECUENCIA_HZ = 50;
const int RESOLUCION_BITS = 14;

const float POSICIONES[] = {9.0, 5.5, 5.0, 4.5, 4.0, 3.5, 2.5};   // abierta, niveles 1-5, cerrada
const char *NOMBRES[]    = {"abierta", "nivel 1", "nivel 2", "nivel 3", "nivel 4", "nivel 5", "cerrada"};
const int N_POS = 7;

bool automatico = true;
int pos = 0;
unsigned long ultimo = 0;

void fijarDuty(float duty) {
  uint32_t maximo = (1UL << RESOLUCION_BITS) - 1;
  ledcWrite(PIN_SERVO, (uint32_t)(maximo * duty / 100.0f + 0.5f));
  Serial.printf("DC %.1f %% (pulso %.2f ms)\n", duty, duty * 0.2f);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!ledcAttach(PIN_SERVO, FRECUENCIA_HZ, RESOLUCION_BITS)) {
    Serial.println("ERROR: no se pudo iniciar el PWM en GPIO5");
    while (true) delay(1000);
  }
  Serial.println("PWM listo en GPIO5. Recorriendo posiciones cada 2 s (escribí un DC o 'auto').");
}

void loop() {
  if (Serial.available()) {
    String c = Serial.readStringUntil('\n');
    c.trim();
    if (c.equalsIgnoreCase("auto")) {
      automatico = true;
      Serial.println("Modo automático");
    } else if (c.length() > 0) {
      float d = c.toFloat();
      if (d >= 1.0f && d <= 15.0f) {
        automatico = false;
        Serial.print("Fijo: ");
        fijarDuty(d);
      } else {
        Serial.println("DC fuera de rango (1 a 15)");
      }
    }
  }

  if (automatico && millis() - ultimo >= 2000) {
    ultimo = millis();
    Serial.printf("%s: ", NOMBRES[pos]);
    fijarDuty(POSICIONES[pos]);
    pos = (pos + 1) % N_POS;
  }
}
