#include "TarjetaSD.h"
#include "NivelResistencia.h"
#include <Preferences.h>
#include <functional>
#include <esp_random.h>

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

bool TarjetaSD::remontar() {
  if (montada) {
    SD.end();
    montada = false;
  }
  return montar() && hayTarjeta();
}

bool TarjetaSD::hayTarjeta() {
  if (!montada) return false;
  // Se lee el sector 0 directo de la tarjeta. Abrir "/" no alcanza: con lo que el sistema de archivos
  // tiene en memoria puede salir bien aunque la tarjeta ya no responda (falso contacto, se movió).
  static uint8_t sector[512];
  bool ok = SD.readRAW(sector, 0);
  if (!ok) {                    // la sacaron o no responde: se desmonta para poder volver a montarla
    SD.end();
    montada = false;
  }
  return ok;
}

bool TarjetaSD::probarEscritura() {
  static const char *RUTA = "/_prueba_escritura.txt";
  File f = SD.open(RUTA, FILE_WRITE);
  if (!f) return false;
  bool escrito = f.print("ok") == 2;
  f.close();
  bool borrado = SD.remove(RUTA);
  return escrito && borrado;
}

void TarjetaSD::olvidarPaciente(EstadoTarjeta motivo) {
  perfil = PerfilPaciente();     // todos los campos vacíos
  estadoActual = motivo;
}

String TarjetaSD::textoLogin() const {
  if (estadoActual == TARJETA_OK)            return perfil.usuario;
  if (estadoActual == TARJETA_PERFIL_DANADO) return "Perfil dañado";
  if (estadoActual == TARJETA_NO_ESTA)       return "Sin microSD";
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
  static const char *CLAVES[] = {"usuario", "nombre", "apellido", "pin_version", "sexo", "altura",
                                 "peso", "fumador", "nacimiento", "registro", "enfermedad"};
  JsonDocument doc;
  for (const char *clave : CLAVES) doc[clave] = String(perfilWeb[clave] | "");
  String usuario = doc["usuario"] | "";
  if (usuario.length() == 0) return false;

  montar();
  if (hayTarjeta() && escribirJsonSeguro(ARCHIVO_PERFIL, doc)) return true;
  // Puede ser un montaje viejo (la tarjeta se movió o tuvo un falso contacto): se remonta y se prueba otra vez
  Serial.println("[SD] No se pudo guardar el perfil: se vuelve a montar la tarjeta y se reintenta");
  bool ok = remontar() && escribirJsonSeguro(ARCHIVO_PERFIL, doc);
  if (!ok) Serial.println("[SD] Tampoco se pudo guardar el perfil al reintentar");
  return ok;
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

// Formato (por ahora se escribe a mano; guardarFrecuencia lo respeta porque pisa solo "frecuencia"):
//   "resistencia": { "nivel": 3, "vigencia_dias": 14, "vence": "2026-11-22", "fijado_por": "medico" }

// =============================================================================
// IDS SIN HORA
// =============================================================================
// Sin hora el id no puede ser la fecha: va "<prefijo>SINHORA_<n>_<xxxxxx>". La web reconoce cada prueba
// por su nombre, así que no se puede repetir NUNCA, ni siquiera después de formatear la SD:
//  - n: contador que se guarda en la flash (sobrevive al formateo de la SD y a reprogramar el equipo).
//    Se toma el mayor entre ese contador y los n que ya hay en la SD, por si la flash se borró.
//  - xxxxxx: 6 dígitos hex al azar (generador de hardware de la ESP32). Cubren el caso en que se borran
//    la flash y la SD a la vez, o el paciente usa otro equipo que va por el mismo n.
//  - además se verifica que ningún archivo de la SD use ese id.
static unsigned long numeroSinHora(const String &nombre, const String &prefijo) {
  if (!nombre.startsWith(prefijo)) return 0;
  return strtoul(nombre.c_str() + prefijo.length(), nullptr, 10);
}

static String nuevoIdSinHora(const std::vector<String> &archivos, const char *prefijo, const char *claveFlash,
                             std::function<bool(const String &)> usado) {
  String pre = String(prefijo) + "SINHORA_";
  Preferences prefs;
  bool hayFlash = prefs.begin("ids_sinhora", false);
  unsigned long n = hayFlash ? prefs.getULong(claveFlash, 0) : 0;
  for (const String &ruta : archivos) {
    unsigned long k = numeroSinHora(ruta.substring(ruta.lastIndexOf('/') + 1), pre);
    if (k > n) n = k;
  }
  String id;
  do {
    n++;
    char azar[7];
    snprintf(azar, sizeof(azar), "%06lx", (unsigned long)(esp_random() & 0xFFFFFF));
    id = pre + String(n) + "_" + azar;
  } while (usado(id));
  if (hayFlash) {
    prefs.putULong(claveFlash, n);
    prefs.end();
  } else {
    Serial.println("[SD] No se pudo guardar el contador de ids sin hora en la flash");
  }
  return id;
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

  // Sin hora: "eval_SINHORA_n_xxxxxx" (ver nuevoIdSinHora), sin ningún archivo en la tarjeta
  auto usado = [&](const String &id) {
    static const char *SUFIJOS[] = {"_PIM.json", "_PIM.csv", "_ESP.json", "_ESP.csv", ".json", ".csv"};
    String base = String(CARPETA_MEDICIONES) + "/" + id;
    for (const char *s : SUFIJOS)
      if (SD.exists(base + s)) return true;
    for (int c = 1; c <= 10; c++)                       // intentos fallidos de espirometría
      if (SD.exists(base + "_ESP_int1_E" + String(c) + ".json")) return true;
    return false;
  };
  return nuevoIdSinHora(listarArchivos(CARPETA_MEDICIONES), "eval_", "eval", usado);
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
    doc["fvc"]   = roundf(ev->fvc_L * 100.0f) / 100.0f;          // con la cola extrapolada
    doc["fvc_medido"]   = roundf(ev->fvcMedido_L * 100.0f) / 100.0f;
    doc["volumen_cola"] = roundf(ev->volCola_L * 1000.0f) / 1000.0f;   // L agregados (0 = sin corrección)
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

// Ej.: eval_SINHORA_24_3fa9c1_ESP_int2_E6 -> calibración 24, segundo intento de espirometría, falló por E6
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
// ENTRENAMIENTOS
// =============================================================================
static const char *CARPETA_ENTRENAMIENTOS = "/ENTRENAMIENTOS";

static String rutaEntrenamiento(const String &id, const char *extension) {
  return String(CARPETA_ENTRENAMIENTOS) + "/" + id + extension;
}

String TarjetaSD::nuevoIdEntrenamiento(const String &fechaHora) {
  String digitos = "";
  for (unsigned int i = 0; i < fechaHora.length(); i++)
    if (isDigit(fechaHora[i])) digitos += fechaHora[i];

  String base = (digitos.length() >= 14 && digitos.substring(0, 4).toInt() >= 2024)
                  ? "ent_" + digitos.substring(0, 8) + "_" + digitos.substring(8, 14)
                  : String("ent_SINHORA");
  auto usado = [&](const String &id) {
    return SD.exists(rutaEntrenamiento(id, ".csv")) || SD.exists(rutaEntrenamiento(id, ".json")) ||
           SD.exists(rutaEntrenamiento(id, ".csv.tmp"));
  };
  // Con hora: el id es la fecha (si ya existe, se le agrega _2, _3...). Sin hora: ent_SINHORA_n_xxxxxx
  if (base == "ent_SINHORA") return nuevoIdSinHora(listarArchivos(CARPETA_ENTRENAMIENTOS), "ent_", "ent", usado);
  if (!usado(base)) return base;
  int n = 2;
  String id;
  do { id = base + "_" + String(n++); } while (usado(id));
  return id;
}

bool TarjetaSD::iniciarEntrenamiento(const String &id) {
  if (id.length() == 0 || !hayTarjeta()) return false;
  if (!SD.exists(CARPETA_ENTRENAMIENTOS) && !SD.mkdir(CARPETA_ENTRENAMIENTOS)) return false;
  String temporal = rutaEntrenamiento(id, ".csv.tmp");
  SD.remove(temporal);
  archivoEntrenamiento = SD.open(temporal, FILE_WRITE);
  if (!archivoEntrenamiento) return false;
  errorEntrenamiento = false;
  muestrasEntrenamiento = 0;
  // Mismas columnas que las evaluaciones (ver escribirCsv)
  if (archivoEntrenamiento.println("tiempo_ms,presion_cmh2o,flujo_lmin,v_flujo_v,v_pim_v") == 0) {
    archivoEntrenamiento.close();
    SD.remove(temporal);
    return false;
  }
  return true;
}

bool TarjetaSD::agregarMuestrasEntrenamiento(const MuestraEspiro *muestras, size_t cantidad) {
  if (!archivoEntrenamiento) return false;
  char linea[80];
  for (size_t i = 0; i < cantidad; i++) {
    const MuestraEspiro &m = muestras[i];
    int largo = snprintf(linea, sizeof(linea), "%ld,%.2f,%.2f,%.4f,%.4f\n", lroundf(m.tiempo_s * 1000.0f),
                         m.presion_cmH2O, m.flujo_Lmin, m.vFlujo_V, m.vPIM_V);
    if (largo <= 0 || archivoEntrenamiento.write((const uint8_t *)linea, largo) != (size_t)largo) {
      errorEntrenamiento = true;
      Serial.println("[SD] Error al escribir el entrenamiento");
      return false;
    }
  }
  muestrasEntrenamiento += cantidad;
  archivoEntrenamiento.flush();   // si se corta la luz, lo medido hasta acá queda en la tarjeta
  return true;
}

bool TarjetaSD::cerrarEntrenamiento() {
  if (archivoEntrenamiento) archivoEntrenamiento.close();
  return !errorEntrenamiento;
}

bool TarjetaSD::cargarSenalEntrenamiento(const String &id, AnalisisEntrenamiento::SenalEntrenamiento &senal) {
  using namespace AnalisisEntrenamiento;
  senal.liberar();
  if (id.length() == 0 || errorEntrenamiento || !hayTarjeta()) return false;
  cerrarEntrenamiento();

  // Memoria: la señal va en 4 vectores separados (bloques más chicos que uno solo) y después el
  // análisis pide lo suyo. Se revisa antes de reservar para no quedarse sin memoria a mitad de camino.
  size_t n = muestrasEntrenamiento + 16;
  size_t total = memoriaSenal(n) + memoriaAnalisis(n);
  Serial.printf("[MEM] Entrenamiento: %u muestras | necesita %u bytes | libre %u | mayor bloque %u\n",
                (unsigned)n, (unsigned)total, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (ESP.getFreeHeap() < total + 16384 || ESP.getMaxAllocHeap() < n * sizeof(uint32_t) + 4096) {
    Serial.println("[MEM] No hay memoria para analizar el entrenamiento");
    return false;
  }
  senal.reservar(n);

  File f = SD.open(rutaEntrenamiento(id, ".csv.tmp"), FILE_READ);
  if (!f) return false;
  char linea[96];
  while (f.available() && senal.size() < n) {
    size_t largo = f.readBytesUntil('\n', linea, sizeof(linea) - 1);
    linea[largo] = '\0';
    if (largo == 0 || !(isDigit(linea[0]) || linea[0] == '-')) continue;   // encabezado o línea vacía
    // tiempo_ms,presion_cmh2o,flujo_lmin,v_flujo_v,v_pim_v
    char *p = linea, *fin;
    long tMs = strtol(p, &fin, 10);       if (*fin != ',') continue; p = fin + 1;
    float presion = strtof(p, &fin);      if (*fin != ',') continue; p = fin + 1;
    float flujo = strtof(p, &fin);        if (*fin != ',') continue; p = fin + 1;
    float vFlujo = strtof(p, &fin);       if (fin == p) continue;
    senal.agregar((uint32_t)std::max(0L, tMs), presion, flujo, vFlujo);
  }
  f.close();
  return senal.size() > 0;
}

// Número redondeado para el .json; NAN -> null
static void ponerNumero(JsonObject obj, const char *clave, float valor, int decimales) {
  if (std::isnan(valor) || std::isinf(valor)) { obj[clave] = nullptr; return; }
  double escala = pow(10.0, decimales);
  obj[clave] = round((double)valor * escala) / escala;   // en double: así no queda 99.30000305
}

static void ponerCiclo(JsonObject obj, const char *clave, int ciclo) {
  if (ciclo > 0) obj[clave] = ciclo;
  else obj[clave] = nullptr;
}

// Misma regla que cargarSenalEntrenamiento para saber si una línea es una muestra (así la fila k del
// CSV corresponde al valor k del flujo procesado)
static bool esMuestraEntrenamiento(const char *linea) {
  if (!(isDigit(linea[0]) || linea[0] == '-')) return false;
  const char *p = linea;
  char *fin;
  strtol(p, &fin, 10);  if (*fin != ',') return false; p = fin + 1;
  strtof(p, &fin);      if (*fin != ',') return false; p = fin + 1;
  strtof(p, &fin);      if (*fin != ',') return false; p = fin + 1;
  strtof(p, &fin);      return fin != p;
}

bool TarjetaSD::agregarFlujoProcesado(const String &id, const std::vector<int16_t> &flujoProc) {
  if (id.length() == 0 || errorEntrenamiento || !hayTarjeta()) return false;
  cerrarEntrenamiento();
  String origen = rutaEntrenamiento(id, ".csv.tmp");
  String destino = rutaEntrenamiento(id, ".csv.proc");
  File fin = SD.open(origen, FILE_READ);
  if (!fin) return false;
  SD.remove(destino);
  File fout = SD.open(destino, FILE_WRITE);
  if (!fout) { fin.close(); return false; }

  // Se escribe de a bloques: miles de escrituras chicas en la SD son lentas
  static const size_t TAM_BLOQUE = 4096;
  char *bloque = (char *)malloc(TAM_BLOQUE);
  if (!bloque) { fin.close(); fout.close(); SD.remove(destino); return false; }
  size_t usado = 0;
  bool ok = true;
  auto escribir = [&](const char *txt, size_t largo) {
    if (usado + largo > TAM_BLOQUE) {
      if (fout.write((const uint8_t *)bloque, usado) != usado) ok = false;
      usado = 0;
    }
    memcpy(bloque + usado, txt, largo);
    usado += largo;
  };

  char linea[112];
  size_t k = 0;
  while (ok && fin.available()) {
    size_t largo = fin.readBytesUntil('\n', linea, 96);
    linea[largo] = '\0';
    if (largo > 0 && linea[largo - 1] == '\r') linea[--largo] = '\0';
    if (largo == 0) continue;
    escribir(linea, largo);
    if (strncmp(linea, "tiempo_ms", 9) == 0) {
      static const char ENCABEZADO_EXTRA[] = ",flujo_proc_lmin\n";   // directo: no pasa por un buffer
      escribir(ENCABEZADO_EXTRA, sizeof(ENCABEZADO_EXTRA) - 1);
      continue;
    }
    char extra[24];
    int le;
    if (esMuestraEntrenamiento(linea)) {
      if (k < flujoProc.size()) le = snprintf(extra, sizeof(extra), ",%.1f\n", flujoProc[k] / AnalisisEntrenamiento::ESCALA_FLUJO);
      else                      le = snprintf(extra, sizeof(extra), ",\n");
      k++;
    } else {
      le = snprintf(extra, sizeof(extra), "\n");
    }
    // snprintf devuelve el largo que HABRÍA tenido: si no entró, se escribe solo lo que quedó en el buffer
    if (le < 0) le = 0;
    if (le >= (int)sizeof(extra)) le = sizeof(extra) - 1;
    escribir(extra, le);
  }
  if (ok && usado > 0 && fout.write((const uint8_t *)bloque, usado) != usado) ok = false;
  free(bloque);
  fin.close();
  fout.close();
  if (!flujoProc.empty() && k != flujoProc.size())
    Serial.printf("[SD] Flujo procesado: %u filas en el CSV y %u valores\n", (unsigned)k, (unsigned)flujoProc.size());

  if (!ok) {
    SD.remove(destino);
    Serial.println("[SD] No se pudo agregar el flujo procesado: el CSV queda sin esa columna");
    return false;
  }
  SD.remove(origen);
  return SD.rename(destino, origen);
}

bool TarjetaSD::guardarEntrenamiento(const String &id, const String &fechaHora, const CondicionesAmbiente &amb,
                                     int nivelResistencia, bool resistenciaManual, float duracion_s, float dcServo,
                                     const char *estado, const char *fin, int ciclosObjetivo,
                                     const AnalisisEntrenamiento::FeaturesEntrenamiento *features) {
  if (id.length() == 0 || errorEntrenamiento || !hayTarjeta()) return false;
  cerrarEntrenamiento();
  String csv = rutaEntrenamiento(id, ".csv");
  SD.remove(csv);
  if (!SD.rename(rutaEntrenamiento(id, ".csv.tmp"), csv)) return false;

  JsonDocument doc;
  JsonObject raiz = doc.to<JsonObject>();
  raiz["version"] = 1;
  raiz["id_entrenamiento"] = id;
  if (fechaHora.length() >= 16) raiz["fecha_hora"] = fechaHora;   // igual que las evaluaciones ("sin hora": no va)
  raiz["sincronizado"] = false;                                   // pasa a true cuando la web lo recibe
  ponerNumero(raiz, "duracion_s", duracion_s, 2);
  ponerNumero(raiz, "dc", dcServo, 1);            // duty cycle del PWM del servo (%), no el de la respiración
  raiz["estado"] = estado;
  raiz["fin"] = fin;
  raiz["ciclos_objetivo"] = ciclosObjetivo;
  if (nivelResistencia >= 1 && nivelResistencia <= 5) raiz["nivel_resistencia"] = nivelResistencia;
  else                                                raiz["nivel_resistencia"] = nullptr;
  raiz["modo_resistencia"] = resistenciaManual ? "manual" : "automatico";
  // Ambiente y BTPS, con los mismos nombres que las evaluaciones (null = no se pudo leer el DHT22: sin corregir)
  if (amb.valida) {
    raiz["temperatura_c"]   = roundf(amb.temperatura_c * 10.0f) / 10.0f;
    raiz["humedad_rel_pct"] = roundf(amb.humedad_rel_pct);
    raiz["factor_btps"]     = roundf(amb.factor_btps * 10000.0f) / 10000.0f;
  } else {
    raiz["temperatura_c"]   = nullptr;
    raiz["humedad_rel_pct"] = nullptr;
    raiz["factor_btps"]     = nullptr;
  }

  if (!features) {
    raiz["ciclos"] = nullptr;    // no se pudieron calcular (ver el monitor serie)
    raiz["resumen"] = nullptr;
  } else {
    const AnalisisEntrenamiento::FeaturesEntrenamiento &F = *features;
    JsonArray ciclos = raiz["ciclos"].to<JsonArray>();
    for (const auto &c : F.ciclos) {
      JsonObject o = ciclos.add<JsonObject>();
      o["numero"] = c.numero;
      ponerNumero(o, "t_inicio_s", c.t_inicio_s, 2);
      ponerNumero(o, "t_fin_s", c.t_fin_s, 2);
      ponerNumero(o, "ti_s", c.ti_s, 2);
      ponerNumero(o, "te_s", c.te_s, 2);
      ponerNumero(o, "pausa_s", c.pausa_s, 2);
      ponerNumero(o, "ti_ttot", c.ti_ttot, 2);
      ponerNumero(o, "pif_Lmin", c.pif_Lmin, 1);
      ponerNumero(o, "pef_Lmin", c.pef_Lmin, 1);
      ponerNumero(o, "vol_insp_L", c.vol_insp_L, 2);
      ponerNumero(o, "vol_esp_L", c.vol_esp_L, 2);
      ponerNumero(o, "pim_cmH2O", c.pim_cmH2O, 1);
      ponerNumero(o, "pem_cmH2O", c.pem_cmH2O, 1);
      ponerNumero(o, "trabajo_insp_J", c.trabajo_insp_J, 2);
      ponerNumero(o, "potencia_insp_W", c.potencia_insp_W, 2);
      ponerNumero(o, "t_pif_s", c.t_pif_s, 2);
      ponerNumero(o, "t_pef_s", c.t_pef_s, 2);
      ponerNumero(o, "t_pim_s", c.t_pim_s, 2);
      ponerNumero(o, "t_pem_s", c.t_pem_s, 2);
    }

    JsonObject resumen = raiz["resumen"].to<JsonObject>();
    JsonObject prom = resumen["promedios"].to<JsonObject>();
    ponerNumero(prom, "ti_s", F.prom_ti_s, 2);
    ponerNumero(prom, "te_s", F.prom_te_s, 2);
    ponerNumero(prom, "pausa_s", F.prom_pausa_s, 2);
    ponerNumero(prom, "ti_ttot", F.prom_ti_ttot, 2);
    ponerNumero(prom, "pif_Lmin", F.prom_pif_Lmin, 1);
    ponerNumero(prom, "pef_Lmin", F.prom_pef_Lmin, 1);
    ponerNumero(prom, "vol_insp_L", F.prom_vol_insp_L, 2);
    ponerNumero(prom, "vol_esp_L", F.prom_vol_esp_L, 2);
    ponerNumero(prom, "pim_cmH2O", F.prom_pim_cmH2O, 1);
    ponerNumero(prom, "pem_cmH2O", F.prom_pem_cmH2O, 1);
    ponerNumero(prom, "trabajo_insp_J", F.prom_trabajo_insp_J, 2);
    ponerNumero(prom, "potencia_insp_W", F.prom_potencia_insp_W, 2);

    JsonObject multi = resumen["multiciclo"].to<JsonObject>();
    multi["n_ciclos_completos"] = F.n_ciclos_completos;
    multi["n_fases_sueltas"] = F.n_fases_sueltas;
    ponerNumero(multi, "periodo_medio_s", F.periodo_medio_s, 2);
    ponerNumero(multi, "frecuencia_rpm", F.frecuencia_rpm, 1);
    ponerNumero(multi, "ventilacion_minuto_Lmin", F.ventilacion_minuto_Lmin, 1);
    ponerNumero(multi, "trabajo_total_insp_J", F.trabajo_total_insp_J, 2);
    ponerNumero(multi, "pendiente_pim_por_ciclo", F.pendiente_pim_por_ciclo, 1);
    ponerNumero(multi, "caida_pim_pct", F.caida_pim_pct, 1);
    ponerNumero(multi, "pif_max_Lmin", F.pif_max_Lmin, 1);
    ponerCiclo(multi, "pif_max_ciclo", F.pif_max_ciclo);
    ponerNumero(multi, "pef_max_Lmin", F.pef_max_Lmin, 1);
    ponerCiclo(multi, "pef_max_ciclo", F.pef_max_ciclo);
    ponerNumero(multi, "pim_max_cmH2O", F.pim_max_cmH2O, 1);
    ponerCiclo(multi, "pim_max_ciclo", F.pim_max_ciclo);
    ponerNumero(multi, "pem_max_cmH2O", F.pem_max_cmH2O, 1);
    ponerCiclo(multi, "pem_max_ciclo", F.pem_max_ciclo);
  }

  if (!escribirJsonSeguro(rutaEntrenamiento(id, ".json"), doc)) { SD.remove(csv); return false; }
  return true;
}

bool TarjetaSD::descartarEntrenamiento(const String &id) {
  cerrarEntrenamiento();
  if (id.length() == 0 || !hayTarjeta()) return false;
  bool ok = true;
  for (const char *ext : {".csv.tmp", ".csv.proc", ".csv", ".json", ".json.tmp"}) {
    String ruta = rutaEntrenamiento(id, ext);
    if (SD.exists(ruta) && !SD.remove(ruta)) ok = false;
  }
  return ok;
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

// ---- Historial de entrenamientos --------------------------------------------
// Orden de los ids, como claveOrden() pero con el prefijo de los entrenamientos: los "ent_SINHORA_n" van
// antes que los que tienen fecha y entre ellos por número (ent_SINHORA_10 después de ent_SINHORA_9)
static String claveOrdenEntrenamiento(const String &id) {
  const String prefijo = "ent_SINHORA_";
  if (!id.startsWith(prefijo)) return "2" + id;
  int fin = prefijo.length();
  while (fin < (int)id.length() && isDigit(id[fin])) fin++;
  String numero = id.substring(prefijo.length(), fin);
  while (numero.length() < 8) numero = "0" + numero;
  return "1" + numero + id.substring(fin);
}

int TarjetaSD::listarEntrenamientos(String *ids, String *fechas, String *tipos, int max) {
  if (!hayTarjeta()) return -1;

  // 1) Ids de los .json, del más nuevo al más viejo (inserción, quedan los 'max' más nuevos)
  int cantidad = 0;
  for (const String &ruta : listarArchivos(CARPETA_ENTRENAMIENTOS)) {
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    if (!nombre.endsWith(".json") || !nombre.startsWith("ent_")) continue;
    String id = nombre.substring(0, nombre.length() - 5);

    int pos = cantidad;
    String clave = claveOrdenEntrenamiento(id);
    while (pos > 0 && clave > claveOrdenEntrenamiento(ids[pos - 1])) pos--;
    if (pos >= max) continue;
    int fin = (cantidad < max) ? cantidad : max - 1;
    for (int k = fin; k > pos; k--) ids[k] = ids[k - 1];
    ids[pos] = id;
    if (cantidad < max) cantidad++;
  }

  // 2) Fecha y ciclos de cada uno, para la lista en pantalla
  for (int i = 0; i < cantidad; i++) {
    String fechaHora = "";
    int hechos = -1, objetivo = 0;
    JsonDocument doc;
    if (leerJson(rutaEntrenamiento(ids[i], ".json"), doc)) {
      fechaHora = String(doc["fecha_hora"] | "");
      objetivo  = doc["ciclos_objetivo"] | 0;
      JsonVariantConst nc = doc["resumen"]["multiciclo"]["n_ciclos_completos"];
      if (!nc.isNull()) hechos = nc.as<int>();
    }
    tipos[i] = (hechos >= 0 ? String(hechos) : String("--")) + "/" + String(objetivo) + " ciclos";
    fechas[i] = fechaLegible(fechaHora, ids[i]);
    // Sin hora todos dirían "Sin fecha": se agrega el número para distinguirlos
    if (fechas[i] == "Sin fecha" && ids[i].startsWith("ent_SINHORA_"))
      fechas[i] = "Sin fecha #" + String(numeroSinHora(ids[i], "ent_SINHORA_"));
  }
  return cantidad;
}

// =============================================================================
// NIVEL DE RESISTENCIA (/CONFIGURACION.JSON: "resistencia_web", "predichos", "resistencia_auto")
// =============================================================================
// Pisa solo esa clave: lo demás de CONFIGURACION.JSON queda igual
bool TarjetaSD::guardarResistenciaWeb(JsonVariantConst resistencia) {
  if (!hayTarjeta()) return false;
  JsonDocument doc;
  if (!leerJson(ARCHIVO_CONFIG, doc) || !doc.is<JsonObject>()) doc.clear();
  doc["resistencia_web"] = resistencia;
  return escribirJsonSeguro(ARCHIVO_CONFIG, doc);
}

bool TarjetaSD::guardarPredichos(JsonVariantConst predichos) {
  if (!hayTarjeta()) return false;
  JsonDocument doc;
  if (!leerJson(ARCHIVO_CONFIG, doc) || !doc.is<JsonObject>()) doc.clear();
  doc["predichos"] = predichos;
  return escribirJsonSeguro(ARCHIVO_CONFIG, doc);
}

ResistenciaWeb TarjetaSD::leerResistenciaWeb() {
  ResistenciaWeb r;
  JsonDocument doc;
  if (!hayTarjeta() || !leerJson(ARCHIVO_CONFIG, doc)) return r;
  JsonObjectConst w = doc["resistencia_web"];
  if (w.isNull()) return r;                       // la web todavía no mandó nada: automático
  int nivel = w["nivel_manual"] | 0;
  r.nivelManual = (nivel >= 1 && nivel <= 5) ? nivel : 0;
  r.enviado = String(w["enviado"] | "");
  r.manual = String(w["modo"] | "automatico") == "manual";
  if (r.manual && r.nivelManual == 0) {
    Serial.println("[CONFIG] \"resistencia_web\" en modo manual sin un nivel 1-5: se usa el automático");
    r.manual = false;
  }
  return r;
}

Predichos TarjetaSD::leerPredichos() {
  Predichos p;
  JsonDocument doc;
  if (!hayTarjeta() || !leerJson(ARCHIVO_CONFIG, doc)) return p;
  JsonObjectConst o = doc["predichos"];
  if (o.isNull()) return p;
  float fvc = o["fvc_predicha_L"].isNull() ? NAN : o["fvc_predicha_L"].as<float>();
  float pim = o["pim_predicha_cmh2o"].isNull() ? NAN : o["pim_predicha_cmh2o"].as<float>();
  if (fvc > 0.0f) p.fvc_L = fvc;                  // null, 0 o negativo: no sirve
  if (pim > 0.0f) p.pim_cmH2O = pim;
  return p;
}

ResistenciaAuto TarjetaSD::leerResistenciaAuto() {
  ResistenciaAuto r;
  JsonDocument doc;
  if (!hayTarjeta() || !leerJson(ARCHIVO_CONFIG, doc)) return r;
  JsonObjectConst a = doc["resistencia_auto"];
  if (a.isNull()) return r;
  auto numero = [](JsonVariantConst v) -> float { return v.isNull() ? NAN : v.as<float>(); };
  r.fechaEvaluacion = String(a["fecha_evaluacion"] | "");
  r.hay = r.fechaEvaluacion.length() == 10;
  int nivel = a["nivel"] | 0;
  r.nivel = (nivel >= 1 && nivel <= 5) ? nivel : 0;
  r.puntajeS = numero(a["puntaje_s"]);
  r.pctPim = numero(a["pct_pim"]);
  r.pctFvc = numero(a["pct_fvc"]);
  r.pim_cmH2O = numero(a["pim_cmh2o"]);
  r.fvc_L = numero(a["fvc_L"]);
  r.vigenciaDias = a["vigencia_dias"] | 0;
  r.vence = String(a["vence"] | "");
  return r;
}

bool TarjetaSD::actualizarResistenciaAuto() {
  using namespace NivelResistencia;
  if (!hayTarjeta()) return false;

  // 1) La mejor PIM y la mejor FVC VÁLIDAS de cada día (las evaluaciones sin fecha no se pueden ubicar)
  struct Dia { long dia; float pim; float fvc; };
  std::vector<Dia> dias;
  for (const String &ruta : listarArchivos(CARPETA_MEDICIONES)) {
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    if (!nombre.endsWith(".json") || !nombre.startsWith("eval_")) continue;
    JsonDocument doc;
    if (!leerJson(rutaMedicion(nombre.substring(0, nombre.length() - 5), ".json"), doc)) continue;
    if (!(doc["valida"] | true)) continue;
    long dia = diaDelCalendario(String(doc["fecha_hora"] | "").c_str());
    if (dia < 0) continue;
    String tipo = String(doc["tipo"] | "");
    tipo.toUpperCase();
    float pim = 0.0f, fvc = 0.0f;
    if (tipo.startsWith("PIM"))         pim = fabsf(doc["pim_sostenida_cmh2o"] | 0.0f);
    else if (tipo.startsWith("ESPIRO")) fvc = doc["fvc"] | 0.0f;
    if (pim <= 0.0f && fvc <= 0.0f) continue;     // PIM en cero (sin sensor) o sin valor
    size_t k = 0;
    while (k < dias.size() && dias[k].dia != dia) k++;
    if (k == dias.size()) dias.push_back({dia, 0.0f, 0.0f});
    dias[k].pim = std::max(dias[k].pim, pim);
    dias[k].fvc = std::max(dias[k].fvc, fvc);
  }

  // 2) El día más reciente que tenga las dos: entre días distintos manda el más reciente
  int elegido = -1;
  for (size_t k = 0; k < dias.size(); k++)
    if (dias[k].pim > 0.0f && dias[k].fvc > 0.0f && (elegido < 0 || dias[k].dia > dias[elegido].dia)) elegido = (int)k;
  if (elegido < 0) {
    Serial.println("[RESIST] No hay ningún día con una PIM y una espirometría válidas: no se cambia el nivel");
    return false;
  }
  const Dia &d = dias[elegido];

  // 3) %PIM, %FVC, S y nivel (sin predichos no se puede: queda la vigencia sin nivel)
  Predichos pred = leerPredichos();
  float pctPim = NAN, pctFvc = NAN, s = NAN;
  if (!isnan(pred.pim_cmH2O) && !isnan(pred.fvc_L)) {
    pctPim = d.pim / pred.pim_cmH2O * 100.0f;
    pctFvc = d.fvc / pred.fvc_L * 100.0f;
    s = (float)puntajeS(pctPim, pctFvc);
  }
  int nv = nivel(s, pctPim, pctFvc);
  char fecha[11], vence[11];
  fechaDelDia(d.dia, fecha);
  fechaDelDia(d.dia + VIGENCIA_DIAS, vence);

  JsonDocument doc;
  if (!leerJson(ARCHIVO_CONFIG, doc) || !doc.is<JsonObject>()) doc.clear();
  JsonObject o = doc["resistencia_auto"].to<JsonObject>();
  if (nv > 0) o["nivel"] = nv;
  else        o["nivel"] = nullptr;
  ponerNumero(o, "puntaje_s", s, 1);
  ponerNumero(o, "pct_pim", pctPim, 1);
  ponerNumero(o, "pct_fvc", pctFvc, 1);
  ponerNumero(o, "pim_cmh2o", d.pim, 1);
  ponerNumero(o, "fvc_L", d.fvc, 2);
  o["fecha_evaluacion"] = fecha;
  o["vigencia_dias"] = VIGENCIA_DIAS;
  o["vence"] = vence;
  bool ok = escribirJsonSeguro(ARCHIVO_CONFIG, doc);

  if (nv > 0)
    Serial.printf("[RESIST] %s: PIM %.1f cmH2O (%.0f %%) | FVC %.2f L (%.0f %%) | S %.1f -> nivel %d | vence %s\n",
                  fecha, d.pim, pctPim, d.fvc, pctFvc, s, nv, vence);
  else
    Serial.printf("[RESIST] %s: PIM %.1f cmH2O | FVC %.2f L | faltan los predichos de la web: sin nivel | vence %s\n",
                  fecha, d.pim, d.fvc, vence);
  return ok;
}

EstadoResistencia TarjetaSD::estadoResistencia(const String &fechaHoy) {
  using namespace NivelResistencia;
  EstadoResistencia e;
  ResistenciaWeb w = leerResistenciaWeb();
  ResistenciaAuto a = leerResistenciaAuto();
  e.hayEvaluacion = a.hay;
  e.fechaEvaluacion = a.fechaEvaluacion;
  long hoy = diaDelCalendario(fechaHoy.c_str());
  e.hayFechaHoy = (hoy >= 0);

  if (w.manual) {                 // lo fijó el médico: sin vencimiento
    e.manual = true;
    e.nivel = w.nivelManual;
    e.puedeEntrenar = true;
    return e;
  }
  e.nivel = a.nivel;
  e.vence = a.vence;
  e.vigenciaDias = a.vigenciaDias;
  if (!a.hay) return e;           // automático sin ninguna evaluación válida: no se puede entrenar
  long vence = diaDelCalendario(a.vence.c_str());
  if (e.hayFechaHoy && vence >= 0) {
    e.diasRestantes = (int)(vence - hoy);
    e.puedeEntrenar = (e.diasRestantes >= 0);   // el día del vencimiento todavía vale
  } else {
    e.hayFechaHoy = false;
    e.puedeEntrenar = true;       // no se puede verificar: se deja entrenar con aviso
  }
  return e;
}

bool TarjetaSD::leerResumenEntrenamiento(const String &id, ResumenEntrenamiento &r) {
  JsonDocument doc;
  if (!leerJson(rutaEntrenamiento(id, ".json"), doc)) return false;
  auto numero = [](JsonVariantConst v) -> float { return v.isNull() ? NAN : v.as<float>(); };
  auto entero = [](JsonVariantConst v) -> int { return v.isNull() ? 0 : v.as<int>(); };

  r = ResumenEntrenamiento();
  r.estado         = String(doc["estado"] | "");
  r.fin            = String(doc["fin"] | "");
  r.ciclosObjetivo = doc["ciclos_objetivo"] | 0;
  r.duracion_s     = numero(doc["duracion_s"]);

  JsonVariantConst p = doc["resumen"]["promedios"];
  JsonVariantConst m = doc["resumen"]["multiciclo"];
  r.hayFeatures = !doc["resumen"].isNull();

  r.ti_s           = numero(p["ti_s"]);
  r.te_s           = numero(p["te_s"]);
  r.pausa_s        = numero(p["pausa_s"]);
  r.ti_ttot        = numero(p["ti_ttot"]);
  r.pif_Lmin       = numero(p["pif_Lmin"]);
  r.pef_Lmin       = numero(p["pef_Lmin"]);
  r.volInsp_L      = numero(p["vol_insp_L"]);
  r.volEsp_L       = numero(p["vol_esp_L"]);
  r.pim_cmH2O      = numero(p["pim_cmH2O"]);
  r.pem_cmH2O      = numero(p["pem_cmH2O"]);
  r.trabajoInsp_J  = numero(p["trabajo_insp_J"]);
  r.potenciaInsp_W = numero(p["potencia_insp_W"]);

  r.nCiclos                = entero(m["n_ciclos_completos"]);
  r.nFasesSueltas          = entero(m["n_fases_sueltas"]);
  r.periodo_s              = numero(m["periodo_medio_s"]);
  r.frecuencia_rpm         = numero(m["frecuencia_rpm"]);
  r.ventilacionMinuto_Lmin = numero(m["ventilacion_minuto_Lmin"]);
  r.trabajoTotalInsp_J     = numero(m["trabajo_total_insp_J"]);
  r.pendientePim           = numero(m["pendiente_pim_por_ciclo"]);
  r.caidaPim_pct           = numero(m["caida_pim_pct"]);
  r.pifMax_Lmin            = numero(m["pif_max_Lmin"]);
  r.pefMax_Lmin            = numero(m["pef_max_Lmin"]);
  r.pimMax_cmH2O           = numero(m["pim_max_cmH2O"]);
  r.pemMax_cmH2O           = numero(m["pem_max_cmH2O"]);
  r.pifMaxCiclo            = entero(m["pif_max_ciclo"]);
  r.pefMaxCiclo            = entero(m["pef_max_ciclo"]);
  r.pimMaxCiclo            = entero(m["pim_max_ciclo"]);
  r.pemMaxCiclo            = entero(m["pem_max_ciclo"]);
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
int TarjetaSD::contarPendientes(bool *leido) {
  if (leido) *leido = true;
  if (!hayTarjeta()) {
    if (leido) *leido = false;
    return 0;
  }
  JsonDocument filtro;
  filtro["sincronizado"] = true;       // solo se lee esa clave de cada archivo

  int pendientes = 0;
  for (const char *carpeta : CARPETAS_PRUEBAS) {
    bool ok = true;
    std::vector<String> rutas = listarArchivos(carpeta, &ok);
    if (!ok && leido) *leido = false;
    for (const String &ruta : rutas) {
      String r = ruta;
      r.toLowerCase();
      if (!r.endsWith(".json")) continue;
      // Los que empiezan con "." los crea la compu al abrir la SD (ej. "._eval_...json" en una Mac): no son pruebas
      if (ruta.substring(ruta.lastIndexOf('/') + 1).startsWith(".")) continue;
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
  // Se monta de cero: un montaje viejo (la tarjeta se movió o tuvo un falso contacto) puede parecer
  // andar y fallar en silencio al leer o escribir.
  if (!remontar()) return FORMATO_SIN_TARJETA;

  // Si no se pueden leer las carpetas, la cuenta de pendientes no sirve: no se borra nada
  bool leido = false;
  int pendientes = contarPendientes(&leido);
  if (!leido) {
    Serial.println("[SD] No se pudieron leer las carpetas de pruebas: se vuelve a montar la tarjeta");
    if (remontar()) pendientes = contarPendientes(&leido);
    if (!leido) {
      Serial.println("[SD] No se borró nada: la tarjeta no responde");
      return FORMATO_NO_RESPONDE;
    }
  }
  if (!forzar && pendientes > 0) {
    Serial.printf("[SD] No se borró nada: hay %d mediciones sin subir\n", pendientes);
    return FORMATO_HAY_PENDIENTES;
  }

  // Borra y después comprueba que no quedó nada. La comprobación hace falta porque si la tarjeta deja de
  // responder a mitad de camino, las carpetas se ven vacías y ningún remove falla.
  auto borrarYComprobar = [&]() -> bool {
    bool ok = true;
    for (const char *ruta : {ARCHIVO_PERFIL, ARCHIVO_CONFIG})
      if (SD.exists(ruta) && !SD.remove(ruta)) ok = false;
    for (const char *carpeta : CARPETAS_PRUEBAS) {
      bool listado = true;
      for (const String &ruta : listarArchivos(carpeta, &listado))
        if (!SD.remove(ruta)) ok = false;
      if (!listado) ok = false;
    }

    if (SD.exists(ARCHIVO_PERFIL) || SD.exists(ARCHIVO_CONFIG)) ok = false;
    for (const char *carpeta : CARPETAS_PRUEBAS) {
      bool listado = true;
      int quedan = listarArchivos(carpeta, &listado).size();
      if (!listado || quedan > 0) {
        Serial.printf("[SD] %s: %s\n", carpeta,
                      listado ? (String("quedaron ") + quedan + " archivos").c_str() : "no se pudo leer");
        ok = false;
      }
    }
    if (!probarEscritura()) {
      Serial.println("[SD] La prueba de escritura falló");
      ok = false;
    }
    return ok;
  };

  bool ok = borrarYComprobar();
  if (!ok) {
    Serial.println("[SD] No se borró todo: se vuelve a montar la tarjeta y se reintenta");
    ok = remontar() && borrarYComprobar();
  }

  olvidarPaciente(TARJETA_SIN_PACIENTE);
  Serial.println(ok ? "[SD] Tarjeta borrada y comprobada" : "[SD] No se pudo borrar todo");
  return ok ? FORMATO_OK : FORMATO_ERROR;
}

uint64_t TarjetaSD::espacioTotal() {
  return hayTarjeta() ? SD.totalBytes() : 0;
}

uint64_t TarjetaSD::espacioLibre() {
  if (!hayTarjeta()) return 0;
  uint64_t total = SD.totalBytes(), usado = SD.usedBytes();
  return total > usado ? total - usado : 0;
}

// =============================================================================
// SUBIDA A LA WEB
// =============================================================================
std::vector<String> TarjetaSD::listarPendientes(int max) {
  std::vector<String> pendientes;
  if (!hayTarjeta()) return pendientes;

  // listarArchivos() junta primero los nombres: no se abren archivos mientras se recorre la carpeta
  JsonDocument filtro;
  filtro["sincronizado"] = true;
  for (const String &ruta : listarArchivos(CARPETA_MEDICIONES)) {
    if ((int)pendientes.size() >= max) break;
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    String minusculas = nombre;
    minusculas.toLowerCase();
    // Los que empiezan con "." los crea la compu al abrir la SD (ej. "._eval_...json" en una Mac): no son pruebas
    if (nombre.startsWith(".") || !minusculas.endsWith(".json")) continue;
    File f = SD.open(ruta, FILE_READ);
    if (!f) continue;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
    f.close();
    if (!err && !(doc["sincronizado"] | false))   // un .json dañado no se puede subir
      pendientes.push_back(nombre.substring(0, nombre.length() - 5));
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

bool TarjetaSD::armarEnvio(const String &archivo, const String &usuario, String &cuerpo) {
  // El .json va tal cual está en la SD; solo se revisa que esté sano
  File fj = SD.open(rutaMedicion(archivo, ".json"), FILE_READ);
  if (!fj) return false;
  String resumen = fj.readString();
  fj.close();
  {
    JsonDocument doc;
    if (deserializeJson(doc, resumen) || !doc.is<JsonObject>()) return false;
  }

  File fc = SD.open(rutaMedicion(archivo, ".csv"), FILE_READ);   // una PIM sin intento informado no tiene .csv
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

bool TarjetaSD::marcarSincronizado(const String &archivo) {
  String ruta = rutaMedicion(archivo, ".json");
  JsonDocument doc;
  if (!leerJson(ruta, doc)) return false;
  doc["sincronizado"] = true;
  // escribirJsonSeguro() escribe a un .tmp y después renombra: si se corta la luz a mitad, el .json no queda roto.
  // Compacto, como lo guarda guardarMedicion()
  return escribirJsonSeguro(ruta, doc, false);
}

// ---- Subida de entrenamientos ------------------------------------------------
std::vector<String> TarjetaSD::listarEntrenamientosPendientes(int max) {
  std::vector<String> pendientes;
  if (!hayTarjeta()) return pendientes;
  JsonDocument filtro;
  filtro["sincronizado"] = true;
  for (const String &ruta : listarArchivos(CARPETA_ENTRENAMIENTOS)) {
    if ((int)pendientes.size() >= max) break;
    String nombre = ruta.substring(ruta.lastIndexOf('/') + 1);
    // Los que empiezan con "." los crea la compu al abrir la SD: no son pruebas
    if (nombre.startsWith(".") || !nombre.startsWith("ent_") || !nombre.endsWith(".json")) continue;
    File f = SD.open(ruta, FILE_READ);
    if (!f) continue;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
    f.close();
    // Los guardados antes de que existiera la subida no tienen el campo: también están pendientes
    if (!err && !(doc["sincronizado"] | false))
      pendientes.push_back(nombre.substring(0, nombre.length() - 5));
  }
  return pendientes;
}

bool TarjetaSD::armarEnvioEntrenamiento(const String &archivo, const String &usuario, String &cuerpo) {
  File fj = SD.open(rutaEntrenamiento(archivo, ".json"), FILE_READ);
  if (!fj) return false;
  String resumen = fj.readString();
  fj.close();
  {
    JsonDocument doc;
    if (deserializeJson(doc, resumen) || !doc.is<JsonObject>()) return false;   // dañado: no se sube
  }
  cuerpo = "";
  if (!cuerpo.reserve(resumen.length() + 200)) return false;
  cuerpo += "{\"usuario\":\"";
  agregarTextoJson(cuerpo, usuario);
  cuerpo += "\",\"archivo\":\"";
  agregarTextoJson(cuerpo, archivo);
  cuerpo += "\",\"resumen\":";
  cuerpo += resumen;
  cuerpo += "}";
  return true;
}

File TarjetaSD::abrirCsvEntrenamiento(const String &archivo) {
  return SD.open(rutaEntrenamiento(archivo, ".csv"), FILE_READ);
}

bool TarjetaSD::marcarEntrenamientoSincronizado(const String &archivo) {
  String ruta = rutaEntrenamiento(archivo, ".json");
  JsonDocument doc;
  if (!leerJson(ruta, doc)) return false;
  doc["sincronizado"] = true;
  return escribirJsonSeguro(ruta, doc, false);
}

// =============================================================================
// LIBERAR ESPACIO
// =============================================================================
// Clave para ordenar de la más vieja a la más nueva, que sirve para las dos carpetas:
//  - "SINHORA_n" (hechas cuando el equipo no tenía hora): primero, por n
//  - con fecha en el nombre (AAAAMMDD_HHMMSS): por fecha
//  - cualquier otro nombre: al final (no se sabe de cuándo es)
static String claveAntiguedad(const String &id) {
  int s = id.indexOf("SINHORA_");
  if (s >= 0) {
    int ini = s + 8, fin = ini;
    while (fin < (int)id.length() && isDigit(id[fin])) fin++;
    String num = id.substring(ini, fin);
    while (num.length() < 8) num = "0" + num;
    return "1" + num;
  }
  for (int i = 0; i + 15 <= (int)id.length(); i++) {
    bool fecha = (id[i + 8] == '_');
    for (int k = 0; fecha && k < 15; k++)
      if (k != 8 && !isDigit(id[i + k])) fecha = false;
    if (fecha) return "2" + id.substring(i, i + 8) + id.substring(i + 9, i + 15);
  }
  return "3" + id;
}

// Id de la prueba a la que pertenece un archivo: lo que va antes de "_PIM" o "_ESP"
static String idDeGrupo(const String &base) {
  int corte = base.length();
  int p = base.indexOf("_PIM");
  int e = base.indexOf("_ESP");
  if (p > 0 && p < corte) corte = p;
  if (e > 0 && e < corte) corte = e;
  return base.substring(0, corte);
}

std::vector<GrupoPruebas> TarjetaSD::gruposMasViejos(int max) {
  std::vector<GrupoPruebas> grupos;   // del más viejo al más nuevo, nunca más de 'max' (no hay RAM para todos)
  if (max <= 0 || !hayTarjeta()) return grupos;

  for (const char *carpeta : CARPETAS_PRUEBAS) {
    File dir = SD.open(carpeta);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); continue; }

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String nombre = String(f.name());
      bool esDir = f.isDirectory();
      f.close();
      nombre = nombre.substring(nombre.lastIndexOf('/') + 1);
      String minusculas = nombre;
      minusculas.toLowerCase();
      // "." = los crea la compu (ej. "._eval..." en una Mac); "_" = temporales de la PIM
      if (esDir || nombre.startsWith(".") || nombre.startsWith("_")) continue;
      if (!minusculas.endsWith(".json") && !minusculas.endsWith(".csv")) continue;

      String id = idDeGrupo(nombre.substring(0, nombre.lastIndexOf('.')));
      String clave = String(carpeta) + "/" + id;
      String ruta = String(carpeta) + "/" + nombre;

      bool yaEsta = false;
      for (GrupoPruebas &g : grupos) {
        if (g.id == clave) { g.rutas.push_back(ruta); yaEsta = true; break; }
      }
      if (yaEsta) continue;

      // Con la lista llena, solo entra si es más vieja que la más nueva de la lista (que se descarta).
      // Como la más nueva de la lista solo puede ir siendo más vieja, una prueba que se descartó nunca
      // vuelve a entrar con parte de sus archivos.
      String orden = claveAntiguedad(id);
      if ((int)grupos.size() >= max) {
        if (!(orden < grupos.back().orden)) continue;
        grupos.pop_back();
      }
      size_t pos = grupos.size();
      while (pos > 0 && orden < grupos[pos - 1].orden) pos--;
      GrupoPruebas g;
      g.id = clave;
      g.orden = orden;
      g.rutas.push_back(ruta);
      grupos.insert(grupos.begin() + pos, g);
    }
    dir.close();
  }
  return grupos;
}

bool TarjetaSD::grupoSincronizado(const GrupoPruebas &g) {
  JsonDocument filtro;
  filtro["sincronizado"] = true;
  int jsons = 0;
  for (const String &ruta : g.rutas) {
    String r = ruta;
    r.toLowerCase();
    if (!r.endsWith(".json")) continue;
    jsons++;
    File f = SD.open(ruta, FILE_READ);
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filtro));
    f.close();
    if (err || !(doc["sincronizado"] | false)) return false;   // un .json dañado no se pudo subir: no se borra
  }
  return jsons > 0;   // sin .json no se sabe si está en la web
}

bool TarjetaSD::borrarGrupo(const GrupoPruebas &g) {
  // Primero los .csv: si se corta a mitad, lo que queda tiene su .json (sincronizado) y se borra la próxima vez
  bool ok = true;
  for (int pasada = 0; pasada < 2; pasada++) {
    for (const String &ruta : g.rutas) {
      String r = ruta;
      r.toLowerCase();
      if (r.endsWith(".json") != (pasada == 1)) continue;
      if (!SD.remove(ruta)) ok = false;
    }
  }
  Serial.println(String("[SD] ") + (ok ? "Borrada: " : "No se pudo borrar del todo: ") + g.id);
  return ok;
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

std::vector<String> TarjetaSD::listarArchivos(const char *carpeta, bool *ok) {
  std::vector<String> rutas;
  if (ok) *ok = true;
  File dir = SD.open(carpeta);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    // Que la carpeta no exista es normal (todavía no se guardó nada). Que la tarjeta no responda, o que
    // la carpeta exista y no se pueda abrir, no: sin esto se confunde con "no hay archivos".
    if (ok && (!hayTarjeta() || SD.exists(carpeta))) *ok = false;
    return rutas;
  }
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
