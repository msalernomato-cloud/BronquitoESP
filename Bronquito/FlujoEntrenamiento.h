#ifndef FLUJO_ENTRENAMIENTO_H
#define FLUJO_ENTRENAMIENTO_H

#include <Arduino.h>
#include "UI.h"
#include "RespiratorySensors.h"
#include "ValvulaActiva.h"
#include "TarjetaSD.h"
#include "Reloj.h"

// Entrenamiento muscular, con el mismo esquema que la calibración:
//   instrucciones -> válvula al DC de entrenamiento -> pantalla -> tara -> "Ya puede empezar"
//   -> captura continua (flujo GPIO36 + presión GPIO39) a la SD; en vivo SOLO se cuentan los ciclos.
//      Termina sola al llegar a ciclosObjetivo (30), si no respira durante 10 s, o con "Detener"
//   -> válvula abierta -> "¿Guardar?" No = se borra.
//      Sí = "Procesando y guardando": se relee la señal de la SD, se calculan las features
//      (AnalisisEntrenamiento) y se escriben en el .json junto al .csv.
// Al volver, el .ino dibuja el menú principal.
class FlujoEntrenamiento {
public:
  static const int CICLOS_OBJETIVO_DEFECTO = 30;

  FlujoEntrenamiento(BronquitoUI &ui, RespiratorySensors &sensoresResp, ValvulaActiva &valvula,
                     TarjetaSD &tarjeta, Reloj &reloj);

  void ejecutar();
  void setCiclosObjetivo(int n) { if (n > 0) ciclosObjetivo = n; }

private:
  int ciclosObjetivo = CICLOS_OBJETIVO_DEFECTO;
  BronquitoUI &ui;
  RespiratorySensors &sensoresResp;
  ValvulaActiva &valvula;
  TarjetaSD &tarjeta;
  Reloj &reloj;

  void mostrarError(const String &titulo, const String &mensaje);
};

#endif
