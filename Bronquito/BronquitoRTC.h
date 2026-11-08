#ifndef BRONQUITO_RTC_H
#define BRONQUITO_RTC_H

#include <Arduino.h>
#include <Wire.h>

// Fecha y hora local tal como la guarda el DS3231 (24 h, sin zona horaria)
struct FechaHora {
  uint16_t anio;      // 2000 - 2099
  uint8_t  mes;       // 1 - 12
  uint8_t  dia;       // 1 - 31
  uint8_t  hora;      // 0 - 23
  uint8_t  minuto;    // 0 - 59
  uint8_t  segundo;   // 0 - 59
};

// Reloj de tiempo real DS3231 (I2C, dirección 0x68). No usa librerías externas, solo Wire.
//
// El chip guarda la hora LOCAL que se le cargue: no maneja zonas horarias ni horario de verano.
// Con la pila puesta mantiene la hora aunque el equipo esté apagado.
class BronquitoRTC {
public:
  explicit BronquitoRTC(TwoWire &bus = Wire);

  // Con sda y scl >= 0 inicia el bus I2C en esos pines. Con -1 asume que otro módulo
  // (por ejemplo el sensor ambiental) ya hizo Wire.begin(). Devuelve true si el DS3231 responde.
  bool begin(int sda = -1, int scl = -1, uint32_t frecuenciaHz = 400000);

  bool disponible();       // el chip responde en el bus
  bool perdioEnergia();    // bandera OSF: el oscilador se detuvo, la hora no es confiable (también true si no responde)

  // true si no hubo pérdida de energía y la fecha es razonable (año >= 2024).
  // Si se pasa `salida`, queda cargada con la hora leída en la misma lectura.
  bool horaValida(FechaHora *salida = nullptr);

  bool leer(FechaHora &f);                         // lectura cruda; false si falla el bus o los valores son absurdos
  bool ajustar(const FechaHora &f);                // carga la hora y borra la bandera OSF
  bool ajustarDesdeTexto(const String &texto);     // "AAAA-MM-DD HH:MM:SS", el mismo formato que fecha_hora del .json

  // "AAAA-MM-DD HH:MM:SS", o "" si la hora no es válida
  String fechaHora();

  // Temperatura interna del chip (resolución 0,25 °C, se actualiza cada 64 s).
  // Es la del componente, no la del ambiente: puede leer unos grados de más por el calor de la placa.
  bool leerTemperatura(float &grados);

private:
  TwoWire *_bus;

  bool leerRegistros(uint8_t reg, uint8_t *destino, uint8_t cantidad);
  bool escribirRegistros(uint8_t reg, const uint8_t *origen, uint8_t cantidad);
  static bool rangoValido(const FechaHora &f);
  static uint8_t diaDeLaSemana(uint16_t anio, uint8_t mes, uint8_t dia);
};

#endif