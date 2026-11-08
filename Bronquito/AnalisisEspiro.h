#ifndef ANALISIS_ESPIRO_H
#define ANALISIS_ESPIRO_H

// Detección de UNA espiración y cálculo de volumen DESPUÉS de la adquisición.
// C++ estándar puro (sin hardware): se puede probar en la PC.
//
// El sensor es un venturi: la garganta siempre tiene menos presión que la entrada, venga el aire
// de un lado o del otro. dV = vFlujo_V - vZero es positivo tanto al soplar como al inspirar, así que
// NO indica el sentido del flujo. Por eso la maniobra es SOLO espiratoria: se inhala fuera del equipo.
//
// Idea:
//   1. Se trabaja con el voltaje filtrado dV CON SIGNO y sin zona muerta. El signo no es el sentido
//      del aire: un dV negativo solo puede ser ruido. Integrando signo(dV)*K*sqrt(|dV|), el ruido
//      (que oscila alrededor de cero) se cancela; con fabs se sumaba siempre como flujo.
//   2. El ruido se estima de la propia captura (mediana de |dV|): el umbral se adapta solo.
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
//
// Referencias: Graham BL et al. Standardization of Spirometry 2019 Update. An Official ATS/ERS
// Technical Statement. Am J Respir Crit Care Med 2019;200(8):e70-e88.
// Miller MR et al. Standardisation of spirometry. Eur Respir J 2005;26:319-338 (criterio de 6 s).
#include <math.h>
#include <vector>
#include <algorithm>

// Códigos de resultado de la espirometría. Se guardan en el .json ("codigo_error") y se muestran
// en pantalla como "E<n>". Los números de 1 a 6 son los de la tabla de criterios del proyecto.
enum CodigoEspiro {
  ESPIRO_OK = 0,
  ESPIRO_E1_INICIO_TARDIO     = 1,   // pasaron más de INICIO_MAX_S desde que el equipo estuvo listo hasta t0
  ESPIRO_E2_BEV_ALTO          = 2,   // BEV > max(0,100 L; 5 % del FVC)
  ESPIRO_E3_DURACION_CORTA    = 3,   // espiración (desde t0) más corta que DURACION_MIN_S
  ESPIRO_E4_SIN_MESETA        = 4,   // el volumen cambió más de MESETA_MAX_L en el último segundo
  ESPIRO_E5_FIVC_MAYOR        = 5,   // FIVC > FVC (reservado: necesita la inspiración, sensor PIM)
  ESPIRO_E6_CANCELADA         = 6,   // la canceló el usuario (no se guarda nada)
  ESPIRO_E7_SIN_SOPLIDO       = 7,   // el flujo nunca superó PICO_MIN_LMIN
  ESPIRO_E8_SOPLO_ANTES       = 8,   // empezó a soplar apenas arrancó la captura (o ya estaba soplando)
  ESPIRO_E9_NO_TERMINO        = 9,   // la captura terminó antes de que el flujo volviera a reposo
  ESPIRO_E10_SIN_VOLUMEN      = 10   // soplido con menos de VOLUMEN_MIN_L
};

// Texto corto para la pantalla y el monitor serie
inline const char *textoCodigoEspiro(int c) {
  switch (c) {
    case ESPIRO_OK:                return "Maniobra exitosa";
    case ESPIRO_E1_INICIO_TARDIO:  return "Tardó más de 3 s en comenzar";
    case ESPIRO_E2_BEV_ALTO:       return "Inicio vacilante (BEV alto)";
    case ESPIRO_E3_DURACION_CORTA: return "Soplido muy corto (< 6 s)";
    case ESPIRO_E4_SIN_MESETA:     return "Sin meseta final";
    case ESPIRO_E5_FIVC_MAYOR:     return "Inspiración previa incompleta";
    case ESPIRO_E6_CANCELADA:      return "Maniobra cancelada";
    case ESPIRO_E7_SIN_SOPLIDO:    return "No se detectó el soplido";
    case ESPIRO_E8_SOPLO_ANTES:    return "Sopló antes de la señal";
    case ESPIRO_E9_NO_TERMINO:     return "No terminó antes del final";
    case ESPIRO_E10_SIN_VOLUMEN:   return "Soplido sin volumen suficiente";
    default:                    return "Error desconocido";
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
  float volUltimoSeg_L;              // volumen exhalado en el último segundo (criterio de meseta)
  float fivc_L, fiv1_L;              // inspiración: NAN hasta que vuelva el sensor PIM
  float offset_V;                    // corrimiento del cero respecto de la tara (se resta antes de analizar)
  float ruido_V, umbral_V;           // ruido estimado de la señal y umbral usado (para depurar)
  float factorBTPS;                  // factor aplicado a flujos y volúmenes (1 = sin corregir)
};

namespace AnalisisEspiro {
  const float PICO_MIN_LMIN   = 120.0f;  // PEF mínimo para considerar que hubo un soplido
  const float N_SIGMA         = 3.0f;    // umbral de inicio/fin = N_SIGMA * ruido
  const float UMBRAL_MIN_V    = 0.002f;  // límites del umbral (por si el ruido es muy chico o muy grande)
  const float UMBRAL_MAX_V    = 0.020f;
  const int   MEDIA_MOVIL     = 2;       // detección sobre una media de 2*MEDIA_MOVIL+1 muestras (~50 ms)
  const float QUIETO_FIN_S    = 0.10f;   // el soplido termina cuando el flujo queda bajo el umbral esto
  const float RETROCESO_MAX_S = 0.15f;   // hacia atrás: máximo que se sigue por la curva, más allá del umbral, buscando el cero
  const float AVANCE_MAX_S    = 2.00f;   // hacia adelante: ídem (la cola de la espiración es lenta y larga)
  const int   CEROS_SEGUIDOS  = 3;       // hacia adelante, el cruce por cero cuenta con 3 muestras suavizadas <= 0 seguidas
  const float REPOSO_MIN_S    = 0.20f;   // tiene que haber al menos este reposo ANTES del soplido...
  const float FIN_MIN_S       = 0.30f;   // ...y este DESPUÉS, antes de que termine la captura
  const float VOLUMEN_MIN_L   = 0.10f;
  const float DT_MAX_S        = 0.05f;   // un dt mayor se considera un error de tiempo y se usa el nominal
  const float OFFSET_MAX_V    = 0.015f;  // corrección máxima del cero de la tara

  // Criterios de aceptación
  const float INICIO_MAX_S    = 3.0f;    // E1: desde que el equipo está listo (t = 0 de la captura) hasta t0
  const float BEV_MIN_L       = 0.100f;  // E2: BEV <= max(BEV_MIN_L, BEV_FRACCION * FVC)  (ATS/ERS 2019)
  const float BEV_FRACCION    = 0.05f;
  const float DURACION_MIN_S  = 6.0f;    // E3: tiempo de espiración mínimo (ATS/ERS 2005)
  const float MESETA_MAX_L    = 0.025f;  // E4: cambio de volumen máximo en el último segundo (ATS/ERS 2019)
  // E3 y E4 dependen de ver la cola de la espiración, que hoy se pierde en el ruido: con este
  // interruptor en false no se exigen (pero se calculan y se informan por el monitor serie).
  const bool  VALIDAR_FIN_ESPIRACION = false;
}

// M necesita: tiempo_s, vFlujo_V, flujo_Lmin, volumen_L, fase
//   fase: -1 = inspiración según el sensor de presión (PIM). Sin ese sensor nunca vale -1.
// factorBTPS multiplica todos los flujos y volúmenes (1 = sin corregir).
// Al terminar, en el buffer:
//   - maniobra válida: flujo_Lmin = flujo con signo y sin zona muerta DENTRO del evento, 0 fuera;
//     volumen_L = 0 antes del inicio, la integral durante el soplido y CONSTANTE (= FVC) después.
//   - maniobra inválida: flujo_Lmin = flujo completo (para poder ver qué pasó); si se encontró el
//     soplido, volumen_L igual que arriba; si no, 0.
template <class M>
EventoEspiratorio analizarEventoEspiratorio(std::vector<M> &b, float vZero, float kFlujo, float dtNominal,
                                            float factorBTPS = 1.0f) {
  using namespace AnalisisEspiro;
  EventoEspiratorio ev = {};
  ev.codigo = ESPIRO_E7_SIN_SOPLIDO;
  ev.fivc_L = NAN; ev.fiv1_L = NAN;
  ev.factorBTPS = factorBTPS;
  const int n = (int)b.size();
  if (n < 10) {
    for (int i = 0; i < n; i++) { b[i].flujo_Lmin = 0.0f; b[i].volumen_L = 0.0f; }
    return ev;
  }

  // dV con signo (negativo = ruido, nunca flujo)
  std::vector<float> d(n), q(n), s(n);
  for (int i = 0; i < n; i++) d[i] = b[i].vFlujo_V - vZero;

  // Corrección del cero: la mediana de dV es el reposo real (el soplido ocupa menos de la mitad del
  // tiempo). Si la tara quedó corrida unos mV, sin esto el "ruido" sería siempre positivo y la cola
  // de la espiración sumaría volumen falso. Se limita a OFFSET_MAX_V por si soplaron más de la mitad.
  {
    std::vector<float> tmp(d);
    std::nth_element(tmp.begin(), tmp.begin() + n / 2, tmp.end());
    ev.offset_V = std::min(OFFSET_MAX_V, std::max(-OFFSET_MAX_V, tmp[n / 2]));
  }
  for (int i = 0; i < n; i++) {
    d[i] -= ev.offset_V;
    // Preparado para la inspiración: si el sensor de presión dice que se está inspirando, esa muestra
    // no puede ser parte de la espiración (se la pasa a negativo). Sin sensor PIM nunca ocurre.
    if (b[i].fase < 0) d[i] = -fabsf(d[i]);
    q[i] = (d[i] >= 0.0f ? 1.0f : -1.0f) * kFlujo * sqrtf(fabsf(d[i])) * factorBTPS;   // sin zona muerta
  }
  // Media móvil centrada de dV: para detectar, así un pico suelto de ruido no corta ni arranca el soplido
  for (int i = 0; i < n; i++) {
    int a = std::max(0, i - MEDIA_MOVIL), z = std::min(n - 1, i + MEDIA_MOVIL);
    float suma = 0.0f;
    for (int k = a; k <= z; k++) suma += d[k];
    s[i] = suma / (z - a + 1);
  }

  // Ruido: mediana de |dV| de toda la captura (el soplido ocupa menos de la mitad del tiempo)
  {
    std::vector<float> absd(n);
    for (int i = 0; i < n; i++) absd[i] = fabsf(d[i]);
    std::nth_element(absd.begin(), absd.begin() + n / 2, absd.end());
    ev.ruido_V = absd[n / 2] / 0.6745f;
  }
  const float umbral = std::min(UMBRAL_MAX_V, std::max(UMBRAL_MIN_V, N_SIGMA * ev.ruido_V));
  ev.umbral_V = umbral;

  // Ventana del evento: se calcula y recién al final se escribe en el buffer
  int inicio = -1, fin = -1;

  // 1) La espiración = el máximo de la señal suavizada
  int p = 0;
  for (int i = 1; i < n; i++) if (s[i] > s[p]) p = i;
  if (s[p] <= 0.0f || kFlujo * sqrtf(s[p]) < PICO_MIN_LMIN) ev.codigo = ESPIRO_E7_SIN_SOPLIDO;
  else {
    // 2) Hacia atrás: hasta que cae bajo el umbral, y después por la curva (muestra a muestra) hasta el
    //    cruce por cero. Acá se usa la señal sin suavizar: la media móvil corre el borde hacia atrás.
    int a = p;
    while (a > 0 && s[a - 1] > umbral) a--;
    inicio = a;
    while (inicio > 0 && d[inicio - 1] > 0.0f && (b[a].tiempo_s - b[inicio - 1].tiempo_s) <= RETROCESO_MAX_S) inicio--;

    // 3) Hacia adelante: termina cuando queda bajo el umbral durante QUIETO_FIN_S.
    //    (Si se inspirara por el equipo justo después, el venturi lo vería como más flujo positivo
    //    y se sumaría al FVC: por eso la instrucción es retirar la boquilla para inhalar.)
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

    if (!reposoOk)                                                       ev.codigo = ESPIRO_E8_SOPLO_ANTES;
    else if (!quieto || b[n - 1].tiempo_s - b[fin].tiempo_s < FIN_MIN_S) ev.codigo = ESPIRO_E9_NO_TERMINO;
    else                                                                 ev.codigo = ESPIRO_OK;
  }

  if (ev.codigo != ESPIRO_OK) {
    // No se encontró un soplido analizable: se guarda el flujo completo para poder ver qué pasó, sin volumen
    for (int i = 0; i < n; i++) { b[i].flujo_Lmin = q[i]; b[i].volumen_L = 0.0f; }
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

  if (vol < VOLUMEN_MIN_L) {
    ev.codigo = ESPIRO_E10_SIN_VOLUMEN;
    for (int k = 0; k < n; k++) { b[k].flujo_Lmin = q[k]; b[k].volumen_L = 0.0f; }
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
  ev.volUltimoSeg_L = vol - volumenEn(tFin - 1.0f);

  // 7) Criterios de aceptación (se informa el primero que falla)
  if (t0 - b[0].tiempo_s > INICIO_MAX_S)                                   ev.codigo = ESPIRO_E1_INICIO_TARDIO;
  else if (bev > std::max(BEV_MIN_L, BEV_FRACCION * vol))                  ev.codigo = ESPIRO_E2_BEV_ALTO;
  else if (VALIDAR_FIN_ESPIRACION && ev.duracion_s < DURACION_MIN_S)       ev.codigo = ESPIRO_E3_DURACION_CORTA;
  else if (VALIDAR_FIN_ESPIRACION && ev.volUltimoSeg_L > MESETA_MAX_L)     ev.codigo = ESPIRO_E4_SIN_MESETA;
  else                                                                     ev.codigo = ESPIRO_OK;

  ev.valido = (ev.codigo == ESPIRO_OK);
  if (!ev.valido)   // maniobra que no valida: se guarda el flujo completo, no solo la ventana
    for (int k = 0; k < n; k++) b[k].flujo_Lmin = q[k];
  return ev;
}

#endif
