#ifndef FLUJO_HISTORIAL_H
#define FLUJO_HISTORIAL_H

#include <Arduino.h>
#include "UI.h"
#include "Evaluaciones.h"

// Historial: lista de evaluaciones guardadas, resumen de cada una y gráficos.
class FlujoHistorial {
public:
  FlujoHistorial(BronquitoUI &ui, Evaluaciones &evaluaciones);

  // Menú del historial. Vuelve cuando el usuario elige "Volver al menú" (false) o "Enviar a la web" (true):
  // el envío lo hace FlujoConexion, desde el .ino
  bool ejecutar();

private:
  BronquitoUI &ui;
  Evaluaciones &evaluaciones;

  void flujoCalibraciones();
  void verEvaluacion(const String &id, const String &fecha, const String &tipo);
  void graficarEvaluacion(const String &id, bool esPim);
};

#endif
