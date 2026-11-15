#include "ValvulaActiva.h"
#include <math.h>

constexpr float ValvulaActiva::DC_NIVEL[6];

bool ValvulaActiva::activar(int nivel) {
  if (!conectada) {
    conectada = ledcAttach(PIN_SERVO, FRECUENCIA_HZ, RESOLUCION_BITS);
    if (!conectada) {
      Serial.println("[VALVULA] No se pudo iniciar el PWM en GPIO5");
      return false;
    }
  }
  float objetivo = dcDelNivel(nivel);
  if (!configurarDuty(objetivo)) {
    Serial.println("[VALVULA] No se pudo fijar el duty cycle");
    liberar();
    return false;
  }
  dc = objetivo;
  Serial.printf("[VALVULA] PWM prendido: nivel %d -> DC %.1f %% (pulso %.2f ms)\n", nivel, dc, dc * 0.2f);
  delay(ESPERA_MOVIMIENTO_MS);
  return true;
}

void ValvulaActiva::liberar() {
  if (!conectada) return;
  if (configurarDuty(DC_ABIERTA)) delay(ESPERA_MOVIMIENTO_MS);   // que llegue a abierta antes de cortar
  else Serial.println("[VALVULA] No se pudo volver a abrir la válvula");
  ledcDetach(PIN_SERVO);
  pinMode(PIN_SERVO, OUTPUT);
  digitalWrite(PIN_SERVO, LOW);   // sin pulsos: el servo queda quieto y sin fuerza
  conectada = false;
  dc = NAN;
  Serial.println("[VALVULA] Válvula abierta y PWM apagado");
}

bool ValvulaActiva::configurarDuty(float duty) {
  if (!conectada || !isfinite(duty) || duty < 0.0f || duty > 100.0f) return false;
  const uint32_t maxDuty = (1UL << RESOLUCION_BITS) - 1;
  const uint32_t valor = static_cast<uint32_t>(roundf(maxDuty * duty / 100.0f));
  return ledcWrite(PIN_SERVO, valor);
}
