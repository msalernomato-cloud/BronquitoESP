#ifndef RELOJ_H
#define RELOJ_H

#include <Arduino.h>
#include <Wire.h>

// =============================================================================
// RELOJ DEL EQUIPO
// -----------------------------------------------------------------------------
// Junta en un solo lugar todo lo que tiene que ver con la hora:
//   1. El RTC DS3231 (I2C, dirección 0x68): guarda la hora aunque el equipo
//      esté apagado, gracias a su pila.
//   2. La hora que manda la web al sincronizar, que queda en el reloj interno
//      del ESP32 (se pierde al apagar).
//
// Para guardar una medición se usa la del RTC si es confiable; si no, la de la
// web. Cada vez que la web manda la hora, también se corrige el RTC.
//
// El DS3231 guarda la hora LOCAL que se le cargue: no sabe de zonas horarias.
// No usa librerías externas, solo Wire.
// =============================================================================

// Fecha y hora local (24 h)
struct FechaHora {
  uint16_t anio;      // 2000 - 2099
  uint8_t  mes;       // 1 - 12
  uint8_t  dia;       // 1 - 31
  uint8_t  hora;      // 0 - 23
  uint8_t  minuto;    // 0 - 59
  uint8_t  segundo;   // 0 - 59
};

class Reloj {
public:
  explicit Reloj(TwoWire &bus = Wire);

  // ---- Uso normal ----------------------------------------------------------

  // Arranca el bus I2C en esos pines. Devuelve true si el RTC responde.
  bool iniciar(int pinSda, int pinScl, uint32_t frecuenciaHz = 100000);

  // La hora para guardar una medición: "AAAA-MM-DD HH:MM:SS",
  // o "sin hora" si ni el RTC ni la web tienen una hora válida.
  String fechaHoraActual();

  // La web mandó la hora al sincronizar (segundos UTC + diferencia horaria en minutos).
  // La guarda en el ESP32 y corrige el RTC. Devuelve true si el RTC quedó actualizado.
  bool ponerHoraDeLaWeb(long segundosUtc, int diferenciaMinutos);

  // ---- Diagnóstico (comando HORA del monitor serie y arranque) -------------

  bool rtcResponde();                 // el chip contesta en el bus I2C
  bool rtcPerdioLaHora();             // se quedó sin pila: la hora no sirve (también true si no responde)
  bool leerTemperaturaDelChip(float &grados);   // la del chip, no la del ambiente (±0,25 °C)
  void imprimirEstado();              // resumen por el monitor serie
  // Busca qué dispositivos contestan en el bus, con los pines como están y con SDA/SCL cruzados.
  // Al terminar deja el bus como lo usa el equipo (comando I2C del monitor serie)
  void escanearBus();

private:
  TwoWire *bus;
  int pinSda = -1, pinScl = -1;
  uint32_t frecuencia = 100000;
  int buscarDispositivos(int sda, int scl);   // imprime las direcciones que contestan; devuelve cuántas
  int diferenciaMinutos = 0;          // zona horaria que mandó la web (ej. -180 para Argentina)

  // Hora del RTC
  bool horaDelRtc(FechaHora &salida);           // true si es confiable
  bool cargarHoraEnRtc(const FechaHora &f);     // escribe la hora y borra la bandera de "perdió la hora"

  // Hora de la web (reloj interno del ESP32)
  bool hayHoraDeLaWeb() const;
  bool horaDeLaWeb(FechaHora &salida) const;

  // Ayudas
  bool leerRegistros(uint8_t registro, uint8_t *destino, uint8_t cantidad);
  bool escribirRegistros(uint8_t registro, const uint8_t *origen, uint8_t cantidad);
  static bool fechaRazonable(const FechaHora &f);
  static String aTexto(const FechaHora &f);
};

#endif
