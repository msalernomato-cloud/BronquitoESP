#include "BronquitoRTC.h"

// Mapa de registros del DS3231
static const uint8_t DIRECCION_I2C = 0x68;
static const uint8_t REG_SEGUNDOS  = 0x00;   // 0x00..0x06: seg, min, hora, día de semana, día, mes, año
static const uint8_t REG_ESTADO    = 0x0F;   // bit 7 = OSF (oscilador detenido)
static const uint8_t REG_TEMP      = 0x11;   // MSB entero con signo, LSB: bits 7-6 = fracción en cuartos
static const uint8_t BIT_OSF       = 0x80;

static uint8_t bcdABin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t binABcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

BronquitoRTC::BronquitoRTC(TwoWire &bus) : _bus(&bus) {}

bool BronquitoRTC::begin(int sda, int scl, uint32_t frecuenciaHz) {
  if (sda >= 0 && scl >= 0) _bus->begin(sda, scl, frecuenciaHz);
  return disponible();
}

bool BronquitoRTC::disponible() {
  _bus->beginTransmission(DIRECCION_I2C);
  return _bus->endTransmission() == 0;
}

bool BronquitoRTC::leerRegistros(uint8_t reg, uint8_t *destino, uint8_t cantidad) {
  _bus->beginTransmission(DIRECCION_I2C);
  _bus->write(reg);
  if (_bus->endTransmission(false) != 0) return false;            // reinicio sin liberar el bus
  if (_bus->requestFrom((int)DIRECCION_I2C, (int)cantidad) != cantidad) return false;
  for (uint8_t i = 0; i < cantidad; i++) destino[i] = _bus->read();
  return true;
}

bool BronquitoRTC::escribirRegistros(uint8_t reg, const uint8_t *origen, uint8_t cantidad) {
  _bus->beginTransmission(DIRECCION_I2C);
  _bus->write(reg);
  _bus->write(origen, cantidad);
  return _bus->endTransmission() == 0;
}

bool BronquitoRTC::rangoValido(const FechaHora &f) {
  return f.anio >= 2000 && f.anio <= 2099 &&
         f.mes >= 1 && f.mes <= 12 &&
         f.dia >= 1 && f.dia <= 31 &&
         f.hora < 24 && f.minuto < 60 && f.segundo < 60;
}

// 1 - 7 (el chip solo lo necesita como contador, no se usa para nada más). Algoritmo de Sakamoto
uint8_t BronquitoRTC::diaDeLaSemana(uint16_t anio, uint8_t mes, uint8_t dia) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = anio - (mes < 3 ? 1 : 0);
  int d = (y + y / 4 - y / 100 + y / 400 + t[mes - 1] + dia) % 7;   // 0 = domingo
  return d + 1;
}

bool BronquitoRTC::perdioEnergia() {
  uint8_t estado;
  if (!leerRegistros(REG_ESTADO, &estado, 1)) return true;
  return (estado & BIT_OSF) != 0;
}

bool BronquitoRTC::leer(FechaHora &f) {
  uint8_t r[7];
  if (!leerRegistros(REG_SEGUNDOS, r, 7)) return false;   // lectura de los 7 bytes de una vez: el chip los congela juntos

  f.segundo = bcdABin(r[0] & 0x7F);
  f.minuto  = bcdABin(r[1] & 0x7F);

  uint8_t h = r[2];
  if (h & 0x40) {                                          // modo 12 h (por si lo dejó así otro programa)
    uint8_t hh = bcdABin(h & 0x1F);
    bool pm = (h & 0x20) != 0;
    if (hh == 12) hh = pm ? 12 : 0;
    else if (pm)  hh += 12;
    f.hora = hh;
  } else {
    f.hora = bcdABin(h & 0x3F);
  }

  f.dia  = bcdABin(r[4] & 0x3F);
  f.mes  = bcdABin(r[5] & 0x1F);                           // bit 7 es el siglo, se ignora
  f.anio = 2000 + bcdABin(r[6]);

  return rangoValido(f);
}

bool BronquitoRTC::horaValida(FechaHora *salida) {
  FechaHora f;
  if (perdioEnergia()) return false;
  if (!leer(f) || f.anio < 2024) return false;
  if (salida) *salida = f;
  return true;
}

bool BronquitoRTC::ajustar(const FechaHora &f) {
  if (!rangoValido(f)) return false;

  uint8_t r[7] = {
    binABcd(f.segundo),
    binABcd(f.minuto),
    binABcd(f.hora),                                        // bit 6 en 0 = formato de 24 h
    diaDeLaSemana(f.anio, f.mes, f.dia),
    binABcd(f.dia),
    binABcd(f.mes),
    binABcd(f.anio - 2000)
  };
  if (!escribirRegistros(REG_SEGUNDOS, r, 7)) return false;

  // Con la hora ya cargada se borra OSF, para que perdioEnergia() vuelva a ser false
  uint8_t estado;
  if (!leerRegistros(REG_ESTADO, &estado, 1)) return false;
  estado &= ~BIT_OSF;
  return escribirRegistros(REG_ESTADO, &estado, 1);
}

bool BronquitoRTC::ajustarDesdeTexto(const String &texto) {
  int a, mo, d, h, mi, s;
  // %*c salta un carácter separador: acepta tanto "2026-10-30 15:00:00" como "2026-10-30T15:00:00"
  if (sscanf(texto.c_str(), "%d-%d-%d%*c%d:%d:%d", &a, &mo, &d, &h, &mi, &s) != 6) return false;

  FechaHora f;
  f.anio = a; f.mes = mo; f.dia = d; f.hora = h; f.minuto = mi; f.segundo = s;
  if (a < 2024) return false;                               // el "hora local" sin sincronizar no debe pisar al chip
  return ajustar(f);
}

String BronquitoRTC::fechaHora() {
  FechaHora f;
  if (!horaValida(&f)) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", f.anio, f.mes, f.dia, f.hora, f.minuto, f.segundo);
  return String(buf);
}

bool BronquitoRTC::leerTemperatura(float &grados) {
  uint8_t t[2];
  if (!leerRegistros(REG_TEMP, t, 2)) return false;
  grados = (int8_t)t[0] + (t[1] >> 6) * 0.25f;
  return true;
}