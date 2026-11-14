#include "FlujoHistorial.h"

static const int MAX_HIST = 30;

FlujoHistorial::FlujoHistorial(BronquitoUI &ui, TarjetaSD &tarjeta) : ui(ui), tarjeta(tarjeta) {}

void FlujoHistorial::graficarEvaluacion(const String &id, bool esPim) {
  ui.mostrarMensaje("Cargando señal...");
  SerieMedicion s;
  if (!tarjeta.cargarSerie(id, s)) {
    ui.mostrarError("No se pudo graficar", "Falta el archivo de datos");
    return;
  }
  int n = s.t.size();

  if (esPim) {
    ui.mostrarGrafico("Presión en el tiempo", "Tiempo (s)", "Presión (cmH2O)", s.t.data(), s.p.data(), n);
    return;
  }

  // Tres gráficos; el botón pasa al siguiente. Volumen-tiempo: SOLO la espiración (un color, sin
  // inspiración). Flujo-volumen y flujo-tiempo: espiración e inspiración en colores distintos
  // (el signo del flujo dice cuál es cuál: la inspiración es negativa).
  const float *q = s.q.data();
  int g = 0;
  while (true) {
    int r;
    if (g == 0)      r = ui.mostrarGrafico("Volumen espirado en el tiempo", "Tiempo (s)", "Volumen (L)",
                                           s.t.data(), s.ve.data(), n, "Flujo / Volumen");
    else if (g == 1) r = ui.mostrarGrafico("Flujo en función del volumen", "Volumen (L)", "Flujo (L/min)",
                                           s.v.data(), q, n, "Flujo / Tiempo", q);
    else             r = ui.mostrarGrafico("Flujo en el tiempo", "Tiempo (s)", "Flujo (L/min)",
                                           s.t.data(), q, n, "Volumen / Tiempo", q);
    if (r == 0) return;
    g = (g + 1) % 3;
  }
}

// "--" si el valor no está en el archivo
static String texto(float valor, int decimales) {
  return isnan(valor) ? String("--") : String(valor, decimales);
}

void FlujoHistorial::verEvaluacion(const String &id, const String &fecha) {
  ResumenMedicion r;
  if (!tarjeta.leerResumen(id, r)) {
    ui.mostrarError("No se pudo leer", "Archivo vacío o dañado");
    return;
  }

  MetricaItem m[8];
  int n = 0;
  if (r.esPim) {
    m[n++] = metrica("PIM sostenida (1 s)", texto(r.pimSostenida_cmH2O, 1), "cmH2O");
    m[n++] = metrica("Presión pico", texto(r.pico_cmH2O, 1), "cmH2O");
    m[n++] = metrica("Intentos realizados", r.intentos > 0 ? String(r.intentos) : String("--"), "");
    m[n++] = metrica("Intento informado", r.intentoInformado > 0 ? String(r.intentoInformado) : String("--"), "");
  } else {
    bool hayRelacion = !isnan(r.fvc_L) && !isnan(r.fev1_L) && r.fvc_L > 0.05f;
    m[n++] = metrica("FVC (capacidad vital forzada)", texto(r.fvc_L, 2), "L");
    m[n++] = metrica("FEV1 (volumen en 1 s)", texto(r.fev1_L, 2), "L");
    m[n++] = metrica("FEV1 / FVC", hayRelacion ? String(100.0f * r.fev1_L / r.fvc_L, 1) : String("--"), "%");
    m[n++] = metrica("PEF (flujo pico)", texto(r.pef_Lmin, 0), "L/min");
    m[n++] = metrica("FIVC (cap. vital inspiratoria)", texto(r.fivc_L, 2), "L");
    m[n++] = metrica("FIV1 (vol. inspirado en 1 s)", texto(r.fiv1_L, 2), "L");
    m[n++] = metrica("PIF (flujo inspiratorio pico)", texto(r.pif_Lmin, 0), "L/min");
  }

  String pie = "";
  if (!r.valida)
    pie = "Código E" + String(r.codigo) + " | " + r.error;
  else if (!isnan(r.temperatura_c) || !isnan(r.humedad_pct))
    pie = "Temp: " + texto(r.temperatura_c, 1) + " C | Humedad: " + texto(r.humedad_pct, 0) + " %";

  const char *titulo = r.esPim ? (r.valida ? "Resumen: PIM" : "PIM fallida")
                               : (r.valida ? "Resumen: Espirometría" : "Espirometría fallida");
  while (true) {
    int opcion = ui.mostrarResumenCalibracion(titulo, fecha, m, n, pie);
    if (opcion != 1) return;
    graficarEvaluacion(id, r.esPim);
  }
}

void FlujoHistorial::flujoCalibraciones() {
  String ids[MAX_HIST], fechas[MAX_HIST], tipos[MAX_HIST];

  ui.mostrarMensaje("Leyendo tarjeta SD...");
  int n = tarjeta.listarMediciones(ids, fechas, tipos, MAX_HIST);
  if (n < 0) {
    ui.mostrarError("No se pudo leer", "No se detectó la tarjeta SD");
    return;
  }

  int pagina = 0;   // se mantiene entre una evaluación y otra
  while (true) {
    int sel = ui.elegirCalibracion(fechas, tipos, n, pagina);
    if (sel < 0) return;
    verEvaluacion(ids[sel], fechas[sel]);
  }
}

bool FlujoHistorial::ejecutar() {
  while (true) {
    int op = ui.mostrarHistorial();
    if (op < 0) return false;
    if (op == 2) return true;
    if (op == 0) {
      flujoCalibraciones();
    } else {
      ui.mostrarMensaje("Entrenamientos", "Próximamente", BRONQUITO_CYAN);
      delay(1500);
    }
  }
}
