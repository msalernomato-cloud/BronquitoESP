#ifndef FLUJO_CONFIGURACION_H
#define FLUJO_CONFIGURACION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "UI.h"
#include "TarjetaSD.h"
#include "BronquitoNet.h"
#include "Reloj.h"

// Opción "Configuración" del menú: muestra la frecuencia de entrenamiento que indicó el médico y el
// nivel de resistencia (con su vencimiento) que está guardado en /CONFIGURACION.JSON.
// Si hay conexión con la computadora, primero baja la última de la web y la guarda en la tarjeta.
class FlujoConfiguracion {
public:
  FlujoConfiguracion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, Reloj &reloj);

  void ejecutar();   // vuelve cuando el usuario sale de la pantalla

private:
  BronquitoUI &ui;
  TarjetaSD &tarjeta;
  BronquitoNetwork &net;
  Reloj &reloj;

  bool actualizarFrecuencia();   // true si bajó una frecuencia nueva de la web
};

#endif
