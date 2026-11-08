#ifndef RELOJ_H
#define RELOJ_H

#include <Arduino.h>
#include "BronquitoRTC.h"
#include "BronquitoNet.h"

// Hora del equipo: la del RTC si es válida; si no, la que trajo la web al sincronizar.
class Reloj {
public:
  Reloj(BronquitoRTC &rtc, BronquitoNetwork &net) : rtc(rtc), net(net) {}

  // "AAAA-MM-DD HH:MM:SS", o "sin hora" si ninguna de las dos es válida
  String fechaHoraActual() {
    String hora = rtc.fechaHora();
    if (hora.length() > 0) return hora;
    return net.fechaHoraLocal();
  }

private:
  BronquitoRTC &rtc;
  BronquitoNetwork &net;
};

#endif
