#include "TarjetaSD.h"

static const char *ARCHIVO_PERFIL = "/perfil.json";
static const char *ARCHIVO_CONFIG = "/CONFIGURACION.JSON";
static const char *CARPETA_MEDICIONES = "/EVALUACIONES";
static const char *CARPETAS_PRUEBAS[] = {"/EVALUACIONES", "/ENTRENAMIENTOS"};
static const int   MAX_PUNTOS_GRAFICO = 3000;

TarjetaSD::TarjetaSD(uint8_t pinCS, uint8_t pinSCK, uint8_t pinMISO, uint8_t pinMOSI)
  : pinCS(pinCS), pinSCK(pinSCK), pinMISO(pinMISO), pinMOSI(pinMOSI), bus(VSPI) {}

// =============================================================================
// TARJETA Y PACIENTE
// =============================================================================
bool TarjetaSD::montar() {
  if (montada) return true;
  bus.begin(pinSCK, pinMISO, pinMOSI, pinCS);
  montada = SD.begin(pinCS, bus);
  return montada;
}

bool TarjetaSD::hayTarjeta() {
  if (!montada) return false;
  File raiz = SD.open("/");
  bool ok = (bool)raiz;
  if (raiz) raiz.close();
  if (!ok) {                    // la sacaron: se desmonta para poder volver a montarla
    SD.end();
    montada = false;
  }
  return ok;
}

void TarjetaSD::olvidarPaciente(EstadoTarjeta motivo) {
  perfil = PerfilPaciente();     // todos los campos vacíos
  estadoActual = motivo;
}

String TarjetaSD::textoLogin() const {
  if (estadoActual == TARJETA_OK)            return perfil.usuario;
  if (estadoActual == TARJETA_PERFIL_DANADO) return "Perfil dañado";
  return "Sin sincronizar";
}

EstadoTarjeta TarjetaSD::leerPerfil(bool mostrarDetalle) {
  auto informar = [&](const char *texto) {
    if (mostrarDetalle) Serial.println(String("[SD] ") + texto);
  };

  if (!montar()) {
    informar("No se detectó la tarjeta microSD.");
    olvidarPaciente(TARJETA_NO_ESTA);
    return estadoActual;
  }

  File archivo = SD.open(ARCHIVO_PERFIL, FILE_READ);
  if (!archivo) {
    if (!hayTarjeta()) {
      informar("La tarjeta ya no está.");
      olvidarPaciente(TARJETA_NO_ESTA);
    } else {
      informar("No hay /perfil.json: no hay un paciente cargado.");
      olvidarPaciente(TARJETA_SIN_PACIENTE);
    }
    return estadoActual;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, archivo);
  archivo.close();
  if (error) {
    informar("/perfil.json está dañado (JSON inválido).");
    olvidarPaciente(TARJETA_PERFIL_DANADO);
    return estadoActual;
  }

  // Cada clave del archivo va a su campo. Si falta una, queda vacía.
  perfil.usuario     = doc["usuario"]     | "";
  perfil.nombre      = doc["nombre"]      | "";
  perfil.apellido    = doc["apellido"]    | "";
  perfil.pin_version = doc["pin_version"] | "";
  perfil.sexo        = doc["sexo"]        | "";
  perfil.altura      = doc["altura"]      | "";
  perfil.peso        = doc["peso"]        | "";
  perfil.fumador     = doc["fumador"]     | "";
  perfil.nacimiento  = doc["nacimiento"]  | "";
  perfil.registro    = doc["registro"]    | "";
  perfil.enfermedad  = doc["enfermedad"]  | "";
  perfil.pin         = doc["pin"]         | "";

  if (perfil.usuario.length() == 0) {          // un perfil sin usuario no sirve para nada
    informar("/perfil.json no tiene \"usuario\".");
    olvidarPaciente(TARJETA_PERFIL_DANADO);
    return estadoActual;
  }
  estadoActual = TARJETA_OK;
  informar(("Perfil cargado: " + perfil.usuario).c_str());
  return estadoActual;
}

bool TarjetaSD::guardarPerfil(JsonVariantConst perfilWeb) {
  if (!hayTarjeta()) return false;
  static const char *CLAVES[] = {"usuario", "nombre", "apellido", "pin_version", "sexo", "altura",
                                 "peso", "fumador", "nacimiento", "registro", "enfermedad"};
  JsonDocument doc;
  for (const char *clave : CLAVES) doc[clave] = String(perfilWeb[clave] | "");
  String usuario = doc["usuario"] | "";
  if (usuario.length() == 0) return false;
  return escribirJsonSeguro(ARCHIVO_PERFIL, doc);
}

bool TarjetaSD::guardarPin(const String &pin, const String &version) {
  if (!hayTarjeta()) return false;
  JsonDocument doc;
  if (!leerJson(ARCHIVO_PERFIL, doc)) return false;
  doc["pin"] = pin;
  doc["pin_version"] = version;
  if (!escribirJsonSeguro(ARCHIVO_PERFIL, doc)) return false;
  perfil.pin = pin;
  perfil.pin_version = version;
  return true;
}

// =============================================================================
// CONFIGURACIÓN
// =============================================================================
// Pisa solo la clave "frecuencia": lo demás de CONFIGURACION.JSON queda igual.
bool TarjetaSD::guardarFrecuencia(JsonVariantConst frecuencia) {
  if (!hayTarjeta()) return false;
  JsonDocument doc;
  if (!leerJson(ARCHIVO_CONFIG, doc) || !doc.is<JsonObject>()) doc.clear();   // corrupto o inexistente: se rehace
  doc["frecuencia"] = frecuencia;
  return escribirJsonSeguro(ARCHIVO_CONFIG, doc);
}

FrecuenciaGuardada TarjetaSD::leerFrecuencia() {
  FrecuenciaGuardada fr;
  if (!hayTarjeta()) return fr;
  JsonDocument doc;
  if (!leerJson(ARCHIVO_CONFIG, doc)) return fr;

  JsonObjectConst frec = doc["frecuencia"];
  if (frec.isNull()) return fr;
  for (JsonVariantConst d : frec["dias_semana"].as<JsonArrayConst>()) {
    int dia = d | -1;
    if (dia >= 0 && dia < 7) fr.dias[dia] = true;
  }
  fr.sesiones = frec["sesiones_por_semana"] | 0;
  fr.vigenteDesde = String(frec["vigente_desde"] | "");
  return fr;
}

// =============================================================================
// MEDICIONES
// =============================================================================
String TarjetaSD::nuevoIdCalibracion(const String &fechaHora) {
  SD.mkdir(CARPETA_MEDICIONES);
  String digitos = "";
  for (unsigned int i = 0; i < fechaHora.length(); i++)
    if (isDigit(fechaHora[i])) digitos += fechaHora[i];

  if (digitos.length() >= 14 && digitos.substring(0, 4).toInt() >= 2024)
    return "eval_" + digitos.substring(0, 8) + "_" + digitos.substring(8, 14);

  // Sin hora: el primer "eval_SINHORA_n" que no tenga ningún archivo en la tarjeta
  auto usado = [&](const String &id) {
    static const char *SUFIJOS[] = {"_PIM.json", "_PIM.csv", "_ESP.json", "_ESP.csv", ".json", ".csv"};
    String base = String(CARPETA_MEDICIONES) + "/" + id;
    for (const char *s : SUFIJOS)
      if (SD.exists(base + s)) return true;
    for (int c = 1; c <= 10; c++)                       // intentos fallidos de espirometría
      if (SD.exists(base + "_ESP_int1_E" + String(c) + ".json")) return true;
    return false;
  };
  int n = 1;
  String id;
  do { id = "eval_SINHORA_" + String(n++); } while (usado(id));
  return id;
}

bool TarjetaSD::guardarMedicion(const String &id, const String &idCalibracion, const char *tipo,
                                const String &fechaHora, const std::vector<MuestraEspiro> &datos, bool pimEnCero,
                                const EventoEspiratorio *ev, const CondicionesAmbiente *amb, const ResumenPIM *pim) {
  SD.mkdir(CARPETA_MEDICIONES);
  String rutaCsv = rutaMedicion(id, ".csv");
  if (!datos.empty() && !escribirCsv(rutaCsv, datos)) return false;

  JsonDocument doc;
  doc["id_evaluacion"] = id;
  doc["calibracion_id"] = idCalibracion;
  doc["tipo"] = tipo;
  if (fechaHora.length() >= 16) doc["fecha_hora"] = fechaHora;
  doc["sincronizado"] = false;

  // ---- Espirometría ----
  if (ev) {
    doc["valida"] = ev->valido;
    doc["codigo_error"] = ev->codigo;
    if (!ev->valido) doc["error"] = textoCodigoEspiro(ev->codigo);
  }
  if (ev && ev->valido) {
    doc["fvc"]   = roundf(ev->fvc_L * 100.0f) / 100.0f;
    doc["fev1"]  = roundf(ev->fev1_L * 100.0f) / 100.0f;
    doc["pef"]   = roundf(ev->pef_Lmin * 10.0f) / 10.0f;
    doc["bev"]   = roundf(ev->bev_L * 1000.0f) / 1000.0f;
    doc["fet_s"] = roundf(ev->duracion_s * 100.0f) / 100.0f;
    doc["t0_ms"] = lroundf(ev->t0_s * 1000.0f);         // t0 por extrapolación retrógrada
  } else {
    doc["fvc"] = nullptr;
    doc["fev1"] = nullptr;
    doc["pef"] = nullptr;
    if (ev) { doc["bev"] = nullptr; doc["fet_s"] = nullptr; doc["t0_ms"] = nullptr; }
  }
  if (ev && ev->hayEvento) {   // ventana del soplido: la usa el gráfico de volumen (también si no validó)
    doc["evento_inicio_ms"] = lroundf(ev->tInicio_s * 1000.0f);
    doc["evento_fin_ms"]    = lroundf(ev->tFin_s * 1000.0f);
  }
  // ---- Inspiración que sigue a la espiración ----
  if (ev && ev->valido && ev->hayInspiracion) {
    doc["fivc"] = roundf(ev->fivc_L * 100.0f) / 100.0f;
    doc["fiv1"] = roundf(ev->fiv1_L * 100.0f) / 100.0f;
    doc["pif"]  = roundf(ev->pif_Lmin * 10.0f) / 10.0f;
  } else {
    doc["fivc"] = nullptr;
    doc["fiv1"] = nullptr;
    if (ev) doc["pif"] = nullptr;
  }
  if (ev && ev->hayEvento && ev->hayInspiracion) {   // ventana de la inspiración (para el gráfico)
    doc["insp_inicio_ms"] = lroundf(ev->tInicioInsp_s * 1000.0f);
    doc["insp_fin_ms"]    = lroundf(ev->tFinInsp_s * 1000.0f);
  }

  // ---- PIM ----
  if (pim) {
    doc["valida"] = (pim->codigo == PIM_OK);
    doc["codigo_error"] = pim->codigo;
    if (pim->codigo != PIM_OK) doc["error"] = textoCodigoPIM(pim->codigo);
    if (pim->codigo == PIM_OK) doc["pim_sostenida_cmh2o"] = roundf(pim->pim_cmH2O * 10.0f) / 10.0f;
    else                           doc["pim_sostenida_cmh2o"] = nullptr;
    if (pim->idxReportado >= 0) {
      doc["intento_informado"] = pim->idxReportado + 1;   // la curva del .csv es la de este intento
      doc["pico_cmh2o"] = roundf(pim->intentos[pim->idxReportado].pico_cmH2O * 10.0f) / 10.0f;
    }
    JsonArray intentos = doc["intentos"].to<JsonArray>();
    for (int k = 0; k < pim->nIntentos; k++) {
      JsonObject o = intentos.add<JsonObject>();
      o["intento"] = k + 1;
      o["codigo"] = pim->intentos[k].codigo;
      if (pim->intentos[k].pim_cmH2O > 0.0f) o["pim_cmh2o"] = roundf(pim->intentos[k].pim_cmH2O * 10.0f) / 10.0f;
      else                                   o["pim_cmh2o"] = nullptr;
      o["esfuerzo_s"] = roundf(pim->intentos[k].duracionEsfuerzo_s * 100.0f) / 100.0f;
    }
    if (pim->reproducible) {
      JsonArray repro = doc["intentos_reproducibles"].to<JsonArray>();
      for (int k = 0; k < AnalisisPIM::INTENTOS_REPRO; k++) repro.add(pim->idxRepro[k] + 1);
    }
  } else if (pimEnCero) doc["pim_sostenida_cmh2o"] = 0.0f;
  else                  doc["pim_sostenida_cmh2o"] = nullptr;

  // ---- Ambiente y BTPS ----
  if (amb && amb->valida) {
    doc["temperatura_c"]   = roundf(amb->temperatura_c * 10.0f) / 10.0f;
    doc["humedad_rel_pct"] = roundf(amb->humedad_rel_pct);
  } else {
    doc["temperatura_c"] = nullptr;
    doc["humedad_rel_pct"] = nullptr;
  }
  if (ev) {   // null = no se pudo leer el sensor: sin corregir
    if (amb && amb->valida) doc["factor_btps"] = roundf(ev->factorBTPS * 10000.0f) / 10000.0f;
    else                    doc["factor_btps"] = nullptr;
  }

  if (!escribirJsonSeguro(rutaMedicion(id, ".json"), doc, false)) {
    SD.remove(rutaCsv);
    return false;
  }
  Serial.printf("[SD] %s guardada: %d muestras\n", id.c_str(), (int)datos.size());
  return true;
}

// ---- Guardado de cada parte de una calibración (acá se arman los nombres) ----
bool TarjetaSD::guardarPIM(const String &idCal, const String &fechaHora, const ResumenPIM &pim,
                           const CondicionesAmbiente &amb) {
  String id = idCal + "_PIM";
  if (pim.idxReportado >= 0 && !usarCsvTemporalPIM(pim.idxReportado, id))
    Serial.println("[SD] No se pudo mover la curva de la PIM");
  const std::vector<MuestraEspiro> sinDatos;            // el .csv ya está en su lugar
  bool ok = guardarMedicion(id, idCal, "PIM", fechaHora, sinDatos, false, nullptr, &amb, &pim);
  borrarTemporalesPIM();
  return ok;
}

bool TarjetaSD::guardarPIMenCero(const String &idCal, const String &fechaHora, const CondicionesAmbiente &amb) {
  MuestraEspiro cero = {};
  std::vector<MuestraEspiro> datos(2, cero);
  datos[1].tiempo_s = 1.0f;                             // dos puntos en cero para que el gráfico no quede vacío
  return guardarMedicion(idCal + "_PIM", idCal, "PIM", fechaHora, datos, true, nullptr, &amb);
}

bool TarjetaSD::guardarEspirometria(const String &idCal, const String &fechaHora,
                                    const std::vector<MuestraEspiro> &datos, const EventoEspiratorio &ev,
                                    const CondicionesAmbiente &amb) {
  return guardarMedicion(idCal + "_ESP", idCal, "ESPIROMETRIA", fechaHora, datos, false, &ev, &amb);
}

// Ej.: eval_SINHORA_24_ESP_int2_E6 -> calibración 24, segundo intento de espirometría, falló por E6
bool TarjetaSD::guardarEspirometriaFallida(const String &idCal, int intento, const String &fechaHora,
                                           const std::vector<MuestraEspiro> &datos, const EventoEspiratorio &ev,
                                           const CondicionesAmbiente &amb) {
  String id = idCal + "_ESP_int" + String(intento) + "_E" + String(ev.codigo);
  return guardarMedicion(id, idCal, "ESPIROMETRIA", fechaHora, datos, false, &ev, &amb);
}

void TarjetaSD::borrarPIM(const String &idCal)          { borrarMedicion(idCal + "_PIM"); }
void TarjetaSD::borrarEspirometria(const String &idCal) { borrarMedicion(idCal + "_ESP"); }

void TarjetaSD::borrarCalibracion(const String &idCal) {
  // El "_" final evita borrar otra calibración: "eval_SINHORA_2_" no coincide con "eval_SINHORA_25_..."
  String prefijo = idCal + "_";
  int borrados = 0;
  for (const String &ruta : listarArchivos(CARPETA_MEDICIONES)) {
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    if (nombre.startsWith(prefijo) && SD.remove(ruta)) borrados++;
  }
  Serial.printf("[SD] Calibración %s descartada: %d archivos borrados\n", idCal.c_str(), borrados);
}

bool TarjetaSD::guardarCsvTemporalPIM(int intento, const std::vector<MuestraEspiro> &datos) {
  SD.mkdir(CARPETA_MEDICIONES);
  SD.remove(rutaTemporalPIM(intento));
  return escribirCsv(rutaTemporalPIM(intento), datos);
}

bool TarjetaSD::usarCsvTemporalPIM(int intento, const String &id) {
  String destino = rutaMedicion(id, ".csv");
  SD.remove(destino);
  return SD.rename(rutaTemporalPIM(intento), destino);
}

void TarjetaSD::borrarTemporalesPIM() {
  for (int k = 0; k < AnalisisPIM::INTENTOS_MAX; k++) SD.remove(rutaTemporalPIM(k));
}

void TarjetaSD::borrarMedicion(const String &id) {
  SD.remove(rutaMedicion(id, ".csv"));
  SD.remove(rutaMedicion(id, ".json"));
}

// =============================================================================
// HISTORIAL
// =============================================================================

// Clave para ordenar de la más nueva a la más vieja. Comparar los ids como texto no alcanza:
// "eval_SINHORA_9" quedaría antes que "eval_SINHORA_10".
//  - con fecha ("eval_AAAAMMDD_HHMMSS_..."): el texto ya ordena bien (ancho fijo)
//  - sin fecha ("eval_SINHORA_n_..."): se ordena por el número n
// Las que tienen fecha van primero (no se puede saber si son más nuevas que las sin fecha).
static String claveOrden(const String &id) {
  const String prefijo = "eval_SINHORA_";
  if (!id.startsWith(prefijo)) return "2" + id;
  int fin = prefijo.length();
  while (fin < (int)id.length() && isDigit(id[fin])) fin++;
  String numero = id.substring(prefijo.length(), fin);
  while (numero.length() < 8) numero = "0" + numero;
  return "1" + numero + id.substring(fin);
}

int TarjetaSD::listarMediciones(String *ids, String *fechas, String *tipos, int max) {
  if (!hayTarjeta()) return -1;

  // 1) Ids de los .json, ordenados de la más nueva a la más vieja (inserción, quedan las 'max' más nuevas)
  int cantidad = 0;
  for (const String &ruta : listarArchivos(CARPETA_MEDICIONES)) {
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    if (!nombre.endsWith(".json") || !nombre.startsWith("eval_")) continue;
    String id = nombre.substring(0, nombre.length() - 5);

    int pos = cantidad;
    String clave = claveOrden(id);
    while (pos > 0 && clave > claveOrden(ids[pos - 1])) pos--;
    if (pos >= max) continue;
    int fin = (cantidad < max) ? cantidad : max - 1;
    for (int k = fin; k > pos; k--) ids[k] = ids[k - 1];
    ids[pos] = id;
    if (cantidad < max) cantidad++;
  }

  // 2) Tipo y fecha de cada una, para la lista en pantalla
  for (int i = 0; i < cantidad; i++) {
    String tipo = "", fechaHora = "";
    bool valida = true;
    int codigo = 0;
    JsonDocument doc;
    if (leerJson(rutaMedicion(ids[i], ".json"), doc)) {
      tipo      = String(doc["tipo"] | "");
      fechaHora = String(doc["fecha_hora"] | "");
      valida    = doc["valida"] | true;          // los archivos viejos no tienen el campo: eran válidos
      codigo    = doc["codigo_error"] | 0;
    }
    tipo.toUpperCase();
    if (tipo.startsWith("PIM"))          tipos[i] = valida ? String("PIM") : "PIM (E" + String(codigo) + ")";
    else if (!tipo.startsWith("ESPIRO")) tipos[i] = "Desconocido";
    else if (valida)                     tipos[i] = "Espirometría";
    else                                 tipos[i] = "Fallida (E" + String(codigo) + ")";
    fechas[i] = fechaLegible(fechaHora, ids[i]);
  }
  return cantidad;
}

bool TarjetaSD::leerResumen(const String &id, ResumenMedicion &r) {
  JsonDocument doc;
  if (!leerJson(rutaMedicion(id, ".json"), doc)) return false;
  auto numero = [&](const char *clave) -> float { return doc[clave].isNull() ? NAN : doc[clave].as<float>(); };

  String tipo = String(doc["tipo"] | "");
  tipo.toUpperCase();
  r = ResumenMedicion();
  r.esPim  = tipo.startsWith("PIM");
  r.valida = doc["valida"] | true;                   // los archivos viejos no tienen el campo: eran válidos
  r.codigo = doc["codigo_error"] | 0;
  r.error  = String(doc["error"] | "");
  r.fvc_L    = numero("fvc");
  r.fev1_L   = numero("fev1");
  r.pef_Lmin = numero("pef");
  r.fivc_L   = numero("fivc");
  r.fiv1_L   = numero("fiv1");
  r.pif_Lmin = numero("pif");
  r.pimSostenida_cmH2O = numero("pim_sostenida_cmh2o");
  r.pico_cmH2O         = numero("pico_cmh2o");
  r.intentos           = doc["intentos"].size();
  r.intentoInformado   = doc["intento_informado"] | 0;
  r.temperatura_c = numero("temperatura_c");
  r.humedad_pct   = numero("humedad_rel_pct");
  return true;
}

bool TarjetaSD::cargarSerie(const String &id, SerieMedicion &serie) {
  // Ventanas de la espiración y de la inspiración guardadas con las métricas: solo se integra dentro
  // de ellas. Sin ventanas (archivos viejos, PIM) se integra todo.
  float inicioMs = -1, finMs = -1, inspInicioMs = -1, inspFinMs = -1;
  {
    JsonDocument doc;
    if (leerJson(rutaMedicion(id, ".json"), doc) &&
        !doc["evento_inicio_ms"].isNull() && !doc["evento_fin_ms"].isNull()) {
      inicioMs = doc["evento_inicio_ms"].as<float>();
      finMs    = doc["evento_fin_ms"].as<float>();
      if (!doc["insp_inicio_ms"].isNull() && !doc["insp_fin_ms"].isNull()) {
        inspInicioMs = doc["insp_inicio_ms"].as<float>();
        inspFinMs    = doc["insp_fin_ms"].as<float>();
      }
    }
  }
  bool conVentana = inicioMs >= 0;
  auto integrar = [&](float tMs) {
    if (!conVentana) return true;
    return (tMs >= inicioMs && tMs <= finMs) || (inspInicioMs >= 0 && tMs >= inspInicioMs && tMs <= inspFinMs);
  };

  File f = SD.open(rutaMedicion(id, ".csv"), FILE_READ);
  if (!f) return false;

  auto esEspiracion = [&](float tMs) { return conVentana && tMs >= inicioMs && tMs <= finMs; };

  float volumen = 0, volumenEsp = 0, tAnteriorMs = -1;
  while (f.available() && (int)serie.t.size() < MAX_PUNTOS_GRAFICO) {
    String linea = f.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0 || !(isDigit(linea[0]) || linea[0] == '-')) continue;   // encabezado o línea vacía

    int c1 = linea.indexOf(',');
    int c2 = linea.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;

    float tMs = linea.substring(0, c1).toFloat();
    float p   = linea.substring(c1 + 1, c2).toFloat();
    int c3 = linea.indexOf(',', c2 + 1);                   // los archivos nuevos tienen más columnas
    float q   = (c3 < 0 ? linea.substring(c2 + 1) : linea.substring(c2 + 1, c3)).toFloat();

    if (tAnteriorMs >= 0 && integrar(tMs))
      volumen += q * (tMs - tAnteriorMs) / 60000.0f;   // L/min · ms -> L (la inspiración, negativa, lo hace bajar)
    if (tAnteriorMs >= 0 && (esEspiracion(tMs) || (!conVentana && q > 0)))
      volumenEsp += q * (tMs - tAnteriorMs) / 60000.0f; // solo espiración: después queda fijo en la FVC
    tAnteriorMs = tMs;

    serie.t.push_back(tMs / 1000.0f);
    serie.p.push_back(p);
    serie.q.push_back(q);
    serie.v.push_back(volumen);
    serie.ve.push_back(volumenEsp);
  }
  f.close();
  return serie.t.size() >= 2;
}

// =============================================================================
// MANTENIMIENTO
// =============================================================================
int TarjetaSD::contarPendientes() {
  if (!hayTarjeta()) return 0;
  JsonDocument filtro;
  filtro["sincronizado"] = true;       // solo se lee esa clave de cada archivo

  int pendientes = 0;
  for (const char *carpeta : CARPETAS_PRUEBAS) {
    for (const String &ruta : listarArchivos(carpeta)) {
      String r = ruta;
      r.toLowerCase();
      if (!r.endsWith(".json")) continue;
      File f = SD.open(ruta, FILE_READ);
      if (!f) { pendientes++; continue; }
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
      f.close();
      if (error || !(doc["sincronizado"] | false)) pendientes++;
    }
  }
  return pendientes;
}

ResultadoFormato TarjetaSD::borrarTodo(bool forzar) {
  montar();
  if (!hayTarjeta()) return FORMATO_SIN_TARJETA;

  int pendientes = contarPendientes();
  if (!forzar && pendientes > 0) {
    Serial.printf("[SD] No se borró nada: hay %d mediciones sin subir\n", pendientes);
    return FORMATO_HAY_PENDIENTES;
  }

  bool ok = true;
  for (const char *ruta : {ARCHIVO_PERFIL, ARCHIVO_CONFIG})
    if (SD.exists(ruta) && !SD.remove(ruta)) ok = false;
  for (const char *carpeta : CARPETAS_PRUEBAS)
    for (const String &ruta : listarArchivos(carpeta))
      if (!SD.remove(ruta)) ok = false;

  olvidarPaciente(TARJETA_SIN_PACIENTE);
  Serial.println(ok ? "[SD] Tarjeta borrada" : "[SD] No se pudo borrar todo");
  return ok ? FORMATO_OK : FORMATO_ERROR;
}

// =============================================================================
// LECTURA Y ESCRITURA DE ARCHIVOS
// =============================================================================
bool TarjetaSD::leerJson(const String &ruta, JsonDocument &doc) {
  File f = SD.open(ruta, FILE_READ);
  if (!f) return false;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  return !error;
}

// Escribe primero <ruta>.tmp y recién después lo renombra: un corte de luz a mitad
// de camino deja el archivo viejo entero, nunca uno a medio escribir.
bool TarjetaSD::escribirJsonSeguro(const String &ruta, JsonDocument &doc, bool legible) {
  String temporal = ruta + ".tmp";
  SD.remove(temporal);
  File f = SD.open(temporal, FILE_WRITE);
  if (!f) return false;
  size_t escritos = legible ? serializeJsonPretty(doc, f) : serializeJson(doc, f);
  f.close();
  if (escritos == 0) { SD.remove(temporal); return false; }
  SD.remove(ruta);
  return SD.rename(temporal, ruta);
}

bool TarjetaSD::escribirCsv(const String &ruta, const std::vector<MuestraEspiro> &datos) {
  File f = SD.open(ruta, FILE_WRITE);
  if (!f) return false;
  // Las dos últimas columnas son las tensiones filtradas que entregan los sensores (para diagnóstico).
  // Van al final para que quien lea solo las tres primeras siga funcionando igual.
  f.println("tiempo_ms,presion_cmh2o,flujo_lmin,v_flujo_v,v_pim_v");
  char linea[80];
  for (const auto &m : datos) {
    snprintf(linea, sizeof(linea), "%ld,%.2f,%.2f,%.4f,%.4f", lroundf(m.tiempo_s * 1000.0f), m.presion_cmH2O,
             m.flujo_Lmin, m.vFlujo_V, m.vPIM_V);
    f.println(linea);
  }
  f.close();
  return true;
}

std::vector<String> TarjetaSD::listarArchivos(const char *carpeta) {
  std::vector<String> rutas;
  File dir = SD.open(carpeta);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return rutas; }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      String nombre = String(f.name());
      nombre = nombre.substring(nombre.lastIndexOf('/') + 1);   // según la versión, name() trae o no la ruta
      rutas.push_back(String(carpeta) + "/" + nombre);
    }
    f.close();
  }
  dir.close();
  return rutas;
}

String TarjetaSD::rutaMedicion(const String &id, const char *extension) {
  return String(CARPETA_MEDICIONES) + "/" + id + extension;
}

String TarjetaSD::rutaTemporalPIM(int intento) {
  return String(CARPETA_MEDICIONES) + "/_tmp_pim_" + String(intento + 1) + ".csv";
}

// "02/11/2026 10:24" a partir de la fecha_hora del .json o, si no está, del id
String TarjetaSD::fechaLegible(const String &fechaHora, const String &id) {
  if (fechaHora.length() >= 16 && fechaHora[4] == '-' && fechaHora[7] == '-')
    return fechaHora.substring(8, 10) + "/" + fechaHora.substring(5, 7) + "/" + fechaHora.substring(0, 4) +
           " " + fechaHora.substring(11, 16);

  int g = id.indexOf('_');
  if (g >= 0 && (int)id.length() >= g + 16 && isDigit(id[g + 1]))
    return id.substring(g + 7, g + 9) + "/" + id.substring(g + 5, g + 7) + "/" + id.substring(g + 1, g + 5) +
           " " + id.substring(g + 10, g + 12) + ":" + id.substring(g + 12, g + 14);
  return "Sin fecha";
}
