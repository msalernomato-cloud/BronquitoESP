#include "FlujoHistorial.h"

static const int MAX_HIST = 30;

FlujoHistorial::FlujoHistorial(BronquitoUI &ui, Evaluaciones &evaluaciones) : ui(ui), evaluaciones(evaluaciones) {}

void FlujoHistorial::graficarEvaluacion(const String &id, bool esPim) {
  ui.mostrarMensaje("Cargando señal...");
  SerieEval s;
  if (!evaluaciones.cargarSerie(id, s)) {
    ui.mostrarMensaje("No se pudo graficar", "Falta el archivo de datos", COLOR_ROJO_DEL);
    delay(2000);
    return;
  }
  int n = s.t.size();

  if (esPim) {
    ui.mostrarGrafico("Presión en el tiempo", "Tiempo (s)", "Presión (cmH2O)", s.t.data(), s.p.data(), n);
    return;
  }

  int g = 0;
  while (true) {
    int r = (g == 0)
      ? ui.mostrarGrafico("Volumen en el tiempo", "Tiempo (s)", "Volumen (L)", s.t.data(), s.v.data(), n, "Flujo / Volumen")
      : ui.mostrarGrafico("Flujo en función del volumen", "Volumen (L)", "Flujo (L/min)", s.v.data(), s.q.data(), n, "Volumen / Tiempo");
    if (r == 0) return;
    g ^= 1;
  }
}

void FlujoHistorial::verEvaluacion(const String &id, const String &fecha, const String &tipo) {
  JsonDocument doc;
  if (!evaluaciones.leerResumen(id, doc)) {
    ui.mostrarMensaje("No se pudo leer", "Archivo vacío o dañado", COLOR_ROJO_DEL);
    delay(2000);
    return;
  }

  auto val = [&](const char *clave, int dec) -> String {
    return doc[clave].isNull() ? String("--") : String(doc[clave].as<float>(), dec);
  };

  MetricaItem m[6];
  int n = 0;
  bool esPim = tipo.startsWith("PIM");                // "PIM" o "PIM (E4)"
  bool fallida = !(doc["valida"] | true);             // los archivos viejos no tienen "valida": eran válidos

  if (esPim) {
    m[n++] = metrica("PIM sostenida (1 s)", val("pim_sostenida_cmh2o", 1), "cmH2O");
    m[n++] = metrica("Presión pico", val("pico_cmh2o", 1), "cmH2O");
    int intentos = doc["intentos"].size();
    m[n++] = metrica("Intentos realizados", intentos > 0 ? String(intentos) : String("--"), "");
    m[n++] = metrica("Intento informado", val("intento_informado", 0), "");
  } else {
    float fvc = doc["fvc"] | 0.0f, fev1 = doc["fev1"] | 0.0f;
    bool ratioOk = !doc["fvc"].isNull() && !doc["fev1"].isNull() && fvc > 0.05f;
    m[n++] = metrica("FVC (capacidad vital forzada)", val("fvc", 2), "L");
    m[n++] = metrica("FEV1 (volumen en 1 s)", val("fev1", 2), "L");
    m[n++] = metrica("FEV1 / FVC", ratioOk ? String(100.0f * fev1 / fvc, 1) : String("--"), "%");
    m[n++] = metrica("PEF (flujo pico)", val("pef", 0), "L/min");
    m[n++] = metrica("FIVC (cap. vital inspiratoria)", val("fivc", 2), "L");
    m[n++] = metrica("FIV1 (vol. inspirado en 1 s)", val("fiv1", 2), "L");
  }

  String pie = "";
  if (fallida)
    pie = "Código E" + String(doc["codigo_error"] | 0) + " | " + String(doc["error"] | "");
  else if (!doc["temperatura_c"].isNull() || !doc["humedad_rel_pct"].isNull())
    pie = "Temp: " + val("temperatura_c", 1) + " C | Humedad: " + val("humedad_rel_pct", 0) + " %";

  const char *titulo = esPim ? (fallida ? "PIM fallida" : "Resumen: PIM")
                              : (fallida ? "Espirometría fallida" : "Resumen: Espirometría");
  while (true) {
    int r = ui.mostrarResumenCalibracion(titulo, fecha, m, n, pie);
    if (r != 1) return;
    graficarEvaluacion(id, esPim);
  }
}

void FlujoHistorial::flujoCalibraciones() {
  String ids[MAX_HIST], fechas[MAX_HIST], tipos[MAX_HIST];

  ui.mostrarMensaje("Leyendo tarjeta SD...");
  int n = evaluaciones.listar(ids, fechas, tipos, MAX_HIST);
  if (n < 0) {
    ui.mostrarMensaje("No se pudo leer", "No se detectó la tarjeta SD", COLOR_ROJO_DEL);
    delay(2000);
    return;
  }

  while (true) {
    int sel = ui.elegirCalibracion(fechas, tipos, n);
    if (sel < 0) return;
    verEvaluacion(ids[sel], fechas[sel], tipos[sel]);
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
