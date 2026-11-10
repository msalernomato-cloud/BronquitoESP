#ifndef ANALISIS_ENTRENAMIENTO_H
#define ANALISIS_ENTRENAMIENTO_H

// Entrenamiento muscular: detección de inspiraciones / espiraciones, ciclos y features.
// C++ estándar puro (sin hardware): se puede compilar y probar en la PC con los CSV de la SD.
//
// Es el port de deteccion_respiracion.py + features_respiracion.py (la referencia). Dos partes:
//
//   1. ContadorCiclos: DURANTE la medición. Solo cuenta ciclos completos (para cortar al llegar al
//      objetivo). Recibe las muestras de a una y aplica la misma lógica que el Python, pero con el
//      nivel de reposo de la presión estimado en vivo (no se conoce la captura entera).
//   2. analizar(): DESPUÉS de guardar. Con la señal completa leída de la SD hace lo del Python
//      (reposo de toda la captura, reparación de caídas, valles, Hampel) y calcula las features de
//      cada ciclo y el resumen.
//
// Diferencia con el Python: el sentido de cada fase sale SIEMPRE de la presión (signo de la presión
// pesada por |flujo|). No hay umbral de ruido ni se usa el signo del flujo del equipo como alternativa
// (el equipo también saca ese signo de la presión, muestra por muestra, así que no agrega nada).
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

// ---- Parámetros (mismos nombres y valores que en el Python) ----
const float  SIGNO_PRESION_INSPIRACION = 1.0f;   // placa actual: presión positiva al inspirar
const float  UMBRAL_ALTO_LMIN      = 40.0f;      // |flujo| para que arranque una fase
const float  UMBRAL_BAJO_LMIN      = 10.0f;      // la fase sigue mientras |flujo| esté sobre esto
const int    HAMPEL_VENTANA        = 5;          // ±5 muestras (±50 ms a 100 Hz)
const float  HAMPEL_N_SIGMA        = 3.0f;
const float  HAMPEL_SIGMA_MIN_LMIN = 10.0f;
const float  V_BAJO_CERO_MAX_V     = 0.020f;     // v_flujo más de 20 mV bajo su cero = falla del sensor
const int    MARGEN_REPARACION     = 5;
const float  PROFUNDIDAD_CORTE     = 0.30f;      // fases pegadas: valle < 30 % del pico menor
const double VENTANA_VALLE_S       = 0.10;
const double VENTANA_PICO_S        = 0.60;
const double HUECO_UNION_S         = 0.15;
const double DURACION_MIN_S        = 0.10;
const double MAX_PAUSA_CICLO_S     = 2.0;

// Solo del contador en vivo (no están en el Python)
const double DESCARTE_INICIAL_S    = 0.2;        // el arranque de la captura no es reposo
const float  ALFA_REPOSO           = 0.05f;      // promedio exponencial del reposo de la presión
const int    MAX_MUESTRAS_TRAMO    = 2000;       // 20 s seguidos con flujo: se cierra el tramo igual

// Escalas de la señal guardada en RAM (enteros de 16 bits para que entren ~10.000 muestras)
const float ESCALA_PRESION = 10.0f;     // 0,1 cmH2O
const float ESCALA_FLUJO   = 10.0f;     // 0,1 L/min
const float ESCALA_V       = 10000.0f;  // 0,1 mV (el CSV tiene 4 decimales: es exacto)

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
  return n * (2 /*limpio*/ + 1 /*fase*/ + 3 * 2 /*copias para medianas*/) + 8192;
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
};

struct CicloRespiratorio {
  int numero;
  float t_inicio_s, t_fin_s;
  float ti_s, te_s, pausa_s, ti_ttot;
  float pif_Lmin, pef_Lmin;
  float vol_insp_L, vol_esp_L;
  float pim_cmH2O, pem_cmH2O;
  float t_pif_s, t_pef_s, t_pim_s, t_pem_s;
};

struct FeaturesEntrenamiento {
  float duracion_s = NAN;
  std::vector<CicloRespiratorio> ciclos;

  // Promedios de las features de ciclo (NAN si no hay ciclos)
  float prom_ti_s = NAN, prom_te_s = NAN, prom_pausa_s = NAN, prom_ti_ttot = NAN;
  float prom_pif_Lmin = NAN, prom_pef_Lmin = NAN, prom_vol_insp_L = NAN, prom_vol_esp_L = NAN;
  float prom_pim_cmH2O = NAN, prom_pem_cmH2O = NAN;

  // Multiciclo
  int n_ciclos_completos = 0;
  int n_fases_sueltas = 0;
  float periodo_medio_s = NAN, frecuencia_rpm = NAN, ventilacion_minuto_Lmin = NAN;
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
// Se le pasan las muestras de a una (t en s, flujo y presión como salen del equipo). Igual que el
// Python: tramos con histéresis 40/10 L/min, corte en los valles hondos (se confirma 0,6 s después,
// cuando ya se vio el pico de la derecha), sentido de cada pedazo por la presión pesada por |flujo|,
// unión de pedazos del mismo sentido y descarte de fases cortas. Un ciclo se cuenta cuando se cierra
// la espiración que sigue a una inspiración (unos 0,15 s después de que termina de espirar).
// Memoria: solo el tramo en curso (como mucho MAX_MUESTRAS_TRAMO muestras).
class ContadorCiclos {
public:
  void reiniciar() {
    *this = ContadorCiclos();
  }

  // true si con esta muestra se completó un ciclo
  bool agregar(float t_s, float flujo_Lmin, float presion_cmH2O) {
    const double t = t_s;
    const float mag = fabsf(flujo_Lmin);
    const float pr = SIGNO_PRESION_INSPIRACION * presion_cmH2O;
    tActual = t;
    nuevosCiclos = 0;
    if (mag > UMBRAL_ALTO_LMIN) tUltimoFlujo = t;

    if (mag > UMBRAL_BAJO_LMIN) {
      if (!enTramo) { enTramo = true; superoAlto = false; limpiarTramo(); }
      if (mag > UMBRAL_ALTO_LMIN) superoAlto = true;
      agregarAlTramo(t, mag, pr - base);
      if (superoAlto) evaluarValles(false);   // un tramo que nunca pasa 40 L/min no es una fase
      if ((int)tt.size() >= MAX_MUESTRAS_TRAMO) cerrarTramo();
    } else {
      if (enTramo) cerrarTramo();
      // Reposo: se sigue el nivel de la presión (la tara puede haber quedado corrida)
      if (mag == 0.0f && t >= DESCARTE_INICIAL_S) {
        base += ALFA_REPOSO * (pr - base);
      }
    }

    // La fase en armado ya no se puede unir con nada: se emite
    if (hayFase) {
      double proximo = (enTramo && !tt.empty()) ? tt[inicioPieza] : t;
      if (proximo - faseFin > HUECO_UNION_S) emitirFase();
      else if (enTramo && superoAlto && sentidoProvisorioContrario()) emitirFase();
    }
    return nuevosCiclos > 0;
  }

  int ciclos() const { return nCiclos; }
  // Segundos desde el último flujo claro (> UMBRAL_ALTO) o desde el inicio
  float segundosSinFlujo() const { return (float)(tActual - tUltimoFlujo); }

private:
  // Reposo de la presión (orientada: > 0 = inspiración)
  float base = 0.0f;

  // Tramo en curso (índices locales). Suma acumulada de la presión pesada para el sentido de cada pedazo.
  bool enTramo = false, superoAlto = false;
  std::vector<double> tt;
  std::vector<float> mg;
  std::vector<double> sW;            // prefijo (tamaño = muestras + 1)
  int inicioPieza = 0;               // primera muestra del pedazo abierto
  int proxValle = 1;                 // próximo candidato a valle por evaluar

  // Fase en armado (pedazos del mismo sentido unidos)
  bool hayFase = false;
  int faseSentido = REPOSO;
  double faseIni = 0.0, faseFin = 0.0;

  // Última fase emitida (para armar ciclos)
  int ultimaFase = REPOSO;
  double ultimaFin = 0.0;

  int nCiclos = 0, nuevosCiclos = 0;
  double tActual = 0.0, tUltimoFlujo = 0.0;

  void limpiarTramo() {
    tt.clear(); mg.clear();
    sW.assign(1, 0.0);
    inicioPieza = 0;
    proxValle = 1;
  }

  void agregarAlTramo(double t, float mag, float pf) {
    tt.push_back(t);
    mg.push_back(mag);
    sW.push_back(sW.back() + (double)pf * mag);
  }

  // ¿Es k un valle hondo? z = última muestra que se puede mirar
  bool esValle(int k, int z) const {
    if (!(mg[k] <= mg[k - 1] && mg[k] < mg[k + 1])) return false;
    int j0 = k, j1 = k;
    while (j0 > 0 && tt[k] - tt[j0 - 1] <= VENTANA_VALLE_S) j0--;
    while (j1 < z && tt[j1 + 1] - tt[k] <= VENTANA_VALLE_S) j1++;
    for (int j = j0; j <= j1; j++) if (mg[j] < mg[k]) return false;
    float izq = 0.0f, der = 0.0f;
    for (int j = k - 1; j >= 0 && tt[k] - tt[j] <= VENTANA_PICO_S; j--) izq = std::max(izq, mg[j]);
    for (int j = k + 1; j <= z && tt[j] - tt[k] <= VENTANA_PICO_S; j++) der = std::max(der, mg[j]);
    return mg[k] < PROFUNDIDAD_CORTE * std::min(izq, der);
  }

  // Evalúa los candidatos que ya tienen completa la ventana de la derecha (o todos, al cerrar)
  void evaluarValles(bool alCerrar) {
    int z = (int)tt.size() - 1;
    while (proxValle < z) {
      if (!alCerrar && tt[z] - tt[proxValle] <= VENTANA_PICO_S) break;
      int k = proxValle++;
      if (esValle(k, z)) {
        cerrarPieza(inicioPieza, k - 1);
        inicioPieza = k;
      }
    }
  }

  void cerrarTramo() {
    if (superoAlto && !tt.empty()) {
      evaluarValles(true);
      cerrarPieza(inicioPieza, (int)tt.size() - 1);
    }
    enTramo = false;
    superoAlto = false;
    limpiarTramo();
  }

  // Inspiración y espiración pegadas: el pedazo abierto todavía no se cerró (su valle se confirma
  // 0,6 s después), pero si ya dura DURACION_MIN_S y la presión dice el sentido contrario al de la
  // fase en armado, esa fase no se va a unir con él: se emite ya, sin esperar.
  bool sentidoProvisorioContrario() const {
    int a = inicioPieza, z = (int)tt.size() - 1;
    if (z <= a || tt[z] - tt[a] < DURACION_MIN_S) return false;
    return sentido(sW[z + 1] - sW[a]) != faseSentido;
  }

  // Sentido por la presión pesada por |flujo| (siempre por presión: ver el comentario del principio)
  static int sentido(double w) { return (w > 0) ? INSPIRACION : ESPIRACION; }

  // Sentido de un pedazo [a, z] y unión con la fase en armado
  void cerrarPieza(int a, int z) {
    int f = sentido(sW[z + 1] - sW[a]);

    if (hayFase && faseSentido == f && tt[a] - faseFin <= HUECO_UNION_S) {
      faseFin = tt[z];
      return;
    }
    if (hayFase) emitirFase();
    hayFase = true;
    faseSentido = f;
    faseIni = tt[a];
    faseFin = tt[z];
  }

  void emitirFase() {
    hayFase = false;
    if (faseFin - faseIni < DURACION_MIN_S) return;   // ruido
    if (faseSentido == ESPIRACION && ultimaFase == INSPIRACION && faseIni - ultimaFin <= MAX_PAUSA_CICLO_S) {
      nCiclos++;
      nuevosCiclos++;
    }
    ultimaFase = faseSentido;
    ultimaFin = faseFin;
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
inline bool analizar(SenalEntrenamiento &s, FeaturesEntrenamiento &out) {
  using namespace detalle;
  out = FeaturesEntrenamiento();
  const int n = (int)s.size();
  if (n < 10) return false;

  auto T = [&](int k) { return s.t_ms[k] / 1000.0; };   // igual que el Python: tiempo_ms / 1000
  out.duracion_s = (float)(T(n - 1) - T(0));

  out.muestras_reparadas = repararCaidas(s);
  std::vector<uint16_t>().swap(s.vFlujo);

  // Presión orientada y sin su nivel de reposo
  // (el desvío que calcula reposoYRuido solo sirve para descartar las respiraciones al buscar el reposo)
  float pBase, pDesvio;
  {
    std::vector<int16_t> pr(n);
    for (int k = 0; k < n; k++) pr[k] = (int16_t)(SIGNO_PRESION_INSPIRACION * s.presion[k]);
    reposoYRuido(pr, pBase, pDesvio);
    pBase /= ESCALA_PRESION;
  }
  out.presion_base_cmH2O = SIGNO_PRESION_INSPIRACION * pBase;
  auto PF  = [&](int k) { return SIGNO_PRESION_INSPIRACION * s.presion[k] / ESCALA_PRESION - pBase; };
  auto Q   = [&](int k) { return s.flujo[k] / ESCALA_FLUJO; };
  auto MAG = [&](int k) { return std::abs((int)s.flujo[k]) / ESCALA_FLUJO; };

  // ---- Tramos con flujo (histéresis) ----
  std::vector<std::pair<int, int>> tramos;
  for (int i = 0; i < n;) {
    if (MAG(i) <= UMBRAL_ALTO_LMIN) { i++; continue; }
    int a = i, z = i;
    while (a > 0 && MAG(a - 1) > UMBRAL_BAJO_LMIN) a--;
    while (z < n - 1 && MAG(z + 1) > UMBRAL_BAJO_LMIN) z++;
    if (!tramos.empty() && a <= tramos.back().second) tramos.back().second = z;
    else tramos.push_back({a, z});
    i = z + 1;
  }

  // ---- Corte en los valles hondos ----
  std::vector<std::pair<int, int>> pedazos;
  for (auto &tr : tramos) {
    int a = tr.first, z = tr.second, desde = a;
    for (int k = a + 1; k < z; k++) {
      float mk = MAG(k);
      if (!(mk <= MAG(k - 1) && mk < MAG(k + 1))) continue;
      int j0 = k, j1 = k;
      while (j0 > a && T(k) - T(j0 - 1) <= VENTANA_VALLE_S) j0--;
      while (j1 < z && T(j1 + 1) - T(k) <= VENTANA_VALLE_S) j1++;
      bool minimo = true;
      for (int j = j0; j <= j1 && minimo; j++) if (MAG(j) < mk) minimo = false;
      if (!minimo) continue;
      float izq = 0.0f, der = 0.0f;
      for (int j = a; j < k; j++) if (T(k) - T(j) <= VENTANA_PICO_S) izq = std::max(izq, MAG(j));
      for (int j = k + 1; j <= z; j++) if (T(j) - T(k) <= VENTANA_PICO_S) der = std::max(der, MAG(j));
      if (mk < PROFUNDIDAD_CORTE * std::min(izq, der)) {
        pedazos.push_back({desde, k - 1});
        desde = k;
      }
    }
    pedazos.push_back({desde, z});
  }

  // ---- Sentido de cada pedazo y unión de los vecinos del mismo sentido ----
  //      Siempre por la presión: signo de la presión pesada por |flujo| (el flujo bajo casi no pesa)
  struct Fase { int a, z, f; };
  std::vector<Fase> unidos;
  for (auto &pz : pedazos) {
    int a = pz.first, z = pz.second;
    double w = 0;
    for (int k = a; k <= z; k++) w += PF(k) * MAG(k);
    Fase fs;
    fs.a = a; fs.z = z;
    fs.f = (w > 0) ? INSPIRACION : ESPIRACION;
    if (!unidos.empty() && unidos.back().f == fs.f && T(a) - T(unidos.back().z) <= HUECO_UNION_S) {
      unidos.back().z = z;
    } else {
      unidos.push_back(fs);
    }
  }

  std::vector<Fase> fases;
  for (auto &fs : unidos) if (T(fs.z) - T(fs.a) >= DURACION_MIN_S) fases.push_back(fs);
  std::vector<std::pair<int, int>>().swap(tramos);
  std::vector<std::pair<int, int>>().swap(pedazos);

  std::vector<int8_t> fase(n, REPOSO);
  for (auto &fs : fases) for (int k = fs.a; k <= fs.z; k++) fase[k] = (int8_t)fs.f;

  // ---- Flujo limpio: signo de la fase + Hampel ----
  auto corregido = [&](int k) {
    float q = Q(k);
    return fase[k] == INSPIRACION ? -fabsf(q) : fase[k] == ESPIRACION ? fabsf(q) : q;
  };
  std::vector<int16_t> limpio(n);
  {
    float w[2 * HAMPEL_VENTANA + 1], dv[2 * HAMPEL_VENTANA + 1];
    for (int i = 0; i < n; i++) {
      int a = std::max(0, i - HAMPEL_VENTANA), z = std::min(n - 1, i + HAMPEL_VENTANA), m = z - a + 1;
      for (int j = 0; j < m; j++) w[j] = corregido(a + j);
      std::sort(w, w + m);
      float med = w[m / 2];
      for (int j = 0; j < m; j++) dv[j] = fabsf(w[j] - med);
      std::sort(dv, dv + m);
      float sigma = std::max(HAMPEL_SIGMA_MIN_LMIN, 1.4826f * dv[m / 2]);
      float x = corregido(i);
      limpio[i] = SenalEntrenamiento::a16((fabsf(x - med) > HAMPEL_N_SIGMA * sigma ? med : x) * ESCALA_FLUJO);
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
    double vol = 0.0;
    int iPico = fs.a, iPres = fs.a;
    for (int k = fs.a; k <= fs.z; k++) {
      if (L(k) > L(iPico)) iPico = k;
      if (k > fs.a) vol += 0.5 * (L(k) + L(k - 1)) / 60.0 * (T(k) - T(k - 1));
      if (fs.f * PF(k) > fs.f * PF(iPres)) iPres = k;
    }
    e.flujoPico_Lmin = L(iPico);
    e.tPico_s = (float)T(iPico);
    e.volumen_L = (float)vol;
    e.presionPico_cmH2O = SIGNO_PRESION_INSPIRACION * PF(iPres);
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

    // Caída de la PIM: último tercio contra el primero
    int k = std::max(1, nc / 3);
    double pri = 0, ult = 0;
    for (int i = 0; i < k; i++) { pri += C[i].pim_cmH2O; ult += C[nc - k + i].pim_cmH2O; }
    pri /= k; ult /= k;
    if (pri != 0) out.caida_pim_pct = (float)((ult - pri) / pri * 100.0);
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
  return true;
}

}  // namespace AnalisisEntrenamiento

#endif
