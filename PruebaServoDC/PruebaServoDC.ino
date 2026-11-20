// Prueba del servo: manda UN SOLO duty cycle fijo al GPIO5 (50 Hz) y lo deja quieto.
// Sirve para ir buscando las posiciones de abierta, niveles 1-5 y cerrada.
//
// Monitor serie a 115200 (fin de línea: "Nueva línea"):
//   - Escribí un número (ej. 5.5)  -> fija ese DC (%)
//   - "+" o "-"                    -> sube/baja 0.1 %
//   - "++" o "--"                  -> sube/baja 0.5 %
//   - "?"                          -> muestra el DC actual

const int PIN_SERVO = 5;
const int FRECUENCIA_HZ = 50;
const int RESOLUCION_BITS = 14;

float DC_INICIAL = 7.5;   // posición con la que arranca (cambiala si querés)
float dcActual = DC_INICIAL;

void fijarDuty(float duty) {
  if (duty < 1.0f || duty > 15.0f) {
    Serial.println("DC fuera de rango (1 a 15)");
    return;
  }
  dcActual = duty;
  uint32_t maximo = (1UL << RESOLUCION_BITS) - 1;
  ledcWrite(PIN_SERVO, (uint32_t)(maximo * duty / 100.0f + 0.5f));
  Serial.printf("DC %.2f %% (pulso %.3f ms)\n", duty, duty * 0.2f);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!ledcAttach(PIN_SERVO, FRECUENCIA_HZ, RESOLUCION_BITS)) {
    Serial.println("ERROR: no se pudo iniciar el PWM en GPIO5");
    while (true) delay(1000);
  }
  Serial.println("PWM listo en GPIO5. Escribí un DC, + / - / ++ / -- o ?");
  fijarDuty(DC_INICIAL);
}

void loop() {
  if (!Serial.available()) return;

  String c = Serial.readStringUntil('\n');
  c.trim();
  if (c.length() == 0) return;

  if (c == "+")       fijarDuty(dcActual + 0.1f);
  else if (c == "-")  fijarDuty(dcActual - 0.1f);
  else if (c == "++") fijarDuty(dcActual + 0.5f);
  else if (c == "--") fijarDuty(dcActual - 0.5f);
  else if (c == "?")  Serial.printf("DC actual: %.2f %%\n", dcActual);
  else                fijarDuty(c.toFloat());
}
