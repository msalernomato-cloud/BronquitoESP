#include "Evaluaciones.h"

static const char *DIR_EVAL = "/EVALUACIONES";
static const int MAX_PUNTOS = 3000;

Evaluaciones::Evaluaciones(BronquitoStorage &storage) : storage(storage) {}

String Evaluaciones::generarId(const String &fechaHora) {
  SD.mkdir(DIR_EVAL);
  String hora = fechaHora;
  String d = "";
  for (unsigned int i = 0; i < hora.length(); i++)
    if (isDigit(hora[i])) d += hora[i];

  if (d.length() >= 14 && d.substring(0, 4).toInt() >= 2024)
    return "eval_" + d.substring(0, 8) + "_" + d.substring(8, 14);

  // ¿Ya hay algún archivo de la calibración <id>? (también los intentos fallidos <id>_ESP_int1_E<c>)
  auto usado = [&](const String &id) {
    String base = String(DIR_EVAL) + "/" + id;
    for (int c = 1; c <= 10; c++)
      if (SD.exists(base + "_ESP_int1_E" + String(c) + ".json")) return true;
    return false;
  };
  int i = 1;
  String id;
  do { id = "eval_SINHORA_" + String(i++); } while (usado(id) || SD.exists(String(DIR_EVAL) + "/" + id + "_PIM.json") ||
                                                  SD.exists(String(DIR_EVAL) + "/" + id + "_PIM.csv") ||
                                                  SD.exists(String(DIR_EVAL) + "/" + id + "_ESP.json") ||
                                                  SD.exists(String(DIR_EVAL) + "/" + id + "_ESP.csv") ||
                                                  SD.exists(String(DIR_EVAL) + "/" + id + ".json") ||
                                                  SD.exists(String(DIR_EVAL) + "/" + id + ".csv"));
  return id;
}

String Evaluaciones::fechaLegible(const String &fechaHora, const String &id) {
  if (fechaHora.length() >= 16 && fechaHora[4] == '-' && fechaHora[7] == '-')
    return fechaHora.substring(8, 10) + "/" + fechaHora.substring(5, 7) + "/" + fechaHora.substring(0, 4) +
           " " + fechaHora.substring(11, 16);

  int g = id.indexOf('_');
  if (g >= 0 && (int)id.length() >= g + 16 && isDigit(id[g + 1]))
    return id.substring(g + 7, g + 9) + "/" + id.substring(g + 5, g + 7) + "/" + id.substring(g + 1, g + 5) +
           " " + id.substring(g + 10, g + 12) + ":" + id.substring(g + 12, g + 14);
  return "Sin fecha";
}

// Clave para ordenar el historial de la más nueva a la más vieja. Comparar los ids como texto no sirve:
// "eval_SINHORA_9" quedaba antes que "eval_SINHORA_10" y, con más de 'max' archivos, se perdían las más nuevas.
//  - con fecha ("eval_AAAAMMDD_HHMMSS_..."): el texto ya ordena bien (ancho fijo)
//  - sin fecha ("eval_SINHORA_n_..."): se ordena por el número n
// Las que tienen fecha van primero (no se puede saber si son más nuevas que las sin fecha).
static String claveOrden(const String &id) {
  const String pre = "eval_SINHORA_";
  if (!id.startsWith(pre)) return "2" + id;
  int fin = pre.length();
  while (fin < (int)id.length() && isDigit(id[fin])) fin++;
  String num = id.substring(pre.length(), fin);
  while (num.length() < 8) num = "0" + num;
  return "1" + num + id.substring(fin);
}

int Evaluaciones::listar(String *ids, String *fechas, String *tipos, int max) {
  if (!storage.tarjetaPresente()) return -1;

  File dir = SD.open(DIR_EVAL);
  if (!dir || !dir.isDirectory()) return 0;

  int cnt = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String nombre = String(f.name());
    bool esDir = f.isDirectory();
    f.close();
    if (esDir) continue;

    nombre = nombre.substring(nombre.lastIndexOf('/') + 1);
    if (!nombre.endsWith(".json") || !nombre.startsWith("eval_")) continue;
    String id = nombre.substring(0, nombre.length() - 5);

    int pos = cnt;
    String clave = claveOrden(id);
    while (pos > 0 && clave > claveOrden(ids[pos - 1])) pos--;
    if (pos >= max) continue;
    int fin = (cnt < max) ? cnt : max - 1;
    for (int k = fin; k > pos; k--) ids[k] = ids[k - 1];
    ids[pos] = id;
    if (cnt < max) cnt++;
  }
  dir.close();

  for (int i = 0; i < cnt; i++) {
    String tipo = "", fh = "";
    bool valida = true;
    int codigo = 0;
    JsonDocument doc;
    File f = SD.open(String(DIR_EVAL) + "/" + ids[i] + ".json", FILE_READ);
    if (f) {
      if (!deserializeJson(doc, f)) {
        tipo = String(doc["tipo"] | "");
        fh   = String(doc["fecha_hora"] | "");
        valida = doc["valida"] | true;          // los archivos viejos no tienen el campo: eran válidos
        codigo = doc["codigo_error"] | 0;
      }
      f.close();
    }
    tipo.toUpperCase();
    if (tipo.startsWith("PIM"))         tipos[i] = valida ? String("PIM") : "PIM (E" + String(codigo) + ")";
    else if (!tipo.startsWith("ESPIRO")) tipos[i] = "Desconocido";
    else if (valida)                    tipos[i] = "Espirometría";
    else                                tipos[i] = "Fallida (E" + String(codigo) + ")";
    fechas[i] = fechaLegible(fh, ids[i]);
  }
  return cnt;
}

bool Evaluaciones::leerResumen(const String &id, JsonDocument &doc) {
  File f = SD.open(String(DIR_EVAL) + "/" + id + ".json", FILE_READ);
  DeserializationError err = DeserializationError::InvalidInput;
  if (f) { err = deserializeJson(doc, f); f.close(); }
  return !err;
}

bool Evaluaciones::cargarSerie(const String &id, SerieEval &s) {
  // Ventana del evento espiratorio guardada junto con las métricas. Sin ella (archivos viejos, PIM) se integra todo.
  float evIniMs = -1, evFinMs = -1;
  {
    JsonDocument dj;
    File fj = SD.open(String(DIR_EVAL) + "/" + id + ".json", FILE_READ);
    if (fj) {
      if (!deserializeJson(dj, fj) && !dj["evento_inicio_ms"].isNull() && !dj["evento_fin_ms"].isNull()) {
        evIniMs = dj["evento_inicio_ms"].as<float>();
        evFinMs = dj["evento_fin_ms"].as<float>();
      }
      fj.close();
    }
  }
  bool conVentana = evIniMs >= 0;

  File f = SD.open(String(DIR_EVAL) + "/" + id + ".csv", FILE_READ);
  if (!f) return false;

  float vol = 0, tPrevMs = -1;
  while (f.available() && (int)s.t.size() < MAX_PUNTOS) {
    String linea = f.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0 || !(isDigit(linea[0]) || linea[0] == '-')) continue;

    int c1 = linea.indexOf(',');
    int c2 = linea.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;

    float tMs = linea.substring(0, c1).toFloat();
    float p   = linea.substring(c1 + 1, c2).toFloat();
    float q   = linea.substring(c2 + 1).toFloat();

    if (tPrevMs >= 0 && (!conVentana || (tMs >= evIniMs && tMs <= evFinMs))) vol += q * (tMs - tPrevMs) / 60000.0f;
    tPrevMs = tMs;

    s.t.push_back(tMs / 1000.0f);
    s.p.push_back(p);
    s.q.push_back(q);
    s.v.push_back(vol);
  }
  f.close();
  return s.t.size() >= 2;
}

bool Evaluaciones::guardarManiobra(const String &id, const String &calibId, const char *tipo, const String &fechaHora,
                                   const std::vector<MuestraEspiro> &datos, bool pimEnCero, const EventoEspiratorio *ev,
                                   const CondicionesAmbiente *amb, const ResumenPIM *pim) {
  String base = String(DIR_EVAL) + "/" + id;

  if (!datos.empty() && !escribirCsv(base + ".csv", datos)) return false;

  JsonDocument doc;
  doc["id_evaluacion"] = id;
  doc["calibracion_id"] = calibId;
  doc["tipo"] = tipo;
  if (fechaHora.length() >= 16) doc["fecha_hora"] = fechaHora;
  doc["sincronizado"] = false;
  if (ev) {   // espirometría: resultado de la validación
    doc["valida"] = ev->valido;
    doc["codigo_error"] = ev->codigo;
    if (!ev->valido) doc["error"] = textoCodigoEspiro(ev->codigo);
  }
  if (ev && ev->valido) {
    doc["fvc"]  = roundf(ev->fvc_L * 100.0f) / 100.0f;
    doc["fev1"] = roundf(ev->fev1_L * 100.0f) / 100.0f;
    doc["pef"]  = roundf(ev->pef_Lmin * 10.0f) / 10.0f;
    doc["bev"]  = roundf(ev->bev_L * 1000.0f) / 1000.0f;
    doc["fet_s"] = roundf(ev->duracion_s * 100.0f) / 100.0f;
    doc["t0_ms"] = lroundf(ev->t0_s * 1000.0f);                // t0 por extrapolación retrógrada
  } else {
    doc["fvc"] = nullptr;
    doc["fev1"] = nullptr;
    doc["pef"] = nullptr;
    if (ev) { doc["bev"] = nullptr; doc["fet_s"] = nullptr; doc["t0_ms"] = nullptr; }
  }
  if (ev && ev->hayEvento) {   // ventana integrada del soplido: la usa el gráfico de Volumen (también si no validó)
    doc["evento_inicio_ms"] = lroundf(ev->tInicio_s * 1000.0f);
    doc["evento_fin_ms"]    = lroundf(ev->tFin_s * 1000.0f);
  }
  doc["fivc"] = nullptr;
  doc["fiv1"] = nullptr;
  if (pim) {   // sesión de PIM: valor informado y detalle de todos los intentos
    doc["valida"] = (pim->codigo == PIM_RES_OK);
    doc["codigo_error"] = pim->codigo;
    if (pim->codigo != PIM_RES_OK) doc["error"] = textoCodigoPIM(pim->codigo);
    if (pim->codigo == PIM_RES_OK) doc["pim_sostenida_cmh2o"] = roundf(pim->pim_cmH2O * 10.0f) / 10.0f;
    else                           doc["pim_sostenida_cmh2o"] = nullptr;
    if (pim->idxReportado >= 0) {
      doc["intento_informado"] = pim->idxReportado + 1;   // la curva del .csv es la de este intento
      doc["pico_cmh2o"] = roundf(pim->intentos[pim->idxReportado].pico_cmH2O * 10.0f) / 10.0f;
    }
    JsonArray arr = doc["intentos"].to<JsonArray>();
    for (int k = 0; k < pim->nIntentos; k++) {
      JsonObject o = arr.add<JsonObject>();
      o["intento"] = k + 1;
      o["codigo"] = pim->intentos[k].codigo;
      if (pim->intentos[k].pim_cmH2O > 0.0f) o["pim_cmh2o"] = roundf(pim->intentos[k].pim_cmH2O * 10.0f) / 10.0f;
      else                                   o["pim_cmh2o"] = nullptr;
      o["esfuerzo_s"] = roundf(pim->intentos[k].duracionEsfuerzo_s * 100.0f) / 100.0f;
    }
    if (pim->reproducible) {
      JsonArray rep = doc["intentos_reproducibles"].to<JsonArray>();
      for (int k = 0; k < AnalisisPIM::INTENTOS_REPRO; k++) rep.add(pim->idxRepro[k] + 1);
    }
  } else if (pimEnCero) doc["pim_sostenida_cmh2o"] = 0.0f;
  else                  doc["pim_sostenida_cmh2o"] = nullptr;
  if (amb && amb->valida) {
    doc["temperatura_c"]   = roundf(amb->temperatura_c * 10.0f) / 10.0f;
    doc["humedad_rel_pct"] = roundf(amb->humedad_rel_pct);
  } else {
    doc["temperatura_c"] = nullptr;
    doc["humedad_rel_pct"] = nullptr;
  }
  if (ev) {   // factor BTPS aplicado a flujos y volúmenes (null = no se pudo leer el sensor, sin corregir)
    if (amb && amb->valida) doc["factor_btps"] = roundf(ev->factorBTPS * 10000.0f) / 10000.0f;
    else                    doc["factor_btps"] = nullptr;
  }

  File fj = SD.open(base + ".json", FILE_WRITE);
  if (!fj) { SD.remove(base + ".csv"); return false; }
  serializeJson(doc, fj);
  fj.close();

  Serial.printf("[SD] %s: %d muestras\n", base.c_str(), (int)datos.size());
  return true;
}

bool Evaluaciones::escribirCsv(const String &ruta, const std::vector<MuestraEspiro> &datos) {
  File fc = SD.open(ruta, FILE_WRITE);
  if (!fc) return false;
  fc.println("tiempo_ms,presion_cmh2o,flujo_lmin");
  char buf[48];
  for (const auto &m : datos) {
    snprintf(buf, sizeof(buf), "%ld,%.2f,%.2f", lroundf(m.tiempo_s * 1000.0f), m.presion_cmH2O, m.flujo_Lmin);
    fc.println(buf);
  }
  fc.close();
  return true;
}

String Evaluaciones::rutaTemporalPIM(int intento) {
  return String(DIR_EVAL) + "/_tmp_pim_" + String(intento + 1) + ".csv";
}

bool Evaluaciones::guardarCsvTemporalPIM(int intento, const std::vector<MuestraEspiro> &datos) {
  SD.mkdir(DIR_EVAL);
  SD.remove(rutaTemporalPIM(intento));
  return escribirCsv(rutaTemporalPIM(intento), datos);
}

bool Evaluaciones::usarCsvTemporalPIM(int intento, const String &id) {
  String destino = String(DIR_EVAL) + "/" + id + ".csv";
  SD.remove(destino);
  return SD.rename(rutaTemporalPIM(intento), destino);
}

void Evaluaciones::borrarTemporalesPIM() {
  for (int k = 0; k < AnalisisPIM::INTENTOS_MAX; k++) SD.remove(rutaTemporalPIM(k));
}

void Evaluaciones::borrar(const String &id) {
  SD.remove(String(DIR_EVAL) + "/" + id + ".csv");
  SD.remove(String(DIR_EVAL) + "/" + id + ".json");
}

// ---------------------------------------------------------------------------
// SUBIDA A LA WEB
// ---------------------------------------------------------------------------
std::vector<String> Evaluaciones::listarPendientes(int max) {
  std::vector<String> pendientes;
  if (!storage.tarjetaPresente()) return pendientes;

  File dir = SD.open(DIR_EVAL);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return pendientes; }

  // Primero los nombres y después se abre cada uno: no se abren archivos mientras se recorre la carpeta
  std::vector<String> nombres;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String nombre = String(f.name());
    bool esDir = f.isDirectory();
    f.close();
    nombre = nombre.substring(nombre.lastIndexOf('/') + 1);
    String minusculas = nombre;
    minusculas.toLowerCase();
    // Los que empiezan con "." los crea la compu al abrir la SD (ej. "._eval_...json" en una Mac): no son pruebas
    if (esDir || nombre.startsWith(".") || !minusculas.endsWith(".json")) continue;
    nombres.push_back(nombre.substring(0, nombre.length() - 5));
  }
  dir.close();

  JsonDocument filtro;
  filtro["sincronizado"] = true;
  for (const String &nombre : nombres) {
    if ((int)pendientes.size() >= max) break;
    File f = SD.open(String(DIR_EVAL) + "/" + nombre + ".json", FILE_READ);
    if (!f) continue;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
    f.close();
    if (!err && !(doc["sincronizado"] | false)) pendientes.push_back(nombre);   // un .json dañado no se puede subir
  }
  return pendientes;
}

// Agrega 'texto' escapado para ir entre comillas en un JSON
static void agregarTextoJson(String &destino, const String &texto) {
  for (unsigned int i = 0; i < texto.length(); i++) {
    char c = texto[i];
    if (c == '"' || c == '\\') { destino += '\\'; destino += c; }
    else if ((unsigned char)c >= 0x20) destino += c;   // los de control (\r, \t) no van en nombres ni en el .csv
  }
}

bool Evaluaciones::armarEnvio(const String &archivo, const String &usuario, String &cuerpo) {
  String base = String(DIR_EVAL) + "/" + archivo;

  // El .json va tal cual está en la SD; solo se revisa que esté sano
  File fj = SD.open(base + ".json", FILE_READ);
  if (!fj) return false;
  String resumen = fj.readString();
  fj.close();
  {
    JsonDocument doc;
    if (deserializeJson(doc, resumen) || !doc.is<JsonObject>()) return false;
  }

  File fc = SD.open(base + ".csv", FILE_READ);   // una PIM sin intento informado no tiene .csv
  size_t tamCsv = fc ? fc.size() : 0;
  cuerpo = "";
  // Cada salto de línea del .csv pasa a ser "\n" (un carácter más por línea): sobra con tamCsv / 8
  if (!cuerpo.reserve(resumen.length() + tamCsv + tamCsv / 8 + 200)) {
    if (fc) fc.close();
    Serial.println("[SUBIDA] Sin memoria para " + archivo);
    return false;
  }

  cuerpo += "{\"usuario\":\"";
  agregarTextoJson(cuerpo, usuario);
  cuerpo += "\",\"archivo\":\"";
  agregarTextoJson(cuerpo, archivo);
  cuerpo += "\",\"resumen\":";
  cuerpo += resumen;
  if (!fc) {
    cuerpo += ",\"csv\":null}";
    return true;
  }

  cuerpo += ",\"csv\":\"";
  while (fc.available()) {
    String linea = fc.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0) continue;
    agregarTextoJson(cuerpo, linea);
    cuerpo += "\\n";
  }
  fc.close();
  cuerpo += "\"}";
  return true;
}

bool Evaluaciones::marcarSincronizado(const String &archivo) {
  String ruta = String(DIR_EVAL) + "/" + archivo + ".json";
  File f = SD.open(ruta, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  doc["sincronizado"] = true;

  // Se escribe a un .tmp y después se renombra (como el perfil): si se corta la luz a mitad, el .json no queda roto
  String tmp = ruta + ".tmp";
  SD.remove(tmp);
  File ft = SD.open(tmp, FILE_WRITE);
  if (!ft) return false;
  size_t n = serializeJson(doc, ft);
  ft.close();
  if (n == 0) { SD.remove(tmp); return false; }
  SD.remove(ruta);
  return SD.rename(tmp, ruta);
}
