#ifndef NIVEL_RESISTENCIA_H
#define NIVEL_RESISTENCIA_H

// Nivel de resistencia automático a partir de la evaluación. C++ estándar puro (sin hardware): se
// puede probar en la PC.
//
//   %PIM = PIM medida / PIM predicha × 100      %FVC = FVC medida / FVC predicha × 100
//   S = 0,6·%PIM + 0,4·%FVC                     (S = 100: persona promedio para su sexo, edad y talla)
//
//   Grupo  Puntaje S          Descripción                  Nivel de la válvula
//   1      S < 60             Debilidad marcada            1 (más abierta)
//   2      60 ≤ S < 80        Por debajo de lo esperado    2
//   3      80 ≤ S < 100       Dentro de lo esperado        3
//   4      100 ≤ S < 120      Por encima de lo esperado    4
//   5      S ≥ 120            Atleta                       5 (más cerrada)
//   Para el grupo 5 se exige además %PIM ≥ 120 y %FVC ≥ 100 (si no, queda en el 4): así no se
//   clasifica como atleta a quien tiene solo una de las dos capacidades por encima de lo esperado.
//
// Los predichos los manda la web (GLI Global 2022 para la FVC, Evans y Whitelaw para la PIM).
// La evaluación vence a los VIGENCIA_DIAS días.

#include <cmath>
#include <cstdio>

namespace NivelResistencia {

const int    VIGENCIA_DIAS = 15;
const double PESO_PIM = 0.6, PESO_FVC = 0.4;

inline double puntajeS(double pctPim, double pctFvc) { return PESO_PIM * pctPim + PESO_FVC * pctFvc; }

// Nivel 1 a 5 (0 si S no se puede calcular)
inline int nivel(double s, double pctPim, double pctFvc) {
  if (std::isnan(s)) return 0;
  if (s < 60.0)  return 1;
  if (s < 80.0)  return 2;
  if (s < 100.0) return 3;
  if (s < 120.0) return 4;
  return (pctPim >= 120.0 && pctFvc >= 100.0) ? 5 : 4;
}

// ---- Fechas "AAAA-MM-DD" (también sirve "AAAA-MM-DD HH:MM:SS": se mira solo la fecha) ----
// Días desde el 1/1/1970 (algoritmo de días civiles). -1 si no tiene ese formato.
inline long diaDelCalendario(const char *f) {
  if (!f) return -1;
  int a, m, d;
  if (std::sscanf(f, "%4d-%2d-%2d", &a, &m, &d) != 3) return -1;
  if (a < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  a -= (m <= 2);
  long era = a / 400;
  long anioEra = a - era * 400;
  long diaAnio = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long diaEra = anioEra * 365 + anioEra / 4 - anioEra / 100 + diaAnio;
  return era * 146097 + diaEra - 719468;
}

// Lo inverso: escribe "AAAA-MM-DD" en buf (al menos 11 bytes)
inline void fechaDelDia(long dias, char *buf) {
  dias += 719468;
  long era = dias / 146097;
  long diaEra = dias - era * 146097;
  long anioEra = (diaEra - diaEra / 1460 + diaEra / 36524 - diaEra / 146096) / 365;
  long diaAnio = diaEra - (365 * anioEra + anioEra / 4 - anioEra / 100);
  long mp = (5 * diaAnio + 2) / 153;
  int d = (int)(diaAnio - (153 * mp + 2) / 5 + 1);
  int m = (int)(mp < 10 ? mp + 3 : mp - 9);
  long a = anioEra + era * 400 + (m <= 2);
  std::snprintf(buf, 11, "%04ld-%02d-%02d", a, m, d);
}

}  // namespace NivelResistencia

#endif
