#include "Reloj.h"
#include <sys/time.h>
#include <time.h>

// ---- Registros del DS3231 ---------------------------------------------------
static const uint8_t DIRECCION_RTC = 0x68;
static const uint8_t REG_HORA      = 0x00;   // 0x00..0x06: seg, min, hora, día de semana, día, mes, año
static const uint8_t REG_ESTADO    = 0x0F;   // bit 7 = OSF (el oscilador se detuvo: se perdió la hora)
static const uint8_t REG_TEMP      = 0x11;   // entero con signo + cuartos de grado
static const uint8_t BIT_PERDIO    = 0x80;

static const uint16_t ANIO_MINIMO  = 2026;         // una fecha anterior es la de un reloj sin poner en hora
static const time_t   EPOCH_MINIMO = 1767225600;   // 01/01/2026 00:00 UTC

static uint8_t bcdADecimal(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t decimalABcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// 1 - 7, domingo = 1 (el chip solo lo usa como contador). Algoritmo de Sakamoto
static uint8_t diaDeLaSemana(uint16_t anio, uint8_t mes, uint8_t dia) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int a = anio - (mes < 3 ? 1 : 0);
  return (a + a / 4 - a / 100 + a / 400 + t[mes - 1] + dia) % 7 + 1;
}

Reloj::Reloj(TwoWire &bus) : bus(&bus) {}

// =============================================================================
// USO NORMAL
// =============================================================================
bool Reloj::iniciar(int sda, int scl, uint32_t frecuenciaHz) {
  pinSda = sda;
  pinScl = scl;
  frecuencia = frecuenciaHz;
  bus->begin(pinSda, pinScl, frecuencia);
  return rtcResponde();
}

String Reloj::fechaHoraActual() {
  FechaHora f;
  if (horaDelRtc(f))  return aTexto(f);
  if (horaDeLaWeb(f)) return aTexto(f);
  return "sin hora";
}

bool Reloj::ponerHoraDeLaWeb(long segundosUtc, int diferencia) {
  if (segundosUtc < (long)EPOCH_MINIMO) return false;   // la web no mandó una hora válida

  struct timeval tv = { (time_t)segundosUtc, 0 };
  settimeofday(&tv, nullptr);
  diferenciaMinutos = diferencia;

  FechaHora f;
  return horaDeLaWeb(f) && cargarHoraEnRtc(f);
}

// =============================================================================
// DIAGNÓSTICO
// =============================================================================
bool Reloj::rtcResponde() {
  bus->beginTransmission(DIRECCION_RTC);
  return bus->endTransmission() == 0;
}

bool Reloj::rtcPerdioLaHora() {
  uint8_t estado;
  if (!leerRegistros(REG_ESTADO, &estado, 1)) return true;
  return (estado & BIT_PERDIO) != 0;
}

bool Reloj::leerTemperaturaDelChip(float &grados) {
  uint8_t t[2];
  if (!leerRegistros(REG_TEMP, t, 2)) return false;
  grados = (int8_t)t[0] + (t[1] >> 6) * 0.25f;
  return true;
}

void Reloj::imprimirEstado() {
  FechaHora f;
  Serial.println("[RELOJ] Hora para las mediciones: " + fechaHoraActual());
  Serial.println("[RELOJ] RTC: " + String(horaDelRtc(f) ? aTexto(f) : "sin hora válida") +
                 " | responde: " + (rtcResponde() ? "sí" : "no") +
                 " | perdió la hora: " + (rtcPerdioLaHora() ? "sí" : "no"));
  Serial.println("[RELOJ] Web: " + String(horaDeLaWeb(f) ? aTexto(f) : "todavía no sincronizó"));
  float grados;
  if (leerTemperaturaDelChip(grados)) Serial.println("[RELOJ] Temperatura del chip: " + String(grados, 2) + " C");
}

int Reloj::buscarDispositivos(int sda, int scl) {
  bus->end();                                          // si el bus ya estaba iniciado, begin() no cambia los pines
  bus->begin(sda, scl, 100000);
  int encontrados = 0;
  for (uint8_t dir = 1; dir < 127; dir++) {
    bus->beginTransmission(dir);
    if (bus->endTransmission() == 0) {
      const char *que = dir == 0x68 ? " (DS3231: el RTC)" : dir == 0x57 ? " (EEPROM del módulo)" : "";
      Serial.printf("[I2C]   contesta 0x%02X%s\n", dir, que);
      encontrados++;
    }
  }
  if (encontrados == 0) Serial.println("[I2C]   no contesta nadie");
  return encontrados;
}

void Reloj::escanearBus() {
  if (pinSda < 0) { Serial.println("[I2C] El reloj no se inició"); return; }
  Serial.printf("[I2C] Buscando con SDA = GPIO%d y SCL = GPIO%d (como está en el código):\n", pinSda, pinScl);
  int bien = buscarDispositivos(pinSda, pinScl);
  Serial.printf("[I2C] Buscando con los pines cruzados (SDA = GPIO%d y SCL = GPIO%d):\n", pinScl, pinSda);
  int cruzados = buscarDispositivos(pinScl, pinSda);

  if (bien > 0)          Serial.println("[I2C] El bus funciona con los pines del código");
  else if (cruzados > 0) Serial.println("[I2C] SDA y SCL están CRUZADOS: dá vuelta el módulo o cambiá los pines en el .ino");
  else                   Serial.println("[I2C] No contesta nada: revisá continuidad entre J10 y el ESP32, o probá otro módulo");

  bus->end();                                          // vuelve a dejar el bus como lo usa el equipo
  bus->begin(pinSda, pinScl, frecuencia);
}

// =============================================================================
// HORA DEL RTC
// =============================================================================
bool Reloj::horaDelRtc(FechaHora &f) {
  if (rtcPerdioLaHora()) return false;

  uint8_t r[7];
  if (!leerRegistros(REG_HORA, r, 7)) return false;   // los 7 bytes juntos: el chip los congela en la lectura

  f.segundo = bcdADecimal(r[0] & 0x7F);
  f.minuto  = bcdADecimal(r[1] & 0x7F);
  if (r[2] & 0x40) {                                   // modo 12 h (por si lo dejó así otro programa)
    uint8_t h = bcdADecimal(r[2] & 0x1F);
    bool pm = (r[2] & 0x20) != 0;
    if (h == 12) h = pm ? 12 : 0;
    else if (pm) h += 12;
    f.hora = h;
  } else {
    f.hora = bcdADecimal(r[2] & 0x3F);
  }
  f.dia  = bcdADecimal(r[4] & 0x3F);
  f.mes  = bcdADecimal(r[5] & 0x1F);                   // el bit 7 es el siglo: se ignora
  f.anio = 2000 + bcdADecimal(r[6]);

  return fechaRazonable(f);
}

bool Reloj::cargarHoraEnRtc(const FechaHora &f) {
  if (!fechaRazonable(f)) return false;

  uint8_t r[7] = {
    decimalABcd(f.segundo),
    decimalABcd(f.minuto),
    decimalABcd(f.hora),                               // bit 6 en 0 = formato de 24 h
    diaDeLaSemana(f.anio, f.mes, f.dia),
    decimalABcd(f.dia),
    decimalABcd(f.mes),
    decimalABcd(f.anio - 2000)
  };
  if (!escribirRegistros(REG_HORA, r, 7)) return false;

  // Con la hora ya cargada se borra la bandera, así rtcPerdioLaHora() vuelve a dar false
  uint8_t estado;
  if (!leerRegistros(REG_ESTADO, &estado, 1)) return false;
  estado &= ~BIT_PERDIO;
  return escribirRegistros(REG_ESTADO, &estado, 1);
}

// =============================================================================
// HORA DE LA WEB (reloj interno del ESP32)
// =============================================================================
bool Reloj::hayHoraDeLaWeb() const {
  return time(nullptr) >= EPOCH_MINIMO;
}

bool Reloj::horaDeLaWeb(FechaHora &f) const {
  if (!hayHoraDeLaWeb()) return false;
  time_t t = time(nullptr) + (time_t)diferenciaMinutos * 60;
  struct tm local;
  gmtime_r(&t, &local);
  f.anio    = local.tm_year + 1900;
  f.mes     = local.tm_mon + 1;
  f.dia     = local.tm_mday;
  f.hora    = local.tm_hour;
  f.minuto  = local.tm_min;
  f.segundo = local.tm_sec;
  return true;
}

// =============================================================================
// AYUDAS
// =============================================================================
bool Reloj::leerRegistros(uint8_t registro, uint8_t *destino, uint8_t cantidad) {
  bus->beginTransmission(DIRECCION_RTC);
  bus->write(registro);
  if (bus->endTransmission(false) != 0) return false;   // reinicio sin liberar el bus
  if (bus->requestFrom((int)DIRECCION_RTC, (int)cantidad) != cantidad) return false;
  for (uint8_t i = 0; i < cantidad; i++) destino[i] = bus->read();
  return true;
}

bool Reloj::escribirRegistros(uint8_t registro, const uint8_t *origen, uint8_t cantidad) {
  bus->beginTransmission(DIRECCION_RTC);
  bus->write(registro);
  bus->write(origen, cantidad);
  return bus->endTransmission() == 0;
}

bool Reloj::fechaRazonable(const FechaHora &f) {
  return f.anio >= ANIO_MINIMO && f.anio <= 2099 &&
         f.mes >= 1 && f.mes <= 12 &&
         f.dia >= 1 && f.dia <= 31 &&
         f.hora < 24 && f.minuto < 60 && f.segundo < 60;
}

String Reloj::aTexto(const FechaHora &f) {
  char texto[24];
  snprintf(texto, sizeof(texto), "%04u-%02u-%02u %02u:%02u:%02u",
           (unsigned)f.anio, (unsigned)f.mes, (unsigned)f.dia,
           (unsigned)f.hora, (unsigned)f.minuto, (unsigned)f.segundo);
  return String(texto);
}
