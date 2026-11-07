#ifndef FLUJO_CALIBRACION_H
#define FLUJO_CALIBRACION_H

#include <Arduino.h>
#include <vector>
#include "UI.h"
#include "RespiratorySensors.h"
#include "AnalisisEspiro.h"
#include "AnalisisPIM.h"
#include "TarjetaSD.h"
#include "Reloj.h"
#include "BTPSSensor.h"

// Calibración guiada: PIM (si el equipo tiene el sensor) + espirometría, y guardado en la SD.
// Una espirometría que no pasa la validación se guarda igual (con su código de error) y se puede repetir.
// Si se cancela, no se guarda nada.
class FlujoCalibracion {
public:
  FlujoCalibracion(BronquitoUI &ui, RespiratorySensors &sensoresResp, TarjetaSD &tarjeta, Reloj &reloj,
                   BTPSSensor &btps);

  // Recorrido completo. Al volver, la pantalla queda para que el .ino dibuje el menú de calibración
  void ejecutar();

private:
  BronquitoUI &ui;
  RespiratorySensors &sensoresResp;
  TarjetaSD &tarjeta;
  Reloj &reloj;
  BTPSSensor &btps;

  // Una maniobra: pantalla, tara, captura (la hace el sensor) y, si es espirometría, su análisis.
  // CAPTURA_CANCELADA si el usuario tocó "Cancelar"; CAPTURA_SIN_MEMORIA si no se pudo medir.
  ResultadoCaptura capturarManiobra(bool esPim, uint32_t duracionMs, std::vector<MuestraEspiro> &salida, EventoEspiratorio &ev,
                        float factorBTPS = 1.0f);
  void calibracionCancelada(const String &calibId);   // el usuario canceló: se borra toda la calibración
  void calibracionInterrumpida();                     // error al medir: se conserva lo ya guardado
  // Lee el DHT22 (antes de medir: la lectura frena el programa unos ms) y calcula el factor BTPS
  CondicionesAmbiente leerAmbiente();
  // Guarda una espirometría que no validó (json con el código de error + csv completo) como
  // <calibId>_ESP_int<intento>_E<código>, junto a la PIM y la espirometría válida de la misma calibración
  void guardarFallida(const std::vector<MuestraEspiro> &esp, const EventoEspiratorio &ev, const CondicionesAmbiente &amb,
                      const String &calibId, int intento);
};

#endif
