#ifndef FLUJO_ENTRENAMIENTO_H
#define FLUJO_ENTRENAMIENTO_H

#include <Arduino.h>
#include "UI.h"
#include "RespiratorySensors.h"
#include "TarjetaSD.h"
#include "Reloj.h"
#include "BTPSSensor.h"
#include "ValvulaActiva.h"

// Entrenamiento muscular, con el mismo esquema que la calibración:
//   instrucciones -> temperatura y humedad (DHT22, factor BTPS) -> válvula al DC del nivel (PWM prendido)
//   -> pantalla -> tara -> "Ya puede empezar"
//   -> captura continua (flujo GPIO36 + presión GPIO39) a la SD; en vivo SOLO se cuentan los ciclos.
//      Termina sola al llegar a ciclosObjetivo (30), si no respira durante 10 s, o con "Detener"
//   -> válvula abierta y PWM apagado -> "¿Guardar?" No = se borra.
//      Sí = "Procesando y guardando": se relee la señal de la SD, se calculan las features
//      (AnalisisEntrenamiento, con la K de cada fase y BTPS) y se escriben en el .json junto al .csv.
// Al volver, el .ino dibuja el menú principal.
class FlujoEntrenamiento {
public:
  static const int CICLOS_OBJETIVO_DEFECTO = 30;

  FlujoEntrenamiento(BronquitoUI &ui, RespiratorySensors &sensoresResp, ValvulaActiva &valvula, TarjetaSD &tarjeta,
                     Reloj &reloj, BTPSSensor &btps);

  void ejecutar();
  void setCiclosObjetivo(int n) { if (n > 0) ciclosObjetivo = n; }

private:
  int ciclosObjetivo = CICLOS_OBJETIVO_DEFECTO;
  BronquitoUI &ui;
  RespiratorySensors &sensoresResp;
  ValvulaActiva &valvula;
  TarjetaSD &tarjeta;
  Reloj &reloj;
  BTPSSensor &btps;

  void mostrarError(const String &titulo, const String &mensaje);
  CondicionesAmbiente leerAmbiente();   // igual que FlujoCalibracion::leerAmbiente()
};

#endif
