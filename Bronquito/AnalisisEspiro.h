#ifndef ANALISIS_ESPIRO_H
#define ANALISIS_ESPIRO_H

// Detección de la espiración forzada y de la inspiración que le sigue, y cálculo de volúmenes,
// DESPUÉS de la adquisición. C++ estándar puro (sin hardware): se puede probar en la PC.
//
// El sensor es un venturi: la garganta siempre tiene menos presión que la entrada, venga el aire
// de un lado o del otro. dV = vFlujo_V - vZero es positivo tanto al soplar como al inspirar, así que
// NO indica el sentido del flujo. El sentido lo da el sensor de presión de la boquilla (con la placa
// actual: presión positiva = inspiración, negativa = espiración; ver SIGNO_PRESION_ESPIRACION).
// Se decide por EVENTO (todo el tramo con flujo toma el
// signo del promedio de la presión), así el ruido de la presión no lo cambia a mitad de un soplido.
//
// Idea:
//   1. Se trabaja con el voltaje filtrado dV CON SIGNO y sin zona muerta. El signo no es el sentido
//      del aire: un dV negativo solo puede ser ruido. Integrando signo(dV)*K*sqrt(|dV|), el ruido
//      (que oscila alrededor de cero) se cancela; con fabs se sumaba siempre como flujo.
//   2. El cero y el ruido se estiman de la propia captura (mediana y MAD descartando los soplidos):
//      el umbral se adapta solo.
//   3. La espiración es la de mayor flujo de los 15 s (si hubo otros soplidos, se ignoran).
//   4. Desde el pico se camina hacia atrás hasta que el flujo cae bajo el umbral y, desde ahí,
//      se sigue retrocediendo por la curva hasta el cruce por cero (inicio de la integración).
//      Lo mismo hacia adelante.
//   5. Se integra solo entre inicio y fin. Fuera de esa ventana el flujo queda en 0 y el
//      volumen constante (= FVC): nada de lo que pase antes o después suma volumen.
//   6. t0 por extrapolación retrógrada (ATS/ERS 2019): recta tangente a la curva volumen-tiempo en el
//      PEF, con pendiente = PEF; donde corta el eje del tiempo es t0. El volumen exhalado antes de t0
//      es el BEV. FEV1 y todos los tiempos se miden desde ese t0.
//   7. Validación de la maniobra con los códigos de CodigoEspiro (tabla de criterios del proyecto).
//   8. Inspiración: el pico de sentido contrario más alto que empieza DESPUÉS de la espiración, dentro de
//      los INSP_DEMORA_MAX_S siguientes a la última muestra espiratoria. Se detecta con el mismo algoritmo (desde el
//      pico hacia atrás hasta el cruce por cero, y hacia adelante hasta el reposo). Se calculan FIVC,
//      FIV1 (desde su propio t0 extrapolado) y PIF. No se exige un tamaño mínimo ni que la curva cierre.
//
// Referencia: Graham BL et al. Standardization of Spirometry 2019 Update. An Official ATS/ERS
// Technical Statement. Am J Respir Crit Care Med 2019;200(8):e70-e88.
#include <math.h>
#include <vector>
#include <algorithm>

// Códigos de resultado de la espirometría (tabla de criterios del proyecto).
// Se guardan en el .json ("codigo_error") y se muestran en pantalla como "E<n>".
enum CodigoEspiro {
  ESPIRO_OK               = 0,
  ESPIRO_E1_INICIO_TARDIO = 1,   // más de 5 s desde que el equipo estuvo listo hasta t0           (activo)
  ESPIRO_E2_BEV_ALTO      = 2,   // BEV mayor a 0,1 L o al 5 % del FVC (el mayor de los dos)         (activo)
  ESPIRO_E3_FIVC_MAYOR    = 3,   // FIVC mayor que FVC: necesita medir la inspiración                 (NO activo)
  ESPIRO_E4_CANCELADA     = 4,   // la canceló el usuario (no se guarda nada)                         (activo)
  ESPIRO_E5_SIN_SOPLIDO   = 5,   // PEF menor a 120 L/min                                             (activo)
  ESPIRO_E6_SOPLO_ANTES   = 6,   // no hubo reposo antes del soplido (sopló antes de la señal)        (activo)
  ESPIRO_E7_NO_TERMINO    = 7,   // no hubo reposo al final de la captura                             (activo)
  ESPIRO_E8_SIN_VOLUMEN   = 8    // volumen menor a 0,1 L                                             (activo)
};

// Texto corto para la pantalla y el monitor serie
inline const char *textoCodigoEspiro(int c) {
  switch (c) {
    case ESPIRO_OK:               return "Maniobra exitosa";
    case ESPIRO_E1_INICIO_TARDIO: return "Tardó más de 5 s en comenzar";
    case ESPIRO_E2_BEV_ALTO:      return "Inicio vacilante (BEV alto)";
    case ESPIRO_E3_FIVC_MAYOR:    return "Inspiración previa incompleta";
    case ESPIRO_E4_CANCELADA:     return "Maniobra cancelada";
    case ESPIRO_E5_SIN_SOPLIDO:   return "No se detectó el soplido";
    case ESPIRO_E6_SOPLO_ANTES:   return "Sopló antes de la señal";
    case ESPIRO_E7_NO_TERMINO:    return "No terminó antes del final";
    case ESPIRO_E8_SIN_VOLUMEN:   return "Soplido sin volumen suficiente";
    default:                      return "Error desconocido";
  }
}

struct EventoEspiratorio {
  bool  valido;                      // pasó todas las validaciones (codigo == ESPIRO_OK)
  bool  hayEvento;                   // se encontró un soplido y se calcularon las métricas (aunque no valide)
  int   codigo;                      // CodigoEspiro
  int   idxInicio, idxFin, idxPef;   // índices en el buffer (ventana integrada y PEF)
  float tInicio_s;                   // inicio de la integración (cruce por cero de la curva)
  float t0_s;                        // t0 por extrapolación retrógrada: desde acá se miden los tiempos
  float tFin_s, duracion_s;          // fin del soplido y tiempo de espiración (FET = tFin - t0)
  float fvc_L, fev1_L;               // volumen total y a 1 s de t0
  float pef_Lmin, tPef_s;            // flujo pico y cuánto tardó en alcanzarlo desde t0
  float bev_L;                       // volumen extrapolado retrógradamente (exhalado antes de t0)
  // Inspiración que sigue a la espiración (NAN / false si no se detectó)
  bool  hayInspiracion;
  int   idxInicioInsp, idxFinInsp, idxPif;
  float tInicioInsp_s, t0Insp_s, tFinInsp_s;   // inicio de la integración, t0 extrapolado y fin
  float fivc_L, fiv1_L;              // volumen inspirado total y a 1 s de su t0
  float pif_Lmin;                    // flujo inspiratorio pico (positivo)
  float offset_V;                    // corrimiento del cero respecto de la tara (se resta antes de analizar)
  float ruido_V, umbral_V;           // ruido estimado de la señal y umbral usado (para depurar)
  float factorBTPS;                  // factor aplicado a flujos y volúmenes (1 = sin corregir)
};

namespace AnalisisEspiro {
  // ---- MODO PRUEBA ----
  // false = sin validaciones: no se rechaza ninguna maniobra (no hay E1, E2, E5 por pico bajo, E6, E7
  // ni E8) y la inspiración se busca sin el límite de 3 s. Fuera de la espiración y de la inspiración
  // el flujo queda en cero (no se ve el ruido). Poner en true para volver a validar.
  const bool  VALIDAR         = false;

  // ---- SENTIDO DE LA PRESIÓN ----
  // Signo que tiene la presión de la boquilla al ESPIRAR. En la placa actual las tomas del sensor de
  // presión quedaron ruteadas al revés: la presión manométrica da POSITIVA al inspirar (la PIM sale
  // positiva) y NEGATIVA al espirar. Convención: presión > 0 = inspiración, presión < 0 = espiración.
  // Si se corrige la placa, poner +1. (RespiratorySensors usa esta misma constante.)
  const float SIGNO_PRESION_ESPIRACION = -1.0f;

  const float PICO_MIN_LMIN   = 120.0f;  // E5: PEF mínimo para considerar que hubo un soplido
  const float N_SIGMA         = 3.0f;    // umbral de inicio/fin = N_SIGMA * ruido
  const float UMBRAL_MIN_V    = 0.002f;  // límites del umbral (por si el ruido es muy chico o muy grande)
  const float UMBRAL_MAX_V    = 0.020f;
  const int   MEDIA_MOVIL     = 2;       // detección sobre una media de 2*MEDIA_MOVIL+1 muestras (~50 ms)
  const float QUIETO_FIN_S    = 0.10f;   // el soplido termina cuando el flujo queda bajo el umbral esto
  const float RETROCESO_MAX_S = 0.15f;   // hacia atrás: máximo que se sigue por la curva, más allá del umbral, buscando el cero
  const float AVANCE_MAX_S    = 2.00f;   // hacia adelante: ídem (la cola de la espiración es lenta y larga)
  const int   CEROS_SEGUIDOS  = 3;       // hacia adelante, el cruce por cero cuenta con 3 muestras suavizadas <= 0 seguidas
  const float REPOSO_MIN_S    = 0.20f;   // E6: tiene que haber al menos este reposo ANTES del soplido...
  const float FIN_MIN_S       = 0.30f;   // E7: ...y este DESPUÉS, antes de que termine la captura
  const float VOLUMEN_MIN_L   = 0.10f;   // E8
  const float DT_MAX_S        = 0.05f;   // un dt mayor se considera un error de tiempo y se usa el nominal
  const float OFFSET_MAX_V    = 0.015f;  // corrección máxima del cero de la tara

  // Criterios de aceptación
  const float INSP_DEMORA_MAX_S = 3.0f;
  const float PROFUNDIDAD_CORTE  = 0.30f; // espiración e inspiración pegadas: se cortan en un valle de flujo
                                          // menor al 30 % del pico más chico de los dos lados
  const int   TRAMO_MIN_MUESTRAS = 3;     // un tramo sobre el umbral más corto que esto (30 ms) es ruido
  const float VECINO_MAX_S       = 0.50f; // un tramo de flujo muy bajo hereda el sentido del vecino a menos de esto  // la inspiración tiene que empezar, como mucho, esto después del fin de la espiración
  const float INICIO_MAX_S    = 5.0f;    // E1: desde que aparece "Exhale e inhale con fuerza" (t = 0 de la captura) hasta t0 extrapolado
  const float BEV_MIN_L       = 0.100f;  // E2: BEV <= max(BEV_MIN_L, BEV_FRACCION * FVC)  (ATS/ERS 2019)
  const float BEV_FRACCION    = 0.05f;
}

// M necesita: tiempo_s, vFlujo_V, presion_cmH2O, flujo_Lmin, volumen_L, fase
//   El sentido del flujo sale de presion_cmH2O (se decide por evento, no por muestra).
//   Al terminar, fase queda con el sentido asignado: +1 espiración, -1 inspiración, 0 reposo.
// kEspiracion / kInspiracion: constante del venturi en cada sentido (Q = K * sqrt(dV)).
// factorBTPS multiplica todos los flujos y volúmenes (1 = sin corregir).
// Al terminar, en el buffer:
//   - maniobra válida: flujo_Lmin = flujo con signo (inspiración negativa) y sin zona muerta DENTRO de
//     la espiración y de la inspiración, 0 fuera; volumen_L = 0 antes del inicio, sube hasta la FVC
//     durante la espiración y baja durante la inspiración (FVC - volumen inspirado).
//   - maniobra inválida: flujo_Lmin = flujo completo (para poder ver qué pasó); si se encontró el
//     soplido, volumen_L igual que arriba; si no, 0.
template <class M>
EventoEspiratorio analizarEventoEspiratorio(std::vector<M> &b, float vZero, float kEspiracion,
                                            float kInspiracion, float dtNominal, float factorBTPS = 1.0f) {
  using namespace AnalisisEspiro;
  EventoEspiratorio ev = {};
  ev.codigo = ESPIRO_E5_SIN_SOPLIDO;
  ev.fivc_L = NAN; ev.fiv1_L = NAN; ev.pif_Lmin = NAN;
  ev.t0Insp_s = NAN; ev.tInicioInsp_s = NAN; ev.tFinInsp_s = NAN;
  ev.idxInicioInsp = ev.idxFinInsp = ev.idxPif = -1;
  ev.factorBTPS = factorBTPS;
  const int n = (int)b.size();
  if (n < 10) {
    for (int i = 0; i < n; i++) { b[i].flujo_Lmin = 0.0f; b[i].volumen_L = 0.0f; }
    return ev;
  }

  const int n2 = 2 * MEDIA_MOVIL + 1;   // muestras de la media móvil
  auto mediaMovil = [&](const std::vector<float> &x, std::vector<float> &y) {
    for (int i = 0; i < n; i++) {
      int a = std::max(0, i - MEDIA_MOVIL), z = std::min(n - 1, i + MEDIA_MOVIL);
      float suma = 0.0f;
      for (int k = a; k <= z; k++) suma += x[k];
      y[i] = suma / (z - a + 1);
    }
  };
  // Nivel de reposo y ruido de una señal: mediana y desvío robusto (MAD), descartando de a poco lo que
  // se aleja más de 3 desvíos (los soplidos). Así no hace falta saber de antemano dónde está el reposo.
  auto reposoYRuido = [](const std::vector<float> &x, float &centro, float &sigma) {
    std::vector<float> v(x);
    for (int it = 0; it < 5; it++) {
      std::vector<float> tmp(v);
      std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
      centro = tmp[tmp.size() / 2];
      for (float &e : tmp) e = fabsf(e - centro);
      std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
      sigma = tmp[tmp.size() / 2] / 0.6745f;
      std::vector<float> dentro;
      for (float e : x) if (fabsf(e - centro) <= 3.0f * sigma) dentro.push_back(e);
      if (dentro.size() < 20 || dentro.size() == v.size()) break;
      v.swap(dentro);
    }
  };

  // ---- a) Flujo: el venturi da la MAGNITUD (dV positivo en los dos sentidos) ----
  std::vector<float> d(n), q(n), s(n), ms(n);
  for (int i = 0; i < n; i++) d[i] = b[i].vFlujo_V - vZero;
  {
    float centro, sigma;
    reposoYRuido(d, centro, sigma);
    // Corrección del cero: si la tara quedó corrida unos mV, sin esto el ruido sumaría volumen falso
    ev.offset_V = std::min(OFFSET_MAX_V, std::max(-OFFSET_MAX_V, centro));
    ev.ruido_V = sigma;
  }
  for (int i = 0; i < n; i++) d[i] -= ev.offset_V;
  mediaMovil(d, ms);
  const float umbral = std::min(UMBRAL_MAX_V, std::max(UMBRAL_MIN_V, N_SIGMA * ev.ruido_V));
  ev.umbral_V = umbral;

  // ---- b) Presión de la boquilla: da el SENTIDO. Se multiplica por SIGNO_PRESION_ESPIRACION para que,
  //         de acá en adelante, pr > 0 sea espiración y pr < 0 inspiración (el buffer no se toca). ----
  std::vector<float> pr(n), pf(n);
  float pBase, pRuido;
  for (int i = 0; i < n; i++) pr[i] = SIGNO_PRESION_ESPIRACION * b[i].presion_cmH2O;
  reposoYRuido(pr, pBase, pRuido);
  for (int i = 0; i < n; i++) pr[i] -= pBase;
  mediaMovil(pr, pf);

  // ---- c) Eventos: tramos con flujo sobre el umbral. Cada evento lleva UN solo sentido para todo
  //         el tramo: el del promedio de la presión pesado por el flujo. Como la presión crece con el
  //         cuadrado del flujo, los momentos de flujo bajo (donde la presión es casi solo ruido) casi no
  //         pesan, y el ruido no cambia el signo a mitad de un soplido.
  //         Si la espiración y la inspiración vienen pegadas (sin pausa), el flujo igual pasa por casi
  //         cero al darse vuelta: el tramo se corta en ese mínimo, pero solo si los dos lados tienen
  //         presiones de signo contrario.
  std::vector<int> signo(n, 0);
  std::vector<int> segIni, segFin, segSig;
  auto pesoPresion = [&](int a, int z) {
    float w = 0.0f;
    for (int k = a; k <= z; k++) w += pf[k] * std::max(ms[k], 0.0f);
    return w;
  };
  // Divide [a, z] en el mínimo de flujo más profundo si separa presiones de signo contrario (recursivo)
  std::vector<std::pair<int, int>> pendientes, tramos;
  for (int i = 0; i < n;) {
    if (ms[i] <= umbral) { i++; continue; }
    int a = i;
    while (i < n && ms[i] > umbral) i++;
    if (i - a >= TRAMO_MIN_MUESTRAS) pendientes.push_back({a, i - 1});   // un pico suelto de ruido no es un evento
  }
  while (!pendientes.empty()) {
    auto [a, z] = pendientes.back();
    pendientes.pop_back();
    int mejor = -1;
    float mejorRel = PROFUNDIDAD_CORTE;
    for (int k = a + 1; k < z; k++) {
      if (!(ms[k] <= ms[k - 1] && ms[k] <= ms[k + 1])) continue;          // mínimo local
      float izq = 0.0f, der = 0.0f;
      for (int j = a; j < k; j++) izq = std::max(izq, ms[j]);
      for (int j = k + 1; j <= z; j++) der = std::max(der, ms[j]);
      float rel = ms[k] / std::max(1e-6f, std::min(izq, der));            // qué tan hondo es el valle
      if (rel < mejorRel && (pesoPresion(a, k - 1) >= 0.0f) != (pesoPresion(k, z) >= 0.0f)) { mejorRel = rel; mejor = k; }
    }
    if (mejor < 0) tramos.push_back({a, z});
    else { pendientes.push_back({a, mejor - 1}); pendientes.push_back({mejor, z}); }
  }
  std::sort(tramos.begin(), tramos.end());
  // Sentido de cada tramo. Si la presión del tramo no se distingue del ruido (flujo muy bajo: la cola de
  // un soplido que quedó separada por un instante bajo el umbral), toma el sentido del tramo vecino
  // más cercano, si está a menos de VECINO_MAX_S; si no hay vecino, queda como espiración.
  const float ruidoPf = pRuido / sqrtf((float)n2);   // ruido de la presión ya suavizada
  std::vector<int> sgT(tramos.size(), 0);
  for (size_t e = 0; e < tramos.size(); e++) {
    float w = pesoPresion(tramos[e].first, tramos[e].second), e2 = 0.0f;
    for (int k = tramos[e].first; k <= tramos[e].second; k++) e2 += ms[k] * ms[k];
    if (fabsf(w) > 3.0f * ruidoPf * sqrtf(e2)) sgT[e] = (w >= 0.0f) ? 1 : -1;
  }
  for (size_t e = 0; e < tramos.size(); e++) {
    if (sgT[e] != 0) continue;
    float mejorDist = VECINO_MAX_S;
    int sg = 1;
    for (size_t o = 0; o < tramos.size(); o++) {
      if (o == e || sgT[o] == 0) continue;
      float dist = (o < e) ? b[tramos[e].first].tiempo_s - b[tramos[o].second].tiempo_s
                           : b[tramos[o].first].tiempo_s - b[tramos[e].second].tiempo_s;
      if (dist >= 0.0f && dist < mejorDist) { mejorDist = dist; sg = sgT[o]; }
    }
    sgT[e] = sg;
  }
  for (size_t e = 0; e < tramos.size(); e++) {
    for (int k = tramos[e].first; k <= tramos[e].second; k++) signo[k] = sgT[e];
    segIni.push_back(tramos[e].first); segFin.push_back(tramos[e].second); segSig.push_back(sgT[e]);
  }
  // El sentido de cada evento se extiende un poco antes y después (sin pisar a otro evento), para que
  // el backtracking y la cola sigan la curva hasta el cruce por cero con el signo correcto.
  for (size_t e = 0; e < segIni.size(); e++)
    for (int k = segIni[e] - 1; k >= 0 && signo[k] == 0 && b[segIni[e]].tiempo_s - b[k].tiempo_s <= RETROCESO_MAX_S; k--)
      signo[k] = segSig[e];
  for (size_t e = 0; e < segIni.size(); e++)
    for (int k = segFin[e] + 1; k < n && signo[k] == 0 && b[k].tiempo_s - b[segFin[e]].tiempo_s <= AVANCE_MAX_S; k++)
      signo[k] = segSig[e];

  // ---- d) dV con signo y flujo: positivo = espiración, negativo = inspiración, sin zona muerta ----
  for (int i = 0; i < n; i++) {
    if (signo[i] < 0) d[i] = -d[i];
    b[i].fase = signo[i];
    q[i] = (d[i] >= 0.0f ? kEspiracion : -kInspiracion) * sqrtf(fabsf(d[i])) * factorBTPS;
  }
  // Media móvil centrada de dV con signo: para detectar, así un pico suelto de ruido no corta ni arranca el soplido
  mediaMovil(d, s);

  // Ventana del evento: se calcula y recién al final se escribe en el buffer
  int inicio = -1, fin = -1;

  // 1) La espiración = el máximo de la señal suavizada
  int p = 0;
  for (int i = 1; i < n; i++) if (s[i] > s[p]) p = i;
  if (s[p] <= 0.0f || (VALIDAR && kEspiracion * sqrtf(s[p]) < PICO_MIN_LMIN)) ev.codigo = ESPIRO_E5_SIN_SOPLIDO;
  else {
    // 2) Hacia atrás: hasta que cae bajo el umbral, y después por la curva (muestra a muestra) hasta el
    //    cruce por cero. Acá se usa la señal sin suavizar: la media móvil corre el borde hacia atrás.
    int a = p;
    while (a > 0 && s[a - 1] > umbral) a--;
    inicio = a;
    while (inicio > 0 && d[inicio - 1] > 0.0f && (b[a].tiempo_s - b[inicio - 1].tiempo_s) <= RETROCESO_MAX_S) inicio--;

    // 3) Hacia adelante: termina cuando queda bajo el umbral durante QUIETO_FIN_S.
    //    (La inspiración que sigue ya viene en negativo por la presión, así que corta la espiración.)
    int ultimo = p, j = p;
    bool quieto = false;
    for (; j < n; j++) {
      if (s[j] > umbral) ultimo = j;
      else if (b[j].tiempo_s - b[ultimo].tiempo_s >= QUIETO_FIN_S) { quieto = true; break; }
    }
    //    Después se sigue por la cola en la señal SUAVIZADA hasta CEROS_SEGUIDOS muestras <= 0 seguidas:
    //    una muestra de ruido negativa sola no la corta, y la integración con signo cancela el ruido.
    fin = ultimo;
    for (int k = ultimo + 1, ceros = 0; k < n && (b[k].tiempo_s - b[ultimo].tiempo_s) <= AVANCE_MAX_S; k++) {
      if (s[k] <= 0.0f) { if (++ceros >= CEROS_SEGUIDOS) break; }
      else              { ceros = 0; fin = k; }
    }

    // 4) Validaciones: reposo antes (sin soplido previo pegado) y vuelta a reposo antes del final
    bool reposoOk = (b[inicio].tiempo_s - b[0].tiempo_s) >= REPOSO_MIN_S;
    for (int k = inicio - 1; reposoOk && k >= 0 && (b[inicio].tiempo_s - b[k].tiempo_s) <= REPOSO_MIN_S; k--)
      if (s[k] > umbral) reposoOk = false;

    if (!VALIDAR)                                                        ev.codigo = ESPIRO_OK;
    else if (!reposoOk)                                                  ev.codigo = ESPIRO_E6_SOPLO_ANTES;
    else if (!quieto || b[n - 1].tiempo_s - b[fin].tiempo_s < FIN_MIN_S) ev.codigo = ESPIRO_E7_NO_TERMINO;
    else                                                                 ev.codigo = ESPIRO_OK;
  }

  if (ev.codigo != ESPIRO_OK) {
    // No se pudo analizar: sin volumen. El flujo queda solo en la ventana del soplido (si se encontró);
    // el resto en cero. Para diagnosticar, el .csv guarda también las tensiones de los sensores.
    for (int i = 0; i < n; i++) {
      b[i].flujo_Lmin = (inicio >= 0 && i >= inicio && i <= fin) ? q[i] : 0.0f;
      b[i].volumen_L = 0.0f;
    }
    return ev;
  }

  // 5) Integrar SOLO entre inicio y fin (trapecios), y PEF dentro de la ventana
  float vol = 0.0f, pef = 0.0f;
  int idxPef = inicio;
  for (int k = 0; k < n; k++) {
    bool dentro = (k >= inicio && k <= fin);
    b[k].flujo_Lmin = dentro ? q[k] : 0.0f;
    if (dentro && k > inicio) {
      float dt = b[k].tiempo_s - b[k - 1].tiempo_s;
      if (dt <= 0.0f || dt > DT_MAX_S) dt = dtNominal;
      vol += 0.5f * (q[k] + q[k - 1]) / 60.0f * dt;
    }
    if (dentro && q[k] > pef) { pef = q[k]; idxPef = k; }
    b[k].volumen_L = vol;   // 0 antes, integral durante, constante después
  }

  if (VALIDAR && vol < VOLUMEN_MIN_L) {
    ev.codigo = ESPIRO_E8_SIN_VOLUMEN;
    for (int k = 0; k < n; k++) b[k].volumen_L = 0.0f;   // el flujo ya quedó solo en la ventana
    return ev;
  }

  // Volumen en un instante cualquiera (interpolado); 0 antes del inicio y FVC después del fin
  auto volumenEn = [&](float t) -> float {
    if (t <= b[inicio].tiempo_s) return 0.0f;
    if (t >= b[fin].tiempo_s) return vol;
    for (int k = inicio + 1; k <= fin; k++) {
      if (b[k].tiempo_s >= t) {
        float ta = b[k - 1].tiempo_s, tb = b[k].tiempo_s;
        float f = (tb > ta) ? (t - ta) / (tb - ta) : 1.0f;
        return b[k - 1].volumen_L + f * (b[k].volumen_L - b[k - 1].volumen_L);
      }
    }
    return vol;
  };

  // 6) t0 por extrapolación retrógrada: tangente en el PEF con pendiente = PEF (en L/s)
  float tPef = b[idxPef].tiempo_s;
  float t0 = tPef - b[idxPef].volumen_L / (pef / 60.0f);
  float bev = volumenEn(t0);
  float tFin = b[fin].tiempo_s;

  ev.hayEvento = true;
  ev.idxInicio = inicio; ev.idxFin = fin; ev.idxPef = idxPef;
  ev.tInicio_s = b[inicio].tiempo_s;
  ev.t0_s = t0;
  ev.tFin_s = tFin;
  ev.duracion_s = tFin - t0;
  ev.fvc_L = vol;
  ev.fev1_L = volumenEn(t0 + 1.0f);
  ev.pef_Lmin = pef;
  ev.tPef_s = tPef - t0;
  ev.bev_L = bev;

  // 7) Inspiración: el pico de sentido contrario más alto que sigue a la espiración
  {
    // Señal inspiratoria en positivo: así se usa la misma lógica que en la espiración
    auto si = [&](int k) { return -s[k]; };
    // a) De los tramos inspiratorios que empiezan después de la espiración, y a menos de
    //    INSP_DEMORA_MAX_S de la última muestra espiratoria, el de pico más alto.
    int elegido = -1;
    float mejorPico = 0.0f;
    for (size_t e = 0; e < segIni.size(); e++) {
      if (segSig[e] > 0 || segIni[e] <= fin) continue;
      // La espiración termina en la última muestra que todavía es espiración con flujo sobre el umbral:
      // la cola lenta puede quedar bajo el umbral, pero sigue siendo espiración.
      float tUlt = tFin;
      for (int k2 = segIni[e] - 1; k2 > fin; k2--)
        if (signo[k2] > 0 && fabsf(d[k2]) > umbral) { tUlt = b[k2].tiempo_s; break; }
      if (VALIDAR && b[segIni[e]].tiempo_s - tUlt > INSP_DEMORA_MAX_S) continue;
      float pico = 0.0f;
      for (int k2 = segIni[e]; k2 <= segFin[e]; k2++) pico = std::max(pico, si(k2));
      if (pico > mejorPico) { mejorPico = pico; elegido = (int)e; }
    }
    if (elegido >= 0) {
      // b) pico de esa inspiración y su último instante sobre el umbral
      int pi = segIni[elegido], ultimoI = segFin[elegido];
      for (int k2 = segIni[elegido]; k2 <= segFin[elegido]; k2++) if (si(k2) > si(pi)) pi = k2;
      // c) desde el pico hacia atrás hasta el cruce por cero (sin meterse en la espiración)
      int a2 = pi;
      while (a2 > fin + 1 && si(a2 - 1) > umbral) a2--;
      int ii = a2;
      while (ii > fin + 1 && d[ii - 1] < 0.0f && (b[a2].tiempo_s - b[ii - 1].tiempo_s) <= RETROCESO_MAX_S) ii--;
      // d) hacia adelante por la cola hasta el cruce por cero (o el final de la captura)
      int fi = ultimoI;
      for (int k2 = ultimoI + 1, ceros = 0; k2 < n && (b[k2].tiempo_s - b[ultimoI].tiempo_s) <= AVANCE_MAX_S; k2++) {
        if (si(k2) <= 0.0f) { if (++ceros >= CEROS_SEGUIDOS) break; }
        else                { ceros = 0; fi = k2; }
      }

      {
        // Volumen inspirado acumulado (positivo) y PIF
        std::vector<float> vi(n, 0.0f);
        float volI = 0.0f, pif = 0.0f;
        int idxPif = ii;
        for (int k2 = ii; k2 < n; k2++) {
          bool dentro = (k2 <= fi);
          if (dentro && k2 > ii) {
            float dt = b[k2].tiempo_s - b[k2 - 1].tiempo_s;
            if (dt <= 0.0f || dt > DT_MAX_S) dt = dtNominal;
            volI += 0.5f * (-q[k2] - q[k2 - 1]) / 60.0f * dt;
          }
          if (dentro && -q[k2] > pif) { pif = -q[k2]; idxPif = k2; }
          vi[k2] = volI;
          b[k2].flujo_Lmin = dentro ? q[k2] : 0.0f;
          b[k2].volumen_L  = vol - volI;   // la curva volumen-tiempo baja durante la inspiración
        }
        auto volumenInspEn = [&](float t) -> float {
          if (t <= b[ii].tiempo_s) return 0.0f;
          if (t >= b[fi].tiempo_s) return volI;
          for (int k2 = ii + 1; k2 <= fi; k2++) {
            if (b[k2].tiempo_s >= t) {
              float ta = b[k2 - 1].tiempo_s, tb = b[k2].tiempo_s;
              float f = (tb > ta) ? (t - ta) / (tb - ta) : 1.0f;
              return vi[k2 - 1] + f * (vi[k2] - vi[k2 - 1]);
            }
          }
          return volI;
        };
        // t0 de la inspiración: misma extrapolación retrógrada que en la espiración
        float t0i = (pif > 0.0f) ? b[idxPif].tiempo_s - vi[idxPif] / (pif / 60.0f) : b[ii].tiempo_s;

        ev.hayInspiracion = true;
        ev.idxInicioInsp = ii; ev.idxFinInsp = fi; ev.idxPif = idxPif;
        ev.tInicioInsp_s = b[ii].tiempo_s;
        ev.t0Insp_s = t0i;
        ev.tFinInsp_s = b[fi].tiempo_s;
        ev.fivc_L = volI;
        ev.fiv1_L = volumenInspEn(t0i + 1.0f);
        ev.pif_Lmin = pif;
      }
    }
  }

  // 8) Criterios de aceptación (se informa el primero que falla). E3 (FIVC > FVC) no se evalúa todavía.
  if (!VALIDAR)                                            ev.codigo = ESPIRO_OK;
  else if (t0 - b[0].tiempo_s > INICIO_MAX_S)             ev.codigo = ESPIRO_E1_INICIO_TARDIO;
  else if (bev > std::max(BEV_MIN_L, BEV_FRACCION * vol)) ev.codigo = ESPIRO_E2_BEV_ALTO;
  else                                                    ev.codigo = ESPIRO_OK;

  ev.valido = (ev.codigo == ESPIRO_OK);
  return ev;   // fuera de la espiración y de la inspiración el flujo queda en cero, aunque no valide
}

#endif
