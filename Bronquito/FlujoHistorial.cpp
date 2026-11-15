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

// ---- Entrenamientos ----------------------------------------------------------

// Tarjeta con la etiqueta armada en el momento (ej. "PIM máxima (ciclo 3)")
static MetricaItem metricaTexto(const String &etiqueta, const String &valor, const char *unidad) {
  MetricaItem m;
  m.etiqueta = etiqueta;
  m.valor = valor;
  m.unidad = unidad;
  return m;
}

// "PIF máximo" -> "PIF máximo (ciclo 4)" si se sabe en qué ciclo fue
static String conCiclo(const char *etiqueta, int ciclo) {
  return ciclo > 0 ? String(etiqueta) + " (ciclo " + String(ciclo) + ")" : String(etiqueta);
}

static String textoEstado(const String &estado) {
  if (estado == "completa")     return "Completa";
  if (estado == "incompleta")   return "Incompleta";
  if (estado == "interrumpida") return "Interrumpida";
  return estado.length() ? estado : String("--");
}

static String textoFin(const String &fin) {
  if (fin == "objetivo")        return "Llegó al objetivo de ciclos";
  if (fin == "detenida")        return "La detuvo el usuario";
  if (fin == "sin_respiracion") return "Se cortó: sin respiración";
  if (fin == "tiempo_maximo")   return "Se cortó: tiempo máximo";
  return fin.length() ? fin : String("--");
}

void FlujoHistorial::verEntrenamiento(const String &id, const String &fecha) {
  ResumenEntrenamiento r;
  if (!tarjeta.leerResumenEntrenamiento(id, r)) {
    ui.mostrarMensaje("No se pudo leer", "Archivo vacío o dañado", COLOR_ROJO_DEL);
    delay(2000);
    return;
  }

  String duracion = "--";
  if (!isnan(r.duracion_s)) {
    int seg = (int)lroundf(r.duracion_s);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d:%02d", seg / 60, seg % 60);
    duracion = buf;
  }
  String ciclos = (r.hayFeatures ? String(r.nCiclos) : String("--")) + "/" + String(r.ciclosObjetivo);

  // Todas las features globales del .json ("resumen"), agrupadas en 4 páginas
  const int PAGINAS = 4;
  const String titulos[PAGINAS] = {"Sesión", "Presiones", "Flujo y volumen", "Tiempo y trabajo"};
  MetricaItem m[25];
  int porPagina[PAGINAS];
  int n = 0, k0;

  k0 = n;   // Sesión
  m[n++] = metrica("Ciclos completos", ciclos, "");
  m[n++] = metrica("Duración", duracion, "min");
  m[n++] = metrica("Frecuencia", texto(r.frecuencia_rpm, 1), "rpm");
  m[n++] = metrica("Período medio", texto(r.periodo_s, 2), "s");
  m[n++] = metrica("Ventilación minuto", texto(r.ventilacionMinuto_Lmin, 1), "L/min");
  m[n++] = metrica("Fases sueltas", r.hayFeatures ? String(r.nFasesSueltas) : String("--"), "");
  porPagina[0] = n - k0;

  k0 = n;   // Presiones
  // En el .json la placa da PIM > 0 y PEM < 0; al usuario se le muestran por convención:
  // PIM negativa (inspiración) y PEM positiva (espiración). Solo cambia lo que se ve.
  m[n++] = metrica("PIM promedio", texto(-r.pim_cmH2O, 1), "cmH2O");
  m[n++] = metricaTexto(conCiclo("PIM máxima", r.pimMaxCiclo), texto(-r.pimMax_cmH2O, 1), "cmH2O");
  m[n++] = metrica("PEM promedio", texto(-r.pem_cmH2O, 1), "cmH2O");
  m[n++] = metricaTexto(conCiclo("PEM máxima", r.pemMaxCiclo), texto(-r.pemMax_cmH2O, 1), "cmH2O");
  m[n++] = metrica("Caída de la PIM", texto(r.caidaPim_pct, 1), "%");
  m[n++] = metrica("Pendiente de la PIM (por ciclo)", texto(r.pendientePim, 1), "cmH2O");
  porPagina[1] = n - k0;

  k0 = n;   // Flujo y volumen
  m[n++] = metrica("PIF promedio", texto(r.pif_Lmin, 0), "L/min");
  m[n++] = metricaTexto(conCiclo("PIF máximo", r.pifMaxCiclo), texto(r.pifMax_Lmin, 0), "L/min");
  m[n++] = metrica("PEF promedio", texto(r.pef_Lmin, 0), "L/min");
  m[n++] = metricaTexto(conCiclo("PEF máximo", r.pefMaxCiclo), texto(r.pefMax_Lmin, 0), "L/min");
  m[n++] = metrica("Volumen inspirado promedio", texto(r.volInsp_L, 2), "L");
  m[n++] = metrica("Volumen espirado promedio", texto(r.volEsp_L, 2), "L");
  porPagina[2] = n - k0;

  k0 = n;   // Tiempo y trabajo
  m[n++] = metrica("Ti promedio", texto(r.ti_s, 2), "s");
  m[n++] = metrica("Te promedio", texto(r.te_s, 2), "s");
  m[n++] = metrica("Pausa promedio", texto(r.pausa_s, 2), "s");
  m[n++] = metrica("Ti / Ttot", texto(r.ti_ttot, 2), "");
  m[n++] = metrica("Trabajo inspiratorio promedio", texto(r.trabajoInsp_J, 2), "J");
  m[n++] = metrica("Potencia inspiratoria promedio", texto(r.potenciaInsp_W, 2), "W");
  m[n++] = metrica("Trabajo inspiratorio total", texto(r.trabajoTotalInsp_J, 2), "J");
  porPagina[3] = n - k0;

  String pie = textoEstado(r.estado) + " | " + (r.hayFeatures ? textoFin(r.fin) : String("Sin features"));
  ui.mostrarResumenEntrenamiento(titulos, fecha, m, porPagina, PAGINAS, pie);
}

void FlujoHistorial::flujoEntrenamientos() {
  String ids[MAX_HIST], fechas[MAX_HIST], tipos[MAX_HIST];

  ui.mostrarMensaje("Leyendo tarjeta SD...");
  int n = tarjeta.listarEntrenamientos(ids, fechas, tipos, MAX_HIST);
  if (n < 0) {
    ui.mostrarMensaje("No se pudo leer", "No se detectó la tarjeta SD", COLOR_ROJO_DEL);
    delay(2000);
    return;
  }

  int pagina = 0;   // se mantiene entre un entrenamiento y otro
  while (true) {
    int sel = ui.elegirEntrenamiento(fechas, tipos, n, pagina);
    if (sel < 0) return;
    verEntrenamiento(ids[sel], fechas[sel]);
  }
}

bool FlujoHistorial::ejecutar() {
  while (true) {
    int op = ui.mostrarHistorial();
    if (op < 0) return false;
    if (op == 2) return true;
    if (op == 0) flujoCalibraciones();
    else         flujoEntrenamientos();
  }
}
