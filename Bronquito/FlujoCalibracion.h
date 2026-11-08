#ifndef FLUJO_CALIBRACION_H
#define FLUJO_CALIBRACION_H

#include <Arduino.h>
#include <vector>
#include "UI.h"
#include "RespiratorySensors.h"
#include "Evaluaciones.h"
#include "Reloj.h"
#include "BTPSSensor.h"

// Calibración guiada: PIM (si el equipo tiene el sensor) + espirometría, y guardado en la SD.
// Una espirometría que no pasa la validación se guarda igual (con su código de error) y se puede repetir.
// Si se cancela, no se guarda nada.
class FlujoCalibracion {
public:
  FlujoCalibracion(BronquitoUI &ui, RespiratorySensors &sensoresResp, Evaluaciones &evaluaciones, Reloj &reloj,
                   BTPSSensor &btps);

  // Recorrido completo. Al volver, la pantalla queda para que el .ino dibuje el menú de calibración
  void ejecutar();

private:
  BronquitoUI &ui;
  RespiratorySensors &sensoresResp;
  Evaluaciones &evaluaciones;
  Reloj &reloj;
  BTPSSensor &btps;

  // Captura una maniobra (con la pantalla de progreso). false si se canceló.
  // La captura está pegada a la pantalla (tara después de dibujar, muestras salteadas tras escribir),
  // por eso vive acá y no en RespiratorySensors.
  bool capturarManiobra(bool esPim, uint32_t duracionMs, std::vector<MuestraEspiro> &salida, EventoEspiratorio &ev,
                        float factorBTPS = 1.0f);
  void calibracionCancelada();
  // Lee el DHT22 (antes de medir: la lectura frena el programa unos ms) y calcula el factor BTPS
  CondicionesAmbiente leerAmbiente();
  // Guarda una espirometría que no validó (json con el código de error + csv completo) como
  // <calibId>_ESP_int<intento>_E<código>, junto a la PIM y la espirometría válida de la misma calibración
  void guardarFallida(const std::vector<MuestraEspiro> &esp, const EventoEspiratorio &ev, const CondicionesAmbiente &amb,
                      const String &calibId, int intento);
};

#endif
