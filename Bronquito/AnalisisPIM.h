#ifndef ANALISIS_PIM_H
#define ANALISIS_PIM_H

// Análisis de la Presión Inspiratoria Máxima (PIM) DESPUÉS de la adquisición.
// C++ estándar puro (sin hardware): se puede probar en la PC.
//
// Por intento:
//   1. Se corrige el cero con la mediana de la presión (el esfuerzo ocupa menos de la mitad de la captura).
//   2. El esfuerzo es el tramo alrededor del pico en el que la presión SUAVIZADA (media móvil de
//      SUAVIZADO_S) supera FRACCION_ESFUERZO de su pico; caídas más cortas que HUECO_MAX_S no lo cortan.
//      Así las oscilaciones de la presión durante el esfuerzo no lo parten. Tiene que durar ESFUERZO_MIN_S (1,5 s).
//   3. PIM sostenida = la mayor presión MEDIA en una ventana de exactamente VENTANA_S (1 s) dentro del esfuerzo.
// Por sesión:
//   4. Se hacen hasta INTENTOS_MAX (8) intentos y se corta cuando hay INTENTOS_REPRO (3) reproducibles:
//      (máximo - mínimo) <= TOLERANCIA_REPRO del máximo. Se informa el MAYOR de esos tres.
//
// Referencias: ATS/ERS Statement on Respiratory Muscle Testing. Am J Respir Crit Care Med 2002;166:518-624.
// Laveneziana P et al. ERS statement on respiratory muscle testing at rest and during exercise.
// Eur Respir J 2019;53:1801214.
#include <math.h>
#include <vector>
#include <algorithm>

// Códigos de resultado de la PIM ("codigo_error" en el .json, "E<n>" en pantalla).
// 1 y 2 son los de la tabla de criterios del proyecto.
enum CodigoPIM {
  PIM_RES_OK               = 0,
  PIM_RES_E1_FUGA          = 1,   // reservado: necesita medir flujo y presión a la vez (dos sensores)
  PIM_RES_E2_ESFUERZO_CORTO = 2,  // el esfuerzo no se sostuvo ESFUERZO_MIN_S
  PIM_RES_E3_SIN_ESFUERZO  = 3,   // la presión nunca superó PRESION_MIN_CMH2O
  PIM_RES_E4_NO_REPRODUCIBLE = 4, // (sesión) INTENTOS_MAX intentos sin INTENTOS_REPRO reproducibles
  PIM_RES_E6_CANCELADA     = 6    // la canceló el usuario (no se guarda nada)
};

inline const char *textoCodigoPIM(int c) {
  switch (c) {
    case PIM_RES_OK:                 return "PIM válida";
    case PIM_RES_E1_FUGA:            return "Fuga detectada en boquilla";
    case PIM_RES_E2_ESFUERZO_CORTO:  return "Esfuerzo muy corto (< 1,5 s)";
    case PIM_RES_E3_SIN_ESFUERZO:    return "No se detectó el esfuerzo";
    case PIM_RES_E4_NO_REPRODUCIBLE: return "Sin 3 intentos reproducibles";
    case PIM_RES_E6_CANCELADA:       return "Maniobra cancelada";
    default:                         return "Error desconocido";
  }
}

namespace AnalisisPIM {
  const float PRESION_MIN_CMH2O = 10.0f;   // por debajo de esto no hubo esfuerzo
  // Tolerancia a las oscilaciones (si la PIM sigue dando E2 con un esfuerzo real, aflojar estos tres):
  const float SUAVIZADO_S       = 0.30f;   // media móvil para detectar el esfuerzo (no para calcular la PIM)
  const float FRACCION_ESFUERZO = 0.40f;   // el esfuerzo es lo que supera este % del pico suavizado
  const float HUECO_MAX_S       = 0.30f;   // caídas más cortas que esto no cortan el esfuerzo
  const float ESFUERZO_MIN_S    = 1.5f;    // E2
  const float VENTANA_S         = 1.0f;    // PIM sostenida = mejor media en esta ventana
  const float OFFSET_MAX_CMH2O  = 5.0f;    // corrección máxima del cero de la tara
  const float TOLERANCIA_REPRO  = 0.10f;   // 3 intentos con (máx - mín) <= 10 % del máx

  // ---- MODO PRUEBA (para ver la señal cruda) ----
  // VALIDAR_PIM = false: no se exige ningún criterio (ni duración ni esfuerzo parejo). Cualquier captura se
  // acepta y se guarda; la PIM informada es la mejor media de 1 s de TODA la captura (solo como referencia).
  // Para volver al protocolo: VALIDAR_PIM = true, INTENTOS_REPRO = 3, INTENTOS_MAX = 8.
  const bool  VALIDAR_PIM       = false;
  const int   INTENTOS_REPRO    = 1;
  const int   INTENTOS_MAX      = 1;
}

struct IntentoPIM {
  bool  valido;
  int   codigo;               // CodigoPIM
  float pim_cmH2O;            // mayor presión media sostenida durante VENTANA_S (0 si el esfuerzo duró < 1 s)
  float pico_cmH2O;
  float tInicioEsfuerzo_s, tFinEsfuerzo_s, duracionEsfuerzo_s;
  float tVentana_s;           // inicio de la ventana de 1 s elegida
  float offset_cmH2O;         // corrimiento del cero que se restó
  float signo;                // +1 o -1: sentido de la presión del esfuerzo (la PIM se informa como magnitud)
};

// M necesita: tiempo_s, presion_cmH2O (el esfuerzo puede dar positivo o negativo: se detecta solo).
// Al terminar, presion_cmH2O del buffer queda con el cero corregido.
template <class M>
IntentoPIM analizarIntentoPIM(std::vector<M> &b) {
  using namespace AnalisisPIM;
  IntentoPIM r = {};
  r.codigo = PIM_RES_E3_SIN_ESFUERZO;
  const int n = (int)b.size();
  if (n < 10) return r;

  // 1) Cero: mediana de la presión
  {
    std::vector<float> tmp(n);
    for (int i = 0; i < n; i++) tmp[i] = b[i].presion_cmH2O;
    std::nth_element(tmp.begin(), tmp.begin() + n / 2, tmp.end());
    r.offset_cmH2O = std::min(OFFSET_MAX_CMH2O, std::max(-OFFSET_MAX_CMH2O, tmp[n / 2]));
  }
  for (int i = 0; i < n; i++) b[i].presion_cmH2O -= r.offset_cmH2O;

  // Sentido: según cómo esté conectada la boquilla (P1 o P2), el esfuerzo inspiratorio da presión positiva o
  // negativa. Se toma el sentido de la desviación más grande y se trabaja con la magnitud (pr). El buffer
  // (y el .csv) quedan con el signo tal como se midió.
  {
    float mx = 0.0f, mn = 0.0f;
    for (int i = 0; i < n; i++) { mx = std::max(mx, b[i].presion_cmH2O); mn = std::min(mn, b[i].presion_cmH2O); }
    r.signo = (-mn > mx) ? -1.0f : 1.0f;
  }
  std::vector<float> pr(n);
  for (int i = 0; i < n; i++) pr[i] = r.signo * b[i].presion_cmH2O;

  // 2) Presión suavizada (media móvil centrada de SUAVIZADO_S) para encontrar el esfuerzo
  std::vector<float> s(n);
  {
    float dtMedio = (b[n - 1].tiempo_s - b[0].tiempo_s) / (n - 1);
    int medio = (dtMedio > 0.0f) ? (int)(SUAVIZADO_S / dtMedio / 2.0f + 0.5f) : 0;
    for (int i = 0; i < n; i++) {
      int a = std::max(0, i - medio), z = std::min(n - 1, i + medio);
      float suma = 0.0f;
      for (int k = a; k <= z; k++) suma += pr[k];
      s[i] = suma / (z - a + 1);
    }
  }
  for (int i = 0; i < n; i++) r.pico_cmH2O = std::max(r.pico_cmH2O, pr[i]);   // pico sin suavizar
  int p = 0;
  for (int i = 1; i < n; i++) if (s[i] > s[p]) p = i;
  if (VALIDAR_PIM && s[p] < PRESION_MIN_CMH2O) return r;

  // Tramo de esfuerzo alrededor del pico suavizado
  const float umbral = std::max(PRESION_MIN_CMH2O, FRACCION_ESFUERZO * s[p]);
  int ini = p, fin = p;
  for (int k = p - 1, ultimo = p; k >= 0; k--) {
    if (s[k] >= umbral) { ultimo = k; ini = k; }
    else if (b[ultimo].tiempo_s - b[k].tiempo_s > HUECO_MAX_S) break;
  }
  for (int k = p + 1, ultimo = p; k < n; k++) {
    if (s[k] >= umbral) { ultimo = k; fin = k; }
    else if (b[k].tiempo_s - b[ultimo].tiempo_s > HUECO_MAX_S) break;
  }
  r.tInicioEsfuerzo_s = b[ini].tiempo_s;
  r.tFinEsfuerzo_s = b[fin].tiempo_s;
  r.duracionEsfuerzo_s = r.tFinEsfuerzo_s - r.tInicioEsfuerzo_s;

  // Modo prueba: la ventana de 1 s se busca en TODA la captura y no hay criterios
  if (!VALIDAR_PIM) {
    ini = 0;
    fin = n - 1;
  }

  // 3) Mejor media en una ventana de exactamente VENTANA_S dentro del esfuerzo (integral por trapecios)
  if (b[fin].tiempo_s - b[ini].tiempo_s >= VENTANA_S) {
    std::vector<float> acum(fin - ini + 1, 0.0f);   // integral de la presión desde ini hasta cada muestra
    for (int k = ini + 1; k <= fin; k++)
      acum[k - ini] = acum[k - 1 - ini] +
                      0.5f * (pr[k] + pr[k - 1]) * (b[k].tiempo_s - b[k - 1].tiempo_s);
    auto integralHasta = [&](float t) -> float {   // integral desde ini hasta el instante t (interpolada)
      for (int k = ini + 1; k <= fin; k++) {
        if (b[k].tiempo_s >= t) {
          float ta = b[k - 1].tiempo_s, tb = b[k].tiempo_s;
          float f = (tb > ta) ? (t - ta) / (tb - ta) : 1.0f;
          float pt = pr[k - 1] + f * (pr[k] - pr[k - 1]);
          return acum[k - 1 - ini] + 0.5f * (pr[k - 1] + pt) * (t - ta);
        }
      }
      return acum[fin - ini];
    };
    float mejor = -1e9f;
    for (int i = ini; i <= fin && b[i].tiempo_s + VENTANA_S <= b[fin].tiempo_s; i++) {
      float media = (integralHasta(b[i].tiempo_s + VENTANA_S) - acum[i - ini]) / VENTANA_S;
      if (media > mejor) { mejor = media; r.tVentana_s = b[i].tiempo_s; }
    }
    r.pim_cmH2O = std::max(0.0f, mejor);
  }

  if (!VALIDAR_PIM) r.codigo = PIM_RES_OK;
  else r.codigo = (r.duracionEsfuerzo_s >= ESFUERZO_MIN_S) ? PIM_RES_OK : PIM_RES_E2_ESFUERZO_CORTO;
  r.valido = (r.codigo == PIM_RES_OK);
  return r;
}

// Resultado de una sesión de hasta INTENTOS_MAX intentos
struct ResumenPIM {
  int   nIntentos;
  IntentoPIM intentos[AnalisisPIM::INTENTOS_MAX];
  bool  reproducible;         // hay INTENTOS_REPRO intentos válidos dentro de la tolerancia
  int   idxRepro[AnalisisPIM::INTENTOS_REPRO];
  int   idxReportado;         // intento cuya curva se guarda (-1 si no hubo ninguno)
  int   codigo;               // CodigoPIM de la sesión
  float pim_cmH2O;            // valor informado: el mayor de los reproducibles
};

// Busca INTENTOS_REPRO intentos válidos con (máx - mín) <= TOLERANCIA_REPRO * máx. Si hay varios grupos,
// elige el de valores más altos. Devuelve true y los índices (el primero es el mayor).
inline bool buscarReproducibles(const IntentoPIM *in, int n, int *out) {
  using namespace AnalisisPIM;
  std::vector<int> v;
  for (int i = 0; i < n; i++) if (in[i].valido) v.push_back(i);
  std::sort(v.begin(), v.end(), [&](int a, int b) { return in[a].pim_cmH2O > in[b].pim_cmH2O; });
  for (int i = 0; i + INTENTOS_REPRO - 1 < (int)v.size(); i++) {
    float mx = in[v[i]].pim_cmH2O, mn = in[v[i + INTENTOS_REPRO - 1]].pim_cmH2O;
    if (mx > 0.0f && mx - mn <= TOLERANCIA_REPRO * mx) {
      for (int k = 0; k < INTENTOS_REPRO; k++) out[k] = v[i + k];
      return true;
    }
  }
  return false;
}

// Cuántos intentos válidos "parecidos" hay en el mejor grupo (para mostrar el avance: 1, 2 o 3 de 3)
inline int mayorGrupoReproducible(const IntentoPIM *in, int n) {
  using namespace AnalisisPIM;
  int mejor = 0;
  for (int i = 0; i < n; i++) {
    if (!in[i].valido) continue;
    int c = 0;
    for (int j = 0; j < n; j++)
      if (in[j].valido && in[j].pim_cmH2O <= in[i].pim_cmH2O &&
          in[i].pim_cmH2O - in[j].pim_cmH2O <= TOLERANCIA_REPRO * in[i].pim_cmH2O) c++;
    mejor = std::max(mejor, c);
  }
  return std::min(mejor, INTENTOS_REPRO);
}

// Cierra la sesión: valor informado, intento cuya curva se guarda y código
inline void cerrarSesionPIM(ResumenPIM &r) {
  r.reproducible = buscarReproducibles(r.intentos, r.nIntentos, r.idxRepro);
  r.idxReportado = -1;
  if (r.reproducible) {
    r.idxReportado = r.idxRepro[0];
    r.codigo = PIM_RES_OK;
    r.pim_cmH2O = r.intentos[r.idxReportado].pim_cmH2O;
    return;
  }
  // No reproducible: se guarda la curva del mejor intento válido (o del último, si ninguno fue válido)
  for (int i = 0; i < r.nIntentos; i++)
    if (r.intentos[i].valido && (r.idxReportado < 0 || r.intentos[i].pim_cmH2O > r.intentos[r.idxReportado].pim_cmH2O))
      r.idxReportado = i;
  if (r.idxReportado < 0 && r.nIntentos > 0) r.idxReportado = r.nIntentos - 1;
  r.codigo = PIM_RES_E4_NO_REPRODUCIBLE;
  r.pim_cmH2O = 0.0f;
}

#endif
