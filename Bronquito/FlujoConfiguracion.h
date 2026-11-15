#ifndef FLUJO_CONFIGURACION_H
#define FLUJO_CONFIGURACION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "UI.h"
#include "TarjetaSD.h"
#include "BronquitoNet.h"
#include "Reloj.h"

// Opción "Configuración" del menú: muestra la frecuencia de entrenamiento que indicó el médico y el
// nivel de resistencia (manual del médico, o el automático con su vencimiento) de /CONFIGURACION.JSON.
// Si hay conexión con la computadora, primero baja la última de la web y la guarda en la tarjeta.
class FlujoConfiguracion {
public:
  FlujoConfiguracion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, Reloj &reloj);

  void ejecutar();   // vuelve cuando el usuario sale de la pantalla

  // Si hay conexión, baja de la web la frecuencia, la resistencia que fijó el médico y los predichos,
  // y los guarda en /CONFIGURACION.JSON. true si bajó una frecuencia nueva. Se llama al entrar a
  // Configuración y antes de cada entrenamiento.
  bool actualizarDesdeWeb();

private:
  BronquitoUI &ui;
  TarjetaSD &tarjeta;
  BronquitoNetwork &net;
  Reloj &reloj;

};

#endif
