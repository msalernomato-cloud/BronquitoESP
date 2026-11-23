// =====================================================================================
// CalibracionPIM.ino  -  Calibración estática del canal de presión (PIM) de Bronquito
// -------------------------------------------------------------------------------------
// Banco: perita -> pieza en Y -> manómetro del tensiómetro (mmHg)
//                            \-> boquilla de 6 mm del PSA071040 (sensor manométrico, ±40 kPa)
// Alimentá la placa con la MISMA fuente de 5 V que vas a usar en la demo: la salida del
// sensor es proporcional a su tensión de alimentación.
// Se mide la SALIDA de la placa (la tensión que llega al GPIO39), así la calibración
// incluye sensor + INA128 + filtro + ADC de la ESP32.
//
// Monitor serie a 115200, con "Nueva línea". Comandos:
//   t        tara: mide el cero con todo abierto a la atmósfera (2 s)
//   <número> captura un punto: escribí lo que marca el manómetro en mmHg (ej: 40)
//            y Enter. Mide 3 s y lo guarda.
//   v        modo vivo on/off: muestra tensión y presión 5 veces por segundo
//            (sirve para ver si la presión está quieta antes de capturar)
//   b        borra el último punto
//   l        lista los puntos guardados
//   f        ajusta la recta y sugiere la K_PIM nueva
//   r        borra todos los puntos de la memoria (no la tara ni la microSD)
//   c        carga a la memoria los puntos guardados en la microSD (para reajustar
//            después de reiniciar)
//
// En la microSD (los mismos pines que usa Bronquito) guarda:
//   /CALIBRACION_PIM.CSV          cada punto apenas se captura, con su número de serie
//   /CALIBRACION_PIM.JSON         el último ajuste (se reemplaza con cada 'f')
//   /CALIBRACION_PIM_AJUSTES.CSV  todos los ajustes hechos, uno por línea
// Cada tara ('t') empieza una serie nueva.
// =====================================================================================

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

const int   PIN_PIM          = 39;          // salida de la placa de sensores (MAN2 / J7)
const float K_PIM_FIRMWARE   = 421.35f;     // cmH2O/V, la del firmware (teórica)
const float CMH2O_POR_MMHG   = 1.35951f;

const int   VENTANA          = 20;          // 20 muestras a 1 kHz = 20 ms (rechaza 50 Hz)
const int   SEG_CAPTURA      = 3;           // segundos por punto
const int   SEG_TARA         = 2;
const int   MAX_PUNTOS       = 80;

// microSD: mismos pines que en Bronquito (bus VSPI compartido con la pantalla)
const int   SD_CS = 19, SD_SCK = 22, SD_MISO = 23, SD_MOSI = 21;
const int   TFT_CS = 15;                    // se deja en alto para que la pantalla no moleste
const char *ARCH_PUNTOS   = "/CALIBRACION_PIM.CSV";
const char *ARCH_AJUSTE   = "/CALIBRACION_PIM.JSON";
const char *ARCH_AJUSTES  = "/CALIBRACION_PIM_AJUSTES.CSV";

struct Punto {
  int   serie;      // número de serie (cada tara empieza una)
  float mmHg;       // lectura del manómetro
  float cmH2O;      // la misma, en cmH2O
  float v;          // tensión media en el GPIO39 (V)
  float dv;         // v - cero (V)
  float sd_mV;      // desvío de las ventanas de 20 ms (mV)
  float deriva;     // pendiente durante la captura (mmHg/s, con la K del firmware): si es grande, hay fuga
};

Punto puntos[MAX_PUNTOS];
int   nPuntos = 0;
float vCero = NAN;
bool  vivo = false;
unsigned long ultimoVivo = 0;

SPIClass busSD(VSPI);
bool  sdOk = false;
int   serie = 1;               // serie actual
bool  serieConPuntos = false;  // si ya se capturó algo en esta serie

// ----------------------------------------------------------------------------- microSD
bool iniciarSD() {
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  busSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, busSD);
  if (!sdOk) { Serial.println(F("[SD] No se pudo montar la microSD: los datos NO se van a guardar.")); return false; }
  // Si ya hay puntos de antes, la serie siguiente continúa la numeración
  int maxSerie = 0;
  File f = SD.open(ARCH_PUNTOS, FILE_READ);
  if (f) {
    while (f.available()) {
      String linea = f.readStringUntil('\n');
      if (linea.length() > 0 && isDigit(linea.charAt(0))) maxSerie = max(maxSerie, (int)linea.toInt());
    }
    f.close();
  } else {
    f = SD.open(ARCH_PUNTOS, FILE_WRITE);
    if (f) { f.println("serie,mmHg,cmH2O_ref,V,dV,V_cero,ruido_mV,deriva_mmHg_s,ms_desde_inicio"); f.close(); }
  }
  serie = maxSerie + 1;
  Serial.printf("[SD] microSD lista. Esta sesión empieza en la serie %d.\n", serie);
  return true;
}

void guardarPunto(const Punto &p) {
  if (!sdOk) return;
  File f = SD.open(ARCH_PUNTOS, FILE_APPEND);
  if (!f) { Serial.println(F("[SD] No se pudo escribir el punto.")); return; }
  f.printf("%d,%.1f,%.3f,%.5f,%.5f,%.5f,%.2f,%.3f,%lu\n", p.serie, p.mmHg, p.cmH2O, p.v, p.dv, vCero,
           p.sd_mV, p.deriva, millis());
  f.close();
  Serial.println(F("[SD] Punto guardado."));
}

// Saca de la microSD la última línea de datos (lo que hace 'b')
void borrarUltimoDeSD() {
  if (!sdOk) return;
  File f = SD.open(ARCH_PUNTOS, FILE_READ);
  if (!f) return;
  String todo = "";
  while (f.available()) todo += (char)f.read();
  f.close();
  todo.trim();
  int corte = todo.lastIndexOf('\n');
  if (corte < 0) return;                           // solo queda el encabezado
  todo = todo.substring(0, corte + 1);
  f = SD.open(ARCH_PUNTOS, FILE_WRITE);
  if (!f) { Serial.println(F("[SD] No se pudo borrar el punto de la microSD.")); return; }
  f.print(todo);
  f.close();
  Serial.println(F("[SD] También se borró de la microSD."));
}

// Carga a la memoria los puntos de la microSD (por ejemplo, después de reiniciar)
void cargarPuntos() {
  if (!sdOk) { Serial.println(F("[SD] No hay microSD.")); return; }
  File f = SD.open(ARCH_PUNTOS, FILE_READ);
  if (!f) { Serial.println(F("[SD] No hay puntos guardados.")); return; }
  nPuntos = 0;
  while (f.available() && nPuntos < MAX_PUNTOS) {
    String l = f.readStringUntil('\n');
    if (l.length() == 0 || !isDigit(l.charAt(0))) continue;
    float c[8]; int k = 0, desde = 0;
    for (int i = 0; i <= (int)l.length() && k < 8; i++) {
      if (i == (int)l.length() || l.charAt(i) == ',') { c[k++] = l.substring(desde, i).toFloat(); desde = i + 1; }
    }
    if (k < 8) continue;
    Punto &p = puntos[nPuntos++];
    p.serie = (int)c[0]; p.mmHg = c[1]; p.cmH2O = c[2]; p.v = c[3]; p.dv = c[4];
    p.sd_mV = c[6]; p.deriva = c[7];
  }
  f.close();
  Serial.printf("[SD] Cargados %d puntos. Mandá 'f' para ajustar.\n", nPuntos);
}

// Promedio de una ventana de 20 ms (una lectura por milisegundo)
float leerVentana() {
  uint32_t suma = 0;
  for (int i = 0; i < VENTANA; i++) {
    unsigned long t0 = micros();
    suma += analogReadMilliVolts(PIN_PIM);
    while (micros() - t0 < 1000) {}
  }
  return suma / (1000.0f * VENTANA);
}

// Mide 'segundos' y devuelve media, desvío y pendiente (V/s) de las ventanas
void medir(int segundos, float &media, float &sd_mV, float &pendiente) {
  const int n = segundos * 1000 / VENTANA;
  double s = 0, s2 = 0, st = 0, stt = 0, sty = 0;
  for (int i = 0; i < n; i++) {
    float v = leerVentana();
    double t = i * VENTANA / 1000.0;
    s += v; s2 += (double)v * v; st += t; stt += t * t; sty += t * v;
  }
  media = s / n;
  double var = s2 / n - (double)media * media;
  sd_mV = 1000.0f * sqrt(var > 0 ? var : 0);
  double den = n * stt - st * st;
  pendiente = den > 0 ? (n * sty - st * s) / den : 0;
}

void tara() {
  Serial.println(F("[TARA] Todo abierto a la atmósfera, sin tocar la perita..."));
  float m, sd, p;
  medir(SEG_TARA, m, sd, p);
  vCero = m;
  if (serieConPuntos) { serie++; serieConPuntos = false; }
  Serial.printf("[TARA] Cero = %.4f V  (ruido %.2f mV). Serie %d.\n", vCero, sd, serie);
}

void capturar(float mmHg) {
  if (isnan(vCero)) { Serial.println(F("Primero hacé la tara con 't'.")); return; }
  if (nPuntos >= MAX_PUNTOS) { Serial.println(F("Lista llena: 'f' para ajustar o 'r' para empezar de nuevo.")); return; }
  Serial.printf("[PUNTO] %.1f mmHg: midiendo %d s, no toques nada...\n", mmHg, SEG_CAPTURA);
  float m, sd, pend;
  medir(SEG_CAPTURA, m, sd, pend);
  Punto &p = puntos[nPuntos++];
  p.serie = serie;
  serieConPuntos = true;
  p.mmHg = mmHg;
  p.cmH2O = mmHg * CMH2O_POR_MMHG;
  p.v = m;
  p.dv = m - vCero;
  p.sd_mV = sd;
  p.deriva = pend * K_PIM_FIRMWARE / CMH2O_POR_MMHG;
  float pFw = p.dv * K_PIM_FIRMWARE;
  Serial.printf("[PUNTO %d] ref %.1f mmHg = %.1f cmH2O | V = %.4f | dV = %+.4f V | firmware dice %+.1f cmH2O | ruido %.2f mV | deriva %+.2f mmHg/s\n",
                nPuntos, p.mmHg, p.cmH2O, p.v, p.dv, pFw, p.sd_mV, p.deriva);
  if (fabsf(p.deriva) > 0.5f)
    Serial.println(F("  OJO: la presión se movió durante la captura (¿fuga?). Conviene borrar con 'b' y repetir."));
  guardarPunto(p);
}

void listar() {
  Serial.println(F("n,serie,mmHg,cmH2O_ref,V,dV,cmH2O_firmware,ruido_mV,deriva_mmHg_s"));
  for (int i = 0; i < nPuntos; i++) {
    Punto &p = puntos[i];
    Serial.printf("%d,%d,%.1f,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f\n", i + 1, p.serie, p.mmHg, p.cmH2O, p.v, p.dv,
                  p.dv * K_PIM_FIRMWARE, p.sd_mV, p.deriva);
  }
}

void ajustar() {
  if (nPuntos < 3) { Serial.println(F("Hacen falta al menos 3 puntos.")); return; }
  // Recta P = K * dV + b (mínimos cuadrados) y recta por el origen P = K0 * dV
  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  for (int i = 0; i < nPuntos; i++) {
    double x = puntos[i].dv, y = puntos[i].cmH2O;
    sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
  }
  int n = nPuntos;
  double den = n * sxx - sx * sx;
  if (fabs(den) < 1e-12) { Serial.println(F("Los puntos no tienen tensiones distintas.")); return; }
  double K = (n * sxy - sx * sy) / den;
  double b = (sy - K * sx) / n;
  double K0 = sxy / sxx;
  double ssRes = 0, ssTot = syy - sy * sy / n, maxRes = 0;
  Serial.println(F("\n[AJUSTE] residuos (referencia - recta), en cmH2O:"));
  for (int i = 0; i < n; i++) {
    double r = puntos[i].cmH2O - (K * puntos[i].dv + b);
    ssRes += r * r;
    if (fabs(r) > fabs(maxRes)) maxRes = r;
    Serial.printf("  %2d) %6.1f mmHg  ->  residuo %+.2f cmH2O\n", i + 1, puntos[i].mmHg, r);
  }
  double r2 = ssTot > 0 ? 1 - ssRes / ssTot : 0;
  double see = n > 2 ? sqrt(ssRes / (n - 2)) : 0;
  Serial.printf("[AJUSTE] P[cmH2O] = %.2f * dV[V] %+.2f   (R2 = %.5f, error típico %.2f cmH2O, residuo máx %+.2f)\n",
                K, b, r2, see, maxRes);
  Serial.printf("[AJUSTE] Recta por el origen: K0 = %.2f cmH2O/V\n", K0);
  Serial.printf("[AJUSTE] K del firmware = %.2f  ->  la medida es %.1f%% de la teórica\n",
                K_PIM_FIRMWARE, 100.0 * fabs(K) / K_PIM_FIRMWARE);
  if (K < 0)
    Serial.println(F("[AJUSTE] Pendiente negativa: es lo esperado. La perita da presión positiva y el firmware lee positiva la inspiración (vacío). Usá el valor absoluto."));
  Serial.printf("[AJUSTE] Sugerencia para RespiratorySensors.h:  const float K_PIM = %.2ff;\n\n", fabs(K));

  if (!sdOk) { Serial.println(F("[SD] Sin microSD: el ajuste NO quedó guardado.")); return; }
  int serieMin = puntos[0].serie, serieMax = puntos[0].serie;
  for (int i = 1; i < n; i++) { serieMin = min(serieMin, puntos[i].serie); serieMax = max(serieMax, puntos[i].serie); }
  bool okJson = false, okHist = false;
  File f = SD.open(ARCH_AJUSTE, FILE_WRITE);       // se reemplaza con el último ajuste
  if (f) {
    okJson = true;
    f.printf("{\n  \"K_cmH2O_por_V\": %.3f,\n  \"b_cmH2O\": %.3f,\n  \"K0_cmH2O_por_V\": %.3f,\n"
             "  \"R2\": %.6f,\n  \"error_tipico_cmH2O\": %.3f,\n  \"residuo_max_cmH2O\": %.3f,\n"
             "  \"n_puntos\": %d,\n  \"series\": \"%d-%d\",\n  \"K_teorica\": %.2f,\n"
             "  \"K_sugerida_firmware\": %.2f\n}\n",
             K, b, K0, r2, see, maxRes, n, serieMin, serieMax, K_PIM_FIRMWARE, fabs(K));
    f.close();
  }
  bool nuevo = !SD.exists(ARCH_AJUSTES);
  File h = SD.open(ARCH_AJUSTES, FILE_APPEND);
  if (h) {
    okHist = true;
    if (nuevo) h.println("series,n_puntos,K_cmH2O_por_V,b_cmH2O,K0_cmH2O_por_V,R2,error_tipico_cmH2O,residuo_max_cmH2O");
    h.printf("%d-%d,%d,%.3f,%.3f,%.3f,%.6f,%.3f,%.3f\n", serieMin, serieMax, n, K, b, K0, r2, see, maxRes);
    h.close();
  }
  Serial.println((okJson && okHist) ? F("[SD] Ajuste guardado en CALIBRACION_PIM.JSON y CALIBRACION_PIM_AJUSTES.CSV.")
                          : F("[SD] No se pudo guardar el ajuste."));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_PIM, INPUT);
  Serial.println(F("\n=== Calibración PIM (GPIO39) ==="));
  iniciarSD();
  Serial.println(F("Comandos: t tara | <mmHg> capturar | v vivo | b borrar último | l listar | f ajustar | r reiniciar | c cargar de la SD"));
}

void loop() {
  if (vivo && millis() - ultimoVivo >= 200) {
    ultimoVivo = millis();
    float v = leerVentana();
    float p = isnan(vCero) ? NAN : (v - vCero) * K_PIM_FIRMWARE;
    Serial.printf("V=%.4f  P_firmware=%+.1f cmH2O (%+.1f mmHg)\n", v, p, p / CMH2O_POR_MMHG);
  }
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.length() == 0) return;
  char c = s.charAt(0);
  if (c == 't') tara();
  else if (c == 'v') { vivo = !vivo; Serial.println(vivo ? F("[VIVO] encendido") : F("[VIVO] apagado")); }
  else if (c == 'b') { if (nPuntos > 0) { nPuntos--; Serial.printf("Borrado el punto %d\n", nPuntos + 1); borrarUltimoDeSD(); } }
  else if (c == 'l') listar();
  else if (c == 'f') ajustar();
  else if (c == 'c') cargarPuntos();
  else if (c == 'r') { nPuntos = 0; Serial.println(F("Puntos borrados (la tara se mantiene).")); }
  else if (isDigit(c) || c == '-' || c == '.') { vivo = false; capturar(s.toFloat()); }
  else Serial.println(F("Comando no reconocido."));
}
