#ifndef VALVULA_ACTIVA_H
#define VALVULA_ACTIVA_H

#include <Arduino.h>

class ValvulaActiva {
public:
  bool begin();
  bool disponible() const { return conectada; }
  bool abrir();
  bool configurarEntrenamiento();

private:
  static const uint8_t PIN_SERVO = 5;
  static const uint32_t FRECUENCIA_HZ = 50;
  static const uint8_t RESOLUCION_BITS = 14;
  bool conectada = false;

  bool configurarDuty(float duty);
};

#endif
