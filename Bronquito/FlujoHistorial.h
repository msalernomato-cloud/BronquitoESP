#ifndef FLUJO_HISTORIAL_H
#define FLUJO_HISTORIAL_H

#include <Arduino.h>
#include "UI.h"
#include "TarjetaSD.h"

// Historial: lista de evaluaciones guardadas, resumen de cada una y gráficos.
class FlujoHistorial {
public:
  FlujoHistorial(BronquitoUI &ui, TarjetaSD &tarjeta);

  // Menú del historial. Vuelve cuando el usuario elige "Volver al menú principal"
  void ejecutar();

private:
  BronquitoUI &ui;
  TarjetaSD &tarjeta;

  void flujoCalibraciones();
  void verEvaluacion(const String &id, const String &fecha);
  void graficarEvaluacion(const String &id, bool esPim);
};

#endif
