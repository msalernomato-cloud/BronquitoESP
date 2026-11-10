#include "ValvulaActiva.h"
#include <math.h>

bool ValvulaActiva::begin() {
  conectada = ledcAttach(PIN_SERVO, FRECUENCIA_HZ, RESOLUCION_BITS);
  if (!conectada) {
    Serial.println("[VALVULA] No se pudo iniciar PWM en GPIO5");
    return false;
  }
  if (!abrir()) {
    Serial.println("[VALVULA] No se pudo mover a la posición abierta");
    conectada = false;
    return false;
  }
  Serial.println("[VALVULA] PWM listo: 50 Hz, posición abierta 9%");
  return true;
}

bool ValvulaActiva::configurarDuty(float duty) {
  if (!conectada || !isfinite(duty) || duty < 0.0f || duty > 100.0f) return false;
  const uint32_t maxDuty = (1UL << RESOLUCION_BITS) - 1;
  const uint32_t valor = static_cast<uint32_t>(roundf(maxDuty * duty / 100.0f));
  return ledcWrite(PIN_SERVO, valor);
}

bool ValvulaActiva::abrir() {
  return configurarDuty(9.0f);
}

bool ValvulaActiva::configurarEntrenamiento() {
  return configurarDuty(4.0f);
}
