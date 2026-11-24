#ifndef VALVULA_ACTIVA_H
#define VALVULA_ACTIVA_H

#include <Arduino.h>

// Válvula activa (mariposa) movida por el servo MG946R en GPIO5, PWM a 50 Hz.
//
// El PWM está prendido SOLO durante el entrenamiento: activar(nivel) lo prende y mueve la válvula, y
// liberar() la vuelve a abrir y APAGA el PWM (el pin queda en LOW, sin pulsos). Fuera del entrenamiento
// el servo no recibe señal: con el PWM prendido todo el tiempo dejó de andar la evaluación.
// En la evaluación: cerrar() (PIM) o activar(0) (espirometría, abierta) y después soltar(), que apaga el
// PWM SIN mover la válvula: queda en esa posición durante la medición, sin pulsos que metan ruido.
//
// 7 posiciones (duty cycle en %, a 50 Hz 1 % = 0,2 ms de pulso): abierta, cerrada y 5 niveles de
// resistencia. Dependen del armado: si se desarma la válvula hay que volver a medirlos (ver informe,
// "Calibración de la válvula activa").
class ValvulaActiva {
public:
  static constexpr float DC_ABIERTA = 10.0f;   // sin resistencia (evaluación)
  static constexpr float DC_CERRADA = 3.5f;   // oclusión total
  // Menos DC = más cerrada. Nivel 1 (resistencia mínima) = el más cerca de abierta; nivel 5 = el más
  // cerca de cerrada.
  static constexpr float DC_NIVEL[6] = {
    DC_ABIERTA,   // 0: sin nivel -> abierta
    5.5f,        // 1: resistencia mínima (la más abierta de las cinco)
    5.0f,         // 2
    4.5f,         // 3
    4.25f,         // 4
    4.0f,         // 5: resistencia máxima (sin llegar a cerrada)
  };
  static const uint32_t ESPERA_MOVIMIENTO_MS = 700;   // lo que tarda el servo en llegar a la posición

  static float dcDelNivel(int nivel) { return DC_NIVEL[(nivel >= 0 && nivel <= 5) ? nivel : 0]; }

  bool activar(int nivel);   // prende el PWM y lleva la válvula al DC del nivel (espera a que llegue)
  void liberar();            // vuelve a abrir, espera y apaga el PWM
  bool cerrar();             // prende el PWM y lleva la válvula a cerrada (oclusión total, para la PIM)
  void soltar();             // apaga el PWM sin mover la válvula (queda donde está, sin fuerza)
  bool activa() const { return conectada; }
  float dcActual() const { return dc; }

private:
  static const uint8_t PIN_SERVO = 5;
  static const uint32_t FRECUENCIA_HZ = 50;
  static const uint8_t RESOLUCION_BITS = 14;
  bool conectada = false;
  float dc = NAN;

  bool configurarDuty(float duty);
};

#endif
