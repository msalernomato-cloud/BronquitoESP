#ifndef ANALISIS_ENTRENAMIENTO_H
#define ANALISIS_ENTRENAMIENTO_H

// Entrenamiento muscular: detección de inspiraciones / espiraciones, ciclos y features.
// C++ estándar puro (sin hardware): se puede compilar y probar en la PC con los CSV de la SD.
//
// Es el port de deteccion_respiracion.py + features_respiracion.py (la referencia). Dos partes:
//
//   1. ContadorCiclos: DURANTE la medición. Solo cuenta ciclos completos (para cortar al llegar al
//      objetivo). Mismo principio que analizar() (fases por presión), con las muestras de a una.
//   2. analizar(): DESPUÉS de guardar. Con la señal completa leída de la SD detecta las fases POR PRESIÓN
//      (METODO = "presion" del Python):
//        - cero de la presión: mediana de la ventana de reposo inicial (0,2-1,0 s) o, si no fue reposo,
//          de toda la captura;
//        - tramos de signo constante de la presión (P > 0 = inspiración); un tramo es fase si su pico de |P|
//          pasa P_BANDA_CMH2O, su impulso ∫|P| dt pasa IMPULSO_MIN y mueve al menos VOL_MIN_L;
//        - unión de fases del mismo sentido con poco reposo entre ellas y bordes según haya pausa o no.
//      Después: flujo limpio (signo y K de la fase, BTPS, rachas invertidas interpoladas y Hampel por fase)
//      y las features de cada ciclo y el resumen.
//
// Diferencia con el Python: el volumen mínimo de cada tramo se mide con el flujo corregido (K de la fase y
// BTPS); el Python usa el flujo del equipo. Validado contra el Python (sin K) en las señales de prueba:
// mismos ciclos, diferencias de redondeo (la presión se guarda con 0,1 cmH2O de resolución).
//
// K de cada fase y BTPS (como la evaluación): el flujo del equipo es K·√|ΔV| con la K que tocó a CADA
// MUESTRA según el signo de la presión en ese instante (Kesp si salió > 0, Kinsp si salió < 0). Como
// Kinsp ≠ Kesp, una muestra con el sentido equivocado sale ×1,67 o ÷1,67. Con ConversionFlujo se recupera
// √|ΔV| = |Q| / K_muestra y se vuelve a multiplicar por la K de la FASE completa y por el factor BTPS.
// Se usa para el volumen mínimo de cada tramo (en vivo y al guardar) y para todas las features de flujo y
// volumen.
//
// Convenciones (placa actual): presión > 0 = INSPIRACIÓN; flujo del equipo < 0 = inspiración.
// Flujos y volúmenes de las features en magnitud. PIM positiva, PEM negativa (respecto del reposo).
// Lo que no se puede calcular queda en NAN (en el .json va como null, nunca 0).

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace AnalisisEntrenamiento {

const int INSPIRACION = 1;
const int ESPIRACION  = -1;
const int REPOSO      = 0;

// ---- Parámetros (mismos nombres y valores que en deteccion_respiracion.py) ----
// RespiratorySensors da la presión con signo físico (negativa al inspirar). Este signo la da vuelta para que,
// dentro de este análisis y en el .json, la inspiración sea positiva (PIM > 0, PEM < 0), como antes.
const float  SIGNO_PRESION_INSPIRACION = -1.0f;
const int    HAMPEL_VENTANA        = 5;          // ±5 muestras (±50 ms a 100 Hz)
const float  HAMPEL_N_SIGMA        = 3.0f;
const float  HAMPEL_SIGMA_MIN_LMIN = 10.0f;
const float  V_BAJO_CERO_MAX_V     = 0.020f;     // v_flujo más de 20 mV bajo su cero = falla del sensor
const int    MARGEN_REPARACION     = 5;
const double MAX_PAUSA_CICLO_S     = 3.0;   // el Python tiene 2,0: subido para cuando sostienen el aire arriba
const double CMH2O_L_A_J           = 98.0665e-3; // 1 cmH2O·L = 98,0665 Pa × 0,001 m³ = 0,0981 J

// ---- Detección por presión (en vivo y al guardar) ----
const double CALIBRACION_INI_S          = 0.2;   // ventana de reposo inicial para el cero de presión
const double CALIBRACION_FIN_S          = 1.0;
// Umbrales de presión escalados por 120,15 / 421,35 = 0,285 al calibrar la K de la PIM (antes 5, 5, 2 y 2):
// la misma señal en voltios ahora vale 0,285 veces menos cmH2O.
const float  CALIBRACION_DISPERSION_MAX = 1.43f; // p90 − p10 de la presión en esa ventana; más = no era reposo
const float  P_BANDA_CMH2O              = 1.43f; // banda de reposo: |P| menor que esto es "sin esfuerzo"
const float  P_RUIDO_CMH2O              = 0.57f; // dónde empieza/termina una fase cuando hay pausa
const double IMPULSO_MIN                = 0.57;  // cmH2O·s: ∫|P| dt mínimo para confirmar una fase
const double VOL_MIN_L                  = 0.15;  // volumen mínimo de una fase (con el flujo corregido)
const double PAUSA_MIN_S                = 0.15;  // |P| en la banda al menos esto entre dos fases = pausa real
const double UNION_MISMO_SENTIDO_S      = 0.5;   // fases del mismo sentido separadas por menos se unen
const int    MAX_INVERTIDAS             = 10;    // racha de signo invertido dentro de una fase que se interpola

// ---- Solo del contador en vivo (no están en el Python) ----
const float  UMBRAL_ALTO_LMIN = 40.0f;   // |flujo| que cuenta como "está respirando" (corte por sin respiración)
const float  ALFA_REPOSO      = 0.05f;   // si el primer segundo no fue reposo: seguimiento del cero de presión

// Escalas de la señal guardada en RAM (enteros de 16 bits para que entren ~10.000 muestras)
const float ESCALA_PRESION = 10.0f;     // 0,1 cmH2O
const float ESCALA_FLUJO   = 10.0f;     // 0,1 L/min
const float ESCALA_V       = 10000.0f;  // 0,1 mV (el CSV tiene 4 decimales: es exacto)

// =============================================================================
// Conversión del flujo: K de cada sentido (las de RespiratorySensors) y factor BTPS
// =============================================================================
// Con los valores por defecto (todo 1) no se cambia nada: el flujo queda como lo dio el equipo.
struct ConversionFlujo {
  float kEspiracion  = 1.0f;   // RespiratorySensors::getKEspiracion()
  float kInspiracion = 1.0f;   // RespiratorySensors::getKInspiracion()
  float factorBTPS   = 1.0f;   // BTPSSensor::getBTPSFactor() (1 = sin corregir)

  // Magnitud del flujo con la K de 'fase' (INSPIRACION | ESPIRACION) y BTPS, a partir del flujo del equipo
  float magnitud(float flujoEquipo_Lmin, int fase) const {
    float kMuestra = (flujoEquipo_Lmin < 0.0f) ? kInspiracion : kEspiracion;   // la que usó el equipo
    float kFase    = (fase == INSPIRACION) ? kInspiracion : kEspiracion;
    return fabsf(flujoEquipo_Lmin) / kMuestra * kFase * factorBTPS;
  }
};

// =============================================================================
// Señal completa de una sesión (leída del CSV de la SD)
// =============================================================================
struct SenalEntrenamiento {
  std::vector<uint32_t> t_ms;
  std::vector<int16_t>  presion;   // cmH2O * ESCALA_PRESION
  std::vector<int16_t>  flujo;     // L/min * ESCALA_FLUJO (con el signo del equipo)
  std::vector<uint16_t> vFlujo;    // V * ESCALA_V

  size_t size() const { return t_ms.size(); }
  void reservar(size_t n) { t_ms.reserve(n); presion.reserve(n); flujo.reserve(n); vFlujo.reserve(n); }
  void liberar() {
    std::vector<uint32_t>().swap(t_ms);
    std::vector<int16_t>().swap(presion);
    std::vector<int16_t>().swap(flujo);
    std::vector<uint16_t>().swap(vFlujo);
  }
  static int16_t a16(float x) {
    long v = lroundf(x);
    return (int16_t)std::max(-32768L, std::min(32767L, v));
  }
  void agregar(uint32_t tMs, float presion_cmH2O, float flujo_Lmin, float vFlujo_V) {
    t_ms.push_back(tMs);
    presion.push_back(a16(presion_cmH2O * ESCALA_PRESION));
    flujo.push_back(a16(flujo_Lmin * ESCALA_FLUJO));
    long v = lroundf(vFlujo_V * ESCALA_V);
    vFlujo.push_back((uint16_t)std::max(0L, std::min(65535L, v)));
  }
};

// Bytes de RAM que necesita analizar() además de la señal (para revisar la memoria antes)
inline size_t memoriaAnalisis(size_t n) {
  // Lo que más pide es buscar el cero de la presión cuando el primer segundo no fue reposo: 4 copias de la
  // presión (2 bytes c/u), con lugar para que una crezca. Después: fase (1) + flujo limpio (2).
  return n * 8 + 8192;
}
inline size_t memoriaSenal(size_t n) { return n * (4 + 2 + 2 + 2); }

// =============================================================================
// Resultados
// =============================================================================
struct EventoRespiratorio {
  int fase;                       // INSPIRACION | ESPIRACION
  int iInicio, iFin;
  float tInicio_s, tFin_s, duracion_s;
  float flujoPico_Lmin, tPico_s;  // magnitud
  float volumen_L;                // magnitud
  float presionPico_cmH2O, tPresionPico_s;   // con signo, sin el reposo
  float trabajo_J;                // ∫ presión (sin reposo) × |flujo| dt, positivo si la presión acompaña
                                  // a la fase: trabajo contra la resistencia del dispositivo
};

struct CicloRespiratorio {
  int numero;
  float t_inicio_s, t_fin_s;
  float ti_s, te_s, pausa_s, ti_ttot;
  float pif_Lmin, pef_Lmin;
  float vol_insp_L, vol_esp_L;
  float pim_cmH2O, pem_cmH2O;
  float trabajo_insp_J;           // trabajo de la inspiración contra el dispositivo
  float potencia_insp_W;          // trabajo_insp_J / ti_s (potencia media de la inspiración)
  float t_pif_s, t_pef_s, t_pim_s, t_pem_s;
};

struct FeaturesEntrenamiento {
  float duracion_s = NAN;
  std::vector<CicloRespiratorio> ciclos;

  // Promedios de las features de ciclo (NAN si no hay ciclos)
  float prom_ti_s = NAN, prom_te_s = NAN, prom_pausa_s = NAN, prom_ti_ttot = NAN;
  float prom_pif_Lmin = NAN, prom_pef_Lmin = NAN, prom_vol_insp_L = NAN, prom_vol_esp_L = NAN;
  float prom_pim_cmH2O = NAN, prom_pem_cmH2O = NAN;
  float prom_trabajo_insp_J = NAN, prom_potencia_insp_W = NAN;

  // Multiciclo
  int n_ciclos_completos = 0;
  int n_fases_sueltas = 0;
  float periodo_medio_s = NAN, frecuencia_rpm = NAN, ventilacion_minuto_Lmin = NAN;
  float trabajo_total_insp_J = 0.0f;   // suma sobre los ciclos (0 sin ciclos, como el Python)
  float pendiente_pim_por_ciclo = NAN, caida_pim_pct = NAN;
  float pif_max_Lmin = NAN, pef_max_Lmin = NAN, pim_max_cmH2O = NAN, pem_max_cmH2O = NAN;
  int pif_max_ciclo = 0, pef_max_ciclo = 0, pim_max_ciclo = 0, pem_max_ciclo = 0;   // 0 = null

  // Diagnóstico
  float presion_base_cmH2O = NAN;
  int muestras_reparadas = 0;
};

// =============================================================================
// Utilidades
// =============================================================================
// Mediana "alta" como en el Python (s[len // 2] de la lista ordenada). Desordena el vector.
template <class T>
inline T medianaAlta(std::vector<T> &v) {
  size_t m = v.size() / 2;
  std::nth_element(v.begin(), v.begin() + m, v.end());
  return v[m];
}

// Nivel de reposo (mediana) y ruido (MAD / 0,6745), descartando iterativamente lo que se aleja más
// de 3 desvíos. En unidades enteras de la señal (después se divide por la escala).
inline void reposoYRuido(const std::vector<int16_t> &x, float &centro, float &sigma) {
  std::vector<int16_t> v(x), tmp;
  centro = sigma = 0.0f;
  for (int it = 0; it < 5; it++) {
    tmp = v;
    centro = medianaAlta(tmp);
    for (size_t i = 0; i < v.size(); i++) tmp[i] = (int16_t)std::min(32767.0f, fabsf(v[i] - centro));
    sigma = medianaAlta(tmp) / 0.6745f;
    std::vector<int16_t> dentro;
    for (int16_t e : x) if (fabsf(e - centro) <= 3.0f * sigma) dentro.push_back(e);
    if (dentro.size() < 20 || dentro.size() == v.size()) break;
    v.swap(dentro);
  }
}

// =============================================================================
// 1) CONTADOR DE CICLOS EN VIVO
// =============================================================================
// Mismo principio que analizar() (fases por presión), con las muestras de a una (t en s, flujo y presión
// como salen del equipo; se redondean igual que al guardarlas en el CSV y leerlas):
//   - Cero de la presión: se juntan las muestras hasta pasar CALIBRACION_FIN_S y se toma la mediana de la
//     ventana de reposo inicial (como analizar). Recién ahí se procesan, empezando por las juntadas.
//     Si esa ventana no fue reposo, el cero se va siguiendo con las muestras de flujo 0 (reposo): ahí el
//     conteo en vivo puede diferir del análisis final, que lo estima con toda la captura.
//   - Tramos de signo constante de la presión; se van acumulando el pico de |P|, el impulso y el volumen
//     (flujo corregido). Como solo crecen, el tramo se CONFIRMA como fase apenas pasa los tres umbrales,
//     sin esperar a que termine.
//   - Ciclo: espiración confirmada después de una inspiración confirmada, con pausa <= MAX_PAUSA_CICLO_S
//     (de la última muestra con |P| >= P_RUIDO_CMH2O de la inspiración a la primera de la espiración).
//     Se cuenta mientras la persona todavía está espirando.
//   - Unir fases del mismo sentido (lo hace analizar) no cambia cuántos ciclos hay: acá no hace falta.
// Memoria: solo las muestras del primer segundo.
class ContadorCiclos {
public:
  // K de cada fase y BTPS para el volumen de los tramos (se mantiene al reiniciar)
  void configurar(const ConversionFlujo &c) { conv = c; }

  void reiniciar() {
    ConversionFlujo c = conv;
    *this = ContadorCiclos();
    conv = c;
  }

  // true si con esta muestra se completó un ciclo
  bool agregar(float t_s, float flujo_Lmin, float presion_cmH2O) {
    // Mismo redondeo que la señal guardada (0,1 cmH2O y 0,1 L/min)
    const float pr = SenalEntrenamiento::a16(SIGNO_PRESION_INSPIRACION * presion_cmH2O * ESCALA_PRESION) / ESCALA_PRESION;
    const float q  = SenalEntrenamiento::a16(flujo_Lmin * ESCALA_FLUJO) / ESCALA_FLUJO;
    const double t = t_s;
    tActual = t;
    if (!hayT0) { hayT0 = true; t0 = t; tUltimoFlujo = t; }
    if (fabsf(q) > UMBRAL_ALTO_LMIN) tUltimoFlujo = t;

    nuevosCiclos = 0;
    if (!hayCero) {
      inicio.push_back({t, pr, q});
      if (t - t0 <= CALIBRACION_FIN_S) return false;
      calcularCero();
      for (auto &m : inicio) procesar(m.t, m.p, m.q);
      std::vector<Muestra>().swap(inicio);
    } else {
      procesar(t, pr, q);
    }
    return nuevosCiclos > 0;
  }

  int ciclos() const { return nCiclos; }
  // Segundos desde el último flujo claro (> UMBRAL_ALTO_LMIN) o desde el inicio
  float segundosSinFlujo() const { return (float)(tActual - tUltimoFlujo); }

private:
  struct Muestra { double t; float p, q; };
  ConversionFlujo conv;

  // Cero de la presión (orientada: > 0 = inspiración)
  bool hayT0 = false, hayCero = false, seguirCero = false;
  double t0 = 0.0;
  float base = 0.0f;
  std::vector<Muestra> inicio;       // muestras del primer segundo, hasta tener el cero

  // Tramo de signo constante en curso
  bool hayTramo = false, confirmado = false;
  int sentido = REPOSO;
  double pico = 0.0, impulso = 0.0, volumen = 0.0;
  double tRuidoIni = -1.0, tRuidoFin = -1.0;   // primera y última muestra con |P| >= P_RUIDO_CMH2O
  double tPrev = 0.0;
  bool hayPrev = false;

  // Última fase confirmada (para armar ciclos)
  int ultimaFase = REPOSO;
  double ultimaFin = 0.0;

  int nCiclos = 0, nuevosCiclos = 0;
  double tActual = 0.0, tUltimoFlujo = 0.0;

  void calcularCero() {
    hayCero = true;
    std::vector<float> v, reposo;
    for (auto &m : inicio) {
      double dt = m.t - t0;
      if (dt >= CALIBRACION_INI_S && dt <= CALIBRACION_FIN_S) {
        v.push_back(m.p);
        if (m.q == 0.0f) reposo.push_back(m.p);
      }
    }
    if (v.size() >= 20) {
      std::sort(v.begin(), v.end());
      size_t n = v.size();
      if (v[9 * n / 10] - v[n / 10] <= CALIBRACION_DISPERSION_MAX) { base = v[n / 2]; return; }
    }
    // No fue reposo: se arranca con lo que haya de flujo 0 y se sigue ajustando en vivo
    seguirCero = true;
    if (!reposo.empty()) { std::sort(reposo.begin(), reposo.end()); base = reposo[reposo.size() / 2]; }
    else if (!v.empty()) { std::sort(v.begin(), v.end()); base = v[v.size() / 2]; }
  }

  void procesar(double t, float pr, float q) {
    if (seguirCero && q == 0.0f) base += ALFA_REPOSO * (pr - base);
    const float pf = pr - base;
    const int s = pf > 0 ? INSPIRACION : ESPIRACION;
    const double dt = hayPrev ? t - tPrev : 0.0;
    tPrev = t;
    hayPrev = true;

    if (!hayTramo || s != sentido) {   // cruce por cero: empieza otro tramo
      hayTramo = true;
      confirmado = false;
      sentido = s;
      pico = impulso = volumen = 0.0;
      tRuidoIni = tRuidoFin = -1.0;
    }
    impulso += fabsf(pf) * dt;
    volumen += conv.magnitud(q, s) / 60.0 * dt;
    pico = std::max(pico, (double)fabsf(pf));
    if (fabsf(pf) >= P_RUIDO_CMH2O) {
      if (tRuidoIni < 0) tRuidoIni = t;
      tRuidoFin = t;
    }

    if (!confirmado && pico >= P_BANDA_CMH2O && impulso >= IMPULSO_MIN && volumen >= VOL_MIN_L) {
      confirmado = true;
      if (s == ESPIRACION && ultimaFase == INSPIRACION && tRuidoIni - ultimaFin <= MAX_PAUSA_CICLO_S) {
        nCiclos++;
        nuevosCiclos++;
      }
      ultimaFase = s;
    }
    // La inspiración confirmada se sigue estirando hasta que termina su tramo
    if (confirmado && s == INSPIRACION) ultimaFin = tRuidoFin;
  }
};

// =============================================================================
// 2) ANÁLISIS COMPLETO (después de la medición)
// =============================================================================
namespace detalle {

// Caídas de v_flujo bajo su cero -> flujo interpolado. Devuelve cuántas muestras se repararon.
inline int repararCaidas(SenalEntrenamiento &s) {
  const int n = (int)s.size();
  if ((int)s.vFlujo.size() != n) return 0;
  std::vector<uint16_t> reposo;
  int nReposo = 0;
  for (int i = 0; i < n; i++) if (s.flujo[i] == 0) nReposo++;
  if (nReposo < 10) return 0;
  reposo.reserve(nReposo);
  for (int i = 0; i < n; i++) if (s.flujo[i] == 0) reposo.push_back(s.vFlujo[i]);
  const int cero = medianaAlta(reposo);
  std::vector<uint16_t>().swap(reposo);

  const int umbral = (int)lroundf(V_BAJO_CERO_MAX_V * ESCALA_V);
  auto d = [&](int i) { return (int)s.vFlujo[i] - cero; };
  std::vector<uint8_t> malas(n, 0);
  for (int i = 0; i < n; i++) {
    if (d(i) >= -umbral) continue;
    malas[i] = 1;
    for (int paso = -1; paso <= 1; paso += 2) {
      int k = i + paso;
      for (int r = 0; r < MARGEN_REPARACION; r++) {
        if (k < 0 || k >= n || d(k) >= 0) break;
        malas[k] = 1;
        k += paso;
      }
    }
  }
  int reparadas = 0;
  int i = 0;
  while (i < n) {
    if (!malas[i]) { i++; continue; }
    int a = i;
    while (i < n && malas[i]) i++;
    int z = i - 1;
    float izq = a > 0 ? s.flujo[a - 1] : (z + 1 < n ? s.flujo[z + 1] : 0);
    float der = z + 1 < n ? s.flujo[z + 1] : izq;
    for (int k = a; k <= z; k++) {
      float f = (float)(k - a + 1) / (z - a + 2);
      s.flujo[k] = SenalEntrenamiento::a16(izq + f * (der - izq));
    }
    reparadas += z - a + 1;
  }
  return reparadas;
}

}  // namespace detalle

// Analiza la señal completa. Repara el flujo en la propia señal y libera vFlujo (ya no hace falta).
// conv: K de cada fase y BTPS para todas las features de flujo y volumen (por defecto, sin cambios).
// flujoProc (opcional): si no es null, al terminar se deja ahí el flujo procesado de cada muestra (signo y
// K de la fase, BTPS y Hampel), en L/min * ESCALA_FLUJO y con el signo del equipo (negativo = inspiración).
inline bool analizar(SenalEntrenamiento &s, FeaturesEntrenamiento &out,
                     const ConversionFlujo &conv = ConversionFlujo(),
                     std::vector<int16_t> *flujoProc = nullptr) {
  using namespace detalle;
  out = FeaturesEntrenamiento();
  const int n = (int)s.size();
  if (n < 10) return false;

  auto T = [&](int k) { return s.t_ms[k] / 1000.0; };   // igual que el Python: tiempo_ms / 1000
  out.duracion_s = (float)(T(n - 1) - T(0));

  out.muestras_reparadas = repararCaidas(s);
  std::vector<uint16_t>().swap(s.vFlujo);

  // ---- Cero de la presión: mediana de la ventana de reposo inicial (como la calibración de la ESP).
  //      Si esa ventana no fue reposo (p90 − p10 > CALIBRACION_DISPERSION_MAX), se estima de toda la captura.
  float pBase;
  {
    std::vector<int16_t> ventana;
    for (int k = 0; k < n; k++) {
      double dt = T(k) - T(0);
      if (dt >= CALIBRACION_INI_S && dt <= CALIBRACION_FIN_S)
        ventana.push_back((int16_t)(SIGNO_PRESION_INSPIRACION * s.presion[k]));
    }
    bool hayReposo = false;
    if (ventana.size() >= 20) {
      std::sort(ventana.begin(), ventana.end());
      size_t m = ventana.size();
      float dispersion = (ventana[9 * m / 10] - ventana[m / 10]) / ESCALA_PRESION;
      if (dispersion <= CALIBRACION_DISPERSION_MAX) {
        pBase = ventana[m / 2] / ESCALA_PRESION;
        hayReposo = true;
      }
    }
    std::vector<int16_t>().swap(ventana);
    if (!hayReposo) {
      std::vector<int16_t> pr(n);
      for (int k = 0; k < n; k++) pr[k] = (int16_t)(SIGNO_PRESION_INSPIRACION * s.presion[k]);
      float desvio;
      reposoYRuido(pr, pBase, desvio);
      pBase /= ESCALA_PRESION;
    }
  }
  out.presion_base_cmH2O = SIGNO_PRESION_INSPIRACION * pBase;
  auto PF = [&](int k) { return SIGNO_PRESION_INSPIRACION * s.presion[k] / ESCALA_PRESION - pBase; };
  auto Q  = [&](int k) { return s.flujo[k] / ESCALA_FLUJO; };

  // ---- Fases por presión (PF > 0 = inspiración) ----
  struct Fase { int a, z, f; };
  // 1) Tramos de signo constante, y 2) confirmación por tamaño: pico de |P|, impulso ∫|P| dt y volumen
  //    (con el flujo corregido: K del sentido del tramo y BTPS). Los demás tramos son ruido o pausa.
  std::vector<Fase> confirmadas;
  {
    int a = 0;
    for (int k = 1; k <= n; k++) {
      if (k < n && (PF(k) > 0) == (PF(a) > 0)) continue;
      int z = k - 1, f = PF(a) > 0 ? INSPIRACION : ESPIRACION;
      double impulso = 0.0, volumen = 0.0, pico = 0.0;
      for (int j = a; j <= z; j++) {
        double dt = j > 0 ? T(j) - T(j - 1) : 0.0;
        impulso += fabs(PF(j)) * dt;
        volumen += conv.magnitud(Q(j), f) / 60.0 * dt;
        pico = std::max(pico, (double)fabsf(PF(j)));
      }
      if (pico >= P_BANDA_CMH2O && impulso >= IMPULSO_MIN && volumen >= VOL_MIN_L)
        confirmadas.push_back({a, z, f});
      a = k;
    }
  }
  // Primera (o última) muestra de [a, z] fuera de la banda de reposo
  auto fueraDeBanda = [&](int a, int z, bool desdeElFinal) {
    if (desdeElFinal) { for (int k = z; k >= a; k--) if (fabsf(PF(k)) >= P_BANDA_CMH2O) return k; return z; }
    for (int k = a; k <= z; k++) if (fabsf(PF(k)) >= P_BANDA_CMH2O) return k;
    return a;
  };
  // 3) Unir fases del mismo sentido con poco tiempo en reposo entre ellas
  std::vector<Fase> unidas;
  for (auto &fs : confirmadas) {
    if (!unidas.empty() && unidas.back().f == fs.f) {
      int finPrev = fueraDeBanda(unidas.back().a, unidas.back().z, true);
      int iniSig  = fueraDeBanda(fs.a, fs.z, false);
      if (T(iniSig) - T(finPrev) <= UNION_MISMO_SENTIDO_S) { unidas.back().z = fs.z; continue; }
    }
    unidas.push_back(fs);
  }
  std::vector<Fase>().swap(confirmadas);
  // 4) Bordes: recortar al nivel de ruido y pegar las fases de sentido contrario sin pausa entre ellas
  std::vector<Fase> fases(unidas.size());
  for (size_t i = 0; i < unidas.size(); i++) {
    int a = unidas[i].a, z = unidas[i].z, ini = a, fin = z;
    for (int k = a; k <= z; k++) if (fabsf(PF(k)) >= P_RUIDO_CMH2O) { ini = k; break; }
    for (int k = z; k >= a; k--) if (fabsf(PF(k)) >= P_RUIDO_CMH2O) { fin = k; break; }
    fases[i] = {ini, fin, unidas[i].f};
  }
  for (size_t i = 0; i + 1 < unidas.size(); i++) {
    const Fase &f1 = unidas[i], &f2 = unidas[i + 1];
    if (f1.f == f2.f) continue;   // mismo sentido sin unir: hubo una pausa larga, quedan recortadas
    double reposo = T(fueraDeBanda(f2.a, f2.z, false)) - T(fueraDeBanda(f1.a, f1.z, true));
    if (reposo < PAUSA_MIN_S) {   // sin pausa: la siguiente arranca en su cruce por cero
      fases[i + 1].a = f2.a;
      fases[i].z = f2.a - 1;
    }
  }
  std::vector<Fase>().swap(unidas);

  std::vector<int8_t> fase(n, REPOSO);
  for (auto &fs : fases) for (int k = fs.a; k <= fs.z; k++) fase[k] = (int8_t)fs.f;

  // ---- Flujo limpio ----
  // a) Signo y K de la fase, y BTPS. Las rachas cortas (≤ MAX_INVERTIDAS) de signo invertido dentro de una
  //    fase (el equipo eligió mal el sentido y la K cerca de presión 0, ej. 68, −122, 72 en una espiración)
  //    se reemplazan interpolando entre las muestras buenas vecinas. En reposo: el flujo del equipo × BTPS.
  //    Se escribe ENCIMA del flujo de la señal (s.flujo): de acá en adelante el flujo del equipo ya no se
  //    usa, y así no hace falta otro vector del tamaño de la señal. Se recorre de izquierda a derecha y
  //    cada valor original se lee antes de pisarlo (mPrev guarda la magnitud de la muestra anterior).
  {
    auto invertida = [&](int k) {
      return fase[k] != REPOSO && s.flujo[k] != 0 && (s.flujo[k] > 0) == (fase[k] == INSPIRACION);
    };
    auto escribir = [&](int j, float mag) {
      s.flujo[j] = SenalEntrenamiento::a16((fase[j] == INSPIRACION ? -mag : mag) * ESCALA_FLUJO);
    };
    float mPrev = 0.0f;   // magnitud (K de la fase y BTPS) de la muestra k - 1, antes de pisarla
    int k = 0;
    while (k < n) {
      if (fase[k] == REPOSO) {
        s.flujo[k] = SenalEntrenamiento::a16(Q(k) * conv.factorBTPS * ESCALA_FLUJO);
        mPrev = 0.0f;
        k++;
        continue;
      }
      if (!invertida(k)) {
        float mk = conv.magnitud(Q(k), fase[k]);
        escribir(k, mk);
        mPrev = mk;
        k++;
        continue;
      }
      int f = fase[k], a = k;
      while (k < n && fase[k] == f && invertida(k)) k++;
      int z = k - 1;
      bool hayIzq = a > 0 && fase[a - 1] == f, hayDer = z + 1 < n && fase[z + 1] == f;
      if (z - a + 1 <= MAX_INVERTIDAS && (hayIzq || hayDer)) {
        float mDer = hayDer ? conv.magnitud(Q(z + 1), f) : 0.0f;   // z + 1 todavía no se pisó
        float izq = hayIzq ? mPrev : mDer, der = hayDer ? mDer : izq;
        for (int j = a; j <= z; j++) {
          float mj = izq + (float)(j - a + 1) / (z - a + 2) * (der - izq);
          escribir(j, mj);
          mPrev = mj;
        }
      } else {   // racha larga (o sin vecinas de la fase): solo se corrige el signo (y la K)
        for (int j = a; j <= z; j++) {
          float mj = conv.magnitud(Q(j), f);
          escribir(j, mj);
          mPrev = mj;
        }
      }
    }
  }
  // b) Hampel DENTRO de cada tramo de fase constante: si la ventana mezclara la fase vecina (de signo
  //    contrario), un pico en el borde de la fase no se detectaría.
  std::vector<int16_t> limpio(n);
  {
    float w[2 * HAMPEL_VENTANA + 1], dv[2 * HAMPEL_VENTANA + 1];
    int ini = 0;
    for (int fin = 1; fin <= n; fin++) {
      if (fin < n && fase[fin] == fase[ini]) continue;
      for (int i = ini; i < fin; i++) {
        int a = std::max(ini, i - HAMPEL_VENTANA), z = std::min(fin - 1, i + HAMPEL_VENTANA), m = z - a + 1;
        for (int j = 0; j < m; j++) w[j] = s.flujo[a + j] / ESCALA_FLUJO;
        std::sort(w, w + m);
        float med = w[m / 2];
        for (int j = 0; j < m; j++) dv[j] = fabsf(w[j] - med);
        std::sort(dv, dv + m);
        float sigma = std::max(HAMPEL_SIGMA_MIN_LMIN, 1.4826f * dv[m / 2]);
        float x = s.flujo[i] / ESCALA_FLUJO;
        limpio[i] = SenalEntrenamiento::a16((fabsf(x - med) > HAMPEL_N_SIGMA * sigma ? med : x) * ESCALA_FLUJO);
      }
      ini = fin;
    }
  }
  auto L = [&](int k) { return fabsf(limpio[k] / ESCALA_FLUJO); };

  // ---- Eventos ----
  std::vector<EventoRespiratorio> eventos;
  for (auto &fs : fases) {
    EventoRespiratorio e;
    e.fase = fs.f;
    e.iInicio = fs.a; e.iFin = fs.z;
    e.tInicio_s = (float)T(fs.a); e.tFin_s = (float)T(fs.z);
    e.duracion_s = (float)(T(fs.z) - T(fs.a));
    double vol = 0.0, trabajo = 0.0;
    int iPico = fs.a, iPres = fs.a;
    for (int k = fs.a; k <= fs.z; k++) {
      if (L(k) > L(iPico)) iPico = k;
      if (k > fs.a) {
        // Trapecios: volumen = ∫|Q| dt ; trabajo = ∫ P·|Q| dt (P sin reposo, con el signo de la fase)
        double dt = T(k) - T(k - 1);
        vol += 0.5 * (L(k) + L(k - 1)) / 60.0 * dt;
        trabajo += 0.5 * (fs.f * PF(k) * L(k) + fs.f * PF(k - 1) * L(k - 1)) / 60.0 * dt;
      }
      if (fs.f * PF(k) > fs.f * PF(iPres)) iPres = k;
    }
    e.trabajo_J = (float)(trabajo * CMH2O_L_A_J);
    e.flujoPico_Lmin = L(iPico);
    e.tPico_s = (float)T(iPico);
    e.volumen_L = (float)vol;
    e.presionPico_cmH2O = PF(iPres);   // convención del análisis: PIM > 0, PEM < 0
    e.tPresionPico_s = (float)T(iPres);
    eventos.push_back(e);
  }

  // ---- Ciclos: inspiración seguida de su espiración ----
  for (size_t i = 0; i + 1 < eventos.size(); i++) {
    const EventoRespiratorio &in = eventos[i], &es = eventos[i + 1];
    if (in.fase != INSPIRACION || es.fase != ESPIRACION) continue;
    float pausa = es.tInicio_s - in.tFin_s;
    if (pausa > MAX_PAUSA_CICLO_S) continue;
    CicloRespiratorio c;
    c.numero = (int)out.ciclos.size() + 1;
    c.t_inicio_s = in.tInicio_s; c.t_fin_s = es.tFin_s;
    c.ti_s = in.duracion_s; c.te_s = es.duracion_s; c.pausa_s = pausa;
    c.ti_ttot = in.duracion_s / (in.duracion_s + es.duracion_s);
    c.pif_Lmin = in.flujoPico_Lmin; c.pef_Lmin = es.flujoPico_Lmin;
    c.vol_insp_L = in.volumen_L; c.vol_esp_L = es.volumen_L;
    c.pim_cmH2O = in.presionPico_cmH2O; c.pem_cmH2O = es.presionPico_cmH2O;
    c.trabajo_insp_J = in.trabajo_J;
    c.potencia_insp_W = in.duracion_s > 0.0f ? in.trabajo_J / in.duracion_s : NAN;
    c.t_pif_s = in.tPico_s; c.t_pef_s = es.tPico_s;
    c.t_pim_s = in.tPresionPico_s; c.t_pem_s = es.tPresionPico_s;
    out.ciclos.push_back(c);
  }

  // ---- Resumen ----
  const auto &C = out.ciclos;
  const int nc = (int)C.size();
  out.n_ciclos_completos = nc;
  out.n_fases_sueltas = (int)eventos.size() - 2 * nc;
  auto media = [&](float CicloRespiratorio::*campo) {
    if (nc == 0) return (float)NAN;
    double s = 0;
    for (auto &c : C) s += c.*campo;
    return (float)(s / nc);
  };
  out.prom_ti_s = media(&CicloRespiratorio::ti_s);
  out.prom_te_s = media(&CicloRespiratorio::te_s);
  out.prom_pausa_s = media(&CicloRespiratorio::pausa_s);
  out.prom_ti_ttot = media(&CicloRespiratorio::ti_ttot);
  out.prom_pif_Lmin = media(&CicloRespiratorio::pif_Lmin);
  out.prom_pef_Lmin = media(&CicloRespiratorio::pef_Lmin);
  out.prom_vol_insp_L = media(&CicloRespiratorio::vol_insp_L);
  out.prom_vol_esp_L = media(&CicloRespiratorio::vol_esp_L);
  out.prom_pim_cmH2O = media(&CicloRespiratorio::pim_cmH2O);
  out.prom_pem_cmH2O = media(&CicloRespiratorio::pem_cmH2O);
  out.prom_trabajo_insp_J = media(&CicloRespiratorio::trabajo_insp_J);
  out.prom_potencia_insp_W = media(&CicloRespiratorio::potencia_insp_W);
  {
    double total = 0;
    for (auto &c : C) total += c.trabajo_insp_J;
    out.trabajo_total_insp_J = (float)total;
  }

  if (nc >= 2) {
    double sp = 0;
    for (int i = 1; i < nc; i++) sp += C[i].t_inicio_s - C[i - 1].t_inicio_s;
    out.periodo_medio_s = (float)(sp / (nc - 1));
    if (out.periodo_medio_s > 0) out.frecuencia_rpm = 60.0f / out.periodo_medio_s;
    if (!std::isnan(out.frecuencia_rpm)) out.ventilacion_minuto_Lmin = out.prom_vol_esp_L * out.frecuencia_rpm;

    // Pendiente de la PIM contra el número de ciclo (mínimos cuadrados)
    double xm = (nc + 1) / 2.0, ym = out.prom_pim_cmH2O, num = 0, den = 0;
    for (int i = 0; i < nc; i++) { num += (i + 1 - xm) * (C[i].pim_cmH2O - ym); den += (i + 1 - xm) * (i + 1 - xm); }
    out.pendiente_pim_por_ciclo = (float)(num / den);

    // Caída de la PIM: último tercio contra el primero. POSITIVA = la PIM cayó (fatiga), negativa = subió.
    // (Distinto del Python, que da (último - primero) / primero: el signo está invertido a propósito.)
    int k = std::max(1, nc / 3);
    double pri = 0, ult = 0;
    for (int i = 0; i < k; i++) { pri += C[i].pim_cmH2O; ult += C[nc - k + i].pim_cmH2O; }
    pri /= k; ult /= k;
    if (pri != 0) out.caida_pim_pct = (float)((pri - ult) / pri * 100.0);
  }

  if (nc > 0) {
    int iPif = 0, iPef = 0, iPim = 0, iPem = 0;   // el primero en caso de empate (como max() del Python)
    for (int i = 1; i < nc; i++) {
      if (C[i].pif_Lmin > C[iPif].pif_Lmin) iPif = i;
      if (C[i].pef_Lmin > C[iPef].pef_Lmin) iPef = i;
      if (C[i].pim_cmH2O > C[iPim].pim_cmH2O) iPim = i;
      if (C[i].pem_cmH2O < C[iPem].pem_cmH2O) iPem = i;   // la PEM es negativa: el máximo es el mínimo
    }
    out.pif_max_Lmin = C[iPif].pif_Lmin;  out.pif_max_ciclo = iPif + 1;
    out.pef_max_Lmin = C[iPef].pef_Lmin;  out.pef_max_ciclo = iPef + 1;
    out.pim_max_cmH2O = C[iPim].pim_cmH2O; out.pim_max_ciclo = iPim + 1;
    out.pem_max_cmH2O = C[iPem].pem_cmH2O; out.pem_max_ciclo = iPem + 1;
  }
  if (flujoProc) flujoProc->swap(limpio);
  return true;
}

}  // namespace AnalisisEntrenamiento

#endif
