#include "Storage.h"
#include <vector>

BronquitoStorage::BronquitoStorage(uint8_t csPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin)
  : chipSelectPin(csPin), sckPin(sckPin), misoPin(misoPin), mosiPin(mosiPin),
    sdMontada(false), synced(false), lastError(ERR_NO_CARD), sdSPI(VSPI) {
  paciente.dni = "Sin sincronizar";
  paciente.pin = "";
}

bool BronquitoStorage::begin() {
  return (sync() == STORAGE_OK);
}

void BronquitoStorage::marcarError(StorageStatus e, const char *textoDNI) {
  paciente.dni = textoDNI;
  synced = false;
  lastError = e;
}

bool BronquitoStorage::tarjetaPresente() {
  if (!sdMontada) return false;
  File raiz = SD.open("/");
  bool ok = (bool)raiz;
  if (raiz) raiz.close();
  if (!ok) {
    SD.end();
    sdMontada = false;
  }
  return ok;
}

StorageStatus BronquitoStorage::sync(bool verbose) {
  if (verbose) {
    Serial.println("--------------------------------------------------");
    Serial.println("[STORAGE] Intentando inicializar lector MicroSD...");
  }

  // 1) Montar la tarjeta con el bus SPI dedicado para J8
  if (!sdMontada) {
    sdSPI.begin(sckPin, misoPin, mosiPin, chipSelectPin);

    if (!SD.begin(chipSelectPin, sdSPI)) {
      if (verbose) {
        Serial.println("[STORAGE] ¡ERROR! No se detectó la tarjeta MicroSD.");
        Serial.println("--------------------------------------------------");
      }
      marcarError(ERR_NO_CARD, "Sin sincronizar");
      return ERR_NO_CARD;
    }
    sdMontada = true;
  }

  // 2) Abrir perfil.json
  File perfilFile = SD.open("/perfil.json", FILE_READ);
  if (!perfilFile) {
    if (!tarjetaPresente()) {
      if (verbose) {
        Serial.println("[STORAGE] ¡ERROR! La tarjeta ya no está.");
        Serial.println("--------------------------------------------------");
      }
      marcarError(ERR_NO_CARD, "Sin sincronizar");
      return ERR_NO_CARD;
    }
    if (verbose) {
      Serial.println("[STORAGE] ¡ERROR! No se encontró /perfil.json en la raíz.");
      Serial.println("--------------------------------------------------");
    }
    marcarError(ERR_FILE_NOT_FOUND, "Sin sincronizar");
    return ERR_FILE_NOT_FOUND;
  }

  // 3) Leer el JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, perfilFile);
  perfilFile.close();

  if (error) {
    if (verbose) {
      Serial.print("[STORAGE] ¡ERROR! Formato JSON inválido: ");
      Serial.println(error.f_str());
      Serial.println("--------------------------------------------------");
    }
    marcarError(ERR_JSON_PARSE, "Perfil dañado");
    return ERR_JSON_PARSE;
  }

  String usuario = doc["usuario"] | "";
  if (usuario.length() == 0) usuario = doc["dni"] | "Desconocido";
  paciente.dni        = usuario;
  paciente.nombre     = doc["nombre"] | "";
  paciente.apellido   = doc["apellido"] | "";
  paciente.pin        = doc["pin"] | "";
  paciente.pinVersion = doc["pin_version"] | "";
  paciente.sexo       = doc["sexo"] | "-";
  paciente.altura     = doc["altura"] | "-";
  paciente.peso       = doc["peso"] | "-";
  paciente.fumador    = doc["fumador"] | "-";
  paciente.nacimiento = doc["nacimiento"] | "-";
  paciente.registro   = doc["registro"] | "-";
  paciente.enfermedad = doc["enfermedad"] | "-";

  synced = true;
  lastError = STORAGE_OK;

  if (verbose) {
    Serial.println("[STORAGE] ¡Perfil cargado correctamente!");
    Serial.print(" - DNI Paciente: "); Serial.println(paciente.dni);
    Serial.println("--------------------------------------------------");
  }
  return STORAGE_OK;
}

bool BronquitoStorage::escribirJson(const char *ruta, JsonDocument &doc) {
  String tmp = String(ruta) + ".tmp";
  SD.remove(tmp);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  size_t n = serializeJsonPretty(doc, f);
  f.close();
  if (n == 0) { SD.remove(tmp); return false; }
  SD.remove(ruta);
  return SD.rename(tmp, ruta);
}

bool BronquitoStorage::guardarPerfil(JsonVariantConst perfil) {
  if (!tarjetaPresente()) return false;
  static const char *CLAVES[] = {"usuario", "nombre", "apellido", "pin_version", "sexo", "altura",
                                 "peso", "fumador", "nacimiento", "registro", "enfermedad"};
  JsonDocument doc;
  for (const char *k : CLAVES) doc[k] = String(perfil[k] | "");
  String usuario = doc["usuario"] | "";
  if (usuario.length() == 0) return false;
  return escribirJson("/perfil.json", doc);
}

bool BronquitoStorage::guardarPin(const String &pin, const String &version) {
  if (!tarjetaPresente()) return false;
  File f = SD.open("/perfil.json", FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  doc["pin"] = pin;
  doc["pin_version"] = version;
  if (!escribirJson("/perfil.json", doc)) return false;
  paciente.pin = pin;
  paciente.pinVersion = version;
  return true;
}

// Pisa solo la clave "frecuencia": lo demás de CONFIGURACION.JSON (ej. la rutina) queda igual.
bool BronquitoStorage::guardarFrecuencia(JsonVariantConst frecuencia) {
  if (!tarjetaPresente()) return false;
  JsonDocument doc;
  File f = SD.open("/CONFIGURACION.JSON", FILE_READ);
  if (f) {
    if (deserializeJson(doc, f) || !doc.is<JsonObject>()) doc.clear();   // si estaba corrupto, se rehace
    f.close();
  }
  doc["frecuencia"] = frecuencia;
  return escribirJson("/CONFIGURACION.JSON", doc);
}

FrecuenciaGuardada BronquitoStorage::leerFrecuencia() {
  FrecuenciaGuardada fr;
  if (!tarjetaPresente()) return fr;
  File f = SD.open("/CONFIGURACION.JSON", FILE_READ);
  if (!f) return fr;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return fr;

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

static const char *CARPETAS_PRUEBAS[] = {"/EVALUACIONES", "/ENTRENAMIENTOS"};

static std::vector<String> listarArchivos(const char *carpeta) {
  std::vector<String> rutas;
  File dir = SD.open(carpeta);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return rutas; }
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) rutas.push_back(String(carpeta) + "/" + f.name());
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  return rutas;
}

int BronquitoStorage::contarPendientes() {
  if (!tarjetaPresente()) return 0;
  JsonDocument filtro;
  filtro["sincronizado"] = true;

  int pendientes = 0;
  for (const char *carpeta : CARPETAS_PRUEBAS) {
    for (const String &ruta : listarArchivos(carpeta)) {
      String r = ruta;
      r.toLowerCase();
      if (!r.endsWith(".json")) continue;
      // Los que empiezan con "." los crea la compu al abrir la SD (ej. "._eval_...json" en una Mac): no son pruebas
      if (ruta.substring(ruta.lastIndexOf('/') + 1).startsWith(".")) continue;
      File f = SD.open(ruta, FILE_READ);
      if (!f) { pendientes++; continue; }
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
      f.close();
      if (err || !(doc["sincronizado"] | false)) pendientes++;
    }
  }
  return pendientes;
}

FormatoResultado BronquitoStorage::formatearFabrica(bool forzar) {
  if (!sdMontada) {
    sdSPI.begin(sckPin, misoPin, mosiPin, chipSelectPin);
    if (SD.begin(chipSelectPin, sdSPI)) sdMontada = true;
  }
  if (!tarjetaPresente()) return FORMATO_SIN_SD;

  int pendientes = contarPendientes();
  if (!forzar && pendientes > 0) {
    Serial.printf("[STORAGE] Formato cancelado: %d pruebas sin subir\n", pendientes);
    return FORMATO_PENDIENTES;
  }

  bool ok = true;
  const char *sueltos[] = {"/perfil.json", "/CONFIGURACION.JSON"};
  for (const char *ruta : sueltos) {
    if (SD.exists(ruta) && !SD.remove(ruta)) ok = false;
  }
  for (const char *carpeta : CARPETAS_PRUEBAS) {
    for (const String &ruta : listarArchivos(carpeta)) {
      if (!SD.remove(ruta)) ok = false;
    }
  }

  paciente = PerfilPaciente();
  marcarError(ERR_FILE_NOT_FOUND, "Sin sincronizar");
  Serial.println(ok ? "[STORAGE] Formato de fabrica OK" : "[STORAGE] Formato incompleto");
  return ok ? FORMATO_OK : FORMATO_ERROR;
}
