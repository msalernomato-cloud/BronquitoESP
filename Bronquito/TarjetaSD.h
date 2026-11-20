#ifndef TARJETA_SD_H
#define TARJETA_SD_H

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "RespiratorySensors.h"
#include "AnalisisEspiro.h"
#include "AnalisisPIM.h"
#include "AnalisisEntrenamiento.h"

// =============================================================================
// TARJETA microSD
// -----------------------------------------------------------------------------
// Es la ÚNICA librería que lee o escribe la tarjeta. Las demás le piden las
// cosas a ella y nunca llaman a SD.open(), SD.remove(), etc.
//
// Qué hay en la tarjeta:
//   /perfil.json                 datos del paciente (incluye el PIN)
//   /CONFIGURACION.JSON          lo que manda la web ("frecuencia", "resistencia_web", "predichos") y el
//                                nivel de resistencia que calcula el equipo ("resistencia_auto")
//   /EVALUACIONES/<id>.json      resumen de cada medición (métricas, validación, ambiente)
//   /EVALUACIONES/<id>.csv       señal completa de cada medición
//   /ENTRENAMIENTOS/<id>.csv     señal completa de cada sesión de entrenamiento
//   /ENTRENAMIENTOS/<id>.json    estado de la sesión, features de cada ciclo y resumen
//
// Bus SPI propio, conector J8: CS 19, SCK 22, MISO 23, MOSI 21.
// =============================================================================

// ---- Estado de la tarjeta y del paciente ----------------------------------
enum EstadoTarjeta {
  TARJETA_OK,              // hay tarjeta y un paciente cargado
  TARJETA_NO_ESTA,         // no hay tarjeta o no responde
  TARJETA_SIN_PACIENTE,    // hay tarjeta pero no existe /perfil.json
  TARJETA_PERFIL_DANADO    // /perfil.json existe pero no se puede leer
};

// ---- Resultado de borrar todo (preparar el equipo para otro paciente) ------
enum ResultadoFormato {
  FORMATO_OK,
  FORMATO_SIN_TARJETA,
  FORMATO_HAY_PENDIENTES,  // hay mediciones sin subir a la web: no se borró nada
  FORMATO_ERROR            // algún archivo no se pudo borrar
};

// ---- Datos del paciente (/perfil.json) -------------------------------------
// Mismos nombres y mismo orden que las claves del archivo.
struct PerfilPaciente {
  String usuario;          // "msalerno": el usuario del paciente en la web
  String nombre;
  String apellido;
  String pin_version;      // cambia cada vez que el médico cambia el PIN desde la web
  String sexo;             // "F" / "M"
  String altura;           // cm
  String peso;             // kg
  String fumador;          // "s" / "n"
  String nacimiento;       // "DD/MM/AAAA"
  String registro;         // "DD/MM/AAAA"
  String enfermedad;
  String pin;              // lo agrega el equipo cuando la web confirma el PIN
};

// ---- Frecuencia de entrenamiento (/CONFIGURACION.JSON) ---------------------
struct FrecuenciaGuardada {
  int sesiones = 0;        // 0 = no hay ninguna guardada
  bool dias[7] = {};       // 0 = lunes ... 6 = domingo
  String vigenteDesde;     // "AAAA-MM-DD"
};

// ---- Nivel de resistencia del entrenamiento (/CONFIGURACION.JSON) ---------
// Cada sección tiene UN solo dueño, así la web y el equipo nunca escriben el mismo dato:
//   "resistencia_web"  la manda la web: modo ("automatico" | "manual") y el nivel que fijó el médico
//   "predichos"        la manda la web: FVC y PIM predichas del paciente (cambian con la edad)
//   "resistencia_auto" la escribe el equipo después de cada evaluación (ver NivelResistencia.h)
struct ResistenciaWeb {
  bool manual = false;           // false = automático (también si la web todavía no mandó nada)
  int nivelManual = 0;           // 1 (válvula más abierta) a 5 (más cerrada); 0 = no hay
  String enviado;                // "AAAA-MM-DD"
};

struct Predichos {
  float fvc_L = NAN;             // NAN = la web no lo mandó (o mandó null)
  float pim_cmH2O = NAN;
};

struct ResistenciaAuto {
  bool hay = false;              // false = todavía no hubo ninguna evaluación válida (PIM + espirometría)
  int nivel = 0;                 // 1 a 5; 0 = no se pudo calcular (faltan los predichos)
  float puntajeS = NAN, pctPim = NAN, pctFvc = NAN;
  float pim_cmH2O = NAN, fvc_L = NAN;   // la mejor PIM y la mejor FVC de ese día
  String fechaEvaluacion;        // "AAAA-MM-DD"
  int vigenciaDias = 0;
  String vence;                  // "AAAA-MM-DD"
};

// Lo que se usa para entrenar, combinando las tres secciones (TarjetaSD::estadoResistencia)
struct EstadoResistencia {
  bool manual = false;           // nivel fijado por el médico: no vence
  int nivel = 0;                 // el que se usa (0 = no hay)
  bool hayEvaluacion = false;    // hay una evaluación válida guardada
  String fechaEvaluacion;        // "AAAA-MM-DD" de la última evaluación válida
  String vence;                  // "AAAA-MM-DD" (vacío en modo manual)
  int vigenciaDias = 0;
  bool hayFechaHoy = false;      // false = el equipo no tiene la hora: no se puede saber si venció
  int diasRestantes = 0;         // hasta "vence" (negativo = venció); vale solo si hayFechaHoy
  bool puedeEntrenar = false;    // manual, o automático con una evaluación vigente (o sin fecha para verificarlo)
};

// ---- Temperatura y humedad al medir (DHT22) y factor BTPS aplicado ---------
struct CondicionesAmbiente {
  bool  valida = false;          // false = no se pudo leer el sensor (no se corrigió)
  float temperatura_c = 0.0f;
  float humedad_rel_pct = 0.0f;
  float factor_btps = 1.0f;
};

// ---- Resumen de una medición leído del .json (para el historial) -----------
// Los valores que no están en el archivo quedan en NAN (la pantalla muestra "--").
struct ResumenMedicion {
  bool  esPim = false;
  bool  valida = true;
  int   codigo = 0;           // código E de la tabla de criterios (0 = sin error)
  String error;               // texto del error, si no validó
  // Espirometría
  float fvc_L = NAN, fev1_L = NAN, pef_Lmin = NAN, fivc_L = NAN, fiv1_L = NAN, pif_Lmin = NAN;
  // PIM
  float pimSostenida_cmH2O = NAN, pico_cmH2O = NAN;
  int   intentos = 0, intentoInformado = 0;
  // Ambiente
  float temperatura_c = NAN, humedad_pct = NAN;
};

// ---- Resumen de un entrenamiento leído del .json (para el historial) --------
// Solo las features globales (el "resumen" del .json), sin las de cada ciclo. Lo que no está en el
// archivo (o es null) queda en NAN / 0 y la pantalla muestra "--".
struct ResumenEntrenamiento {
  String estado, fin;                 // "completa" | "incompleta" | "interrumpida" ; por qué terminó
  int   ciclosObjetivo = 0;
  float duracion_s = NAN;
  bool  hayFeatures = false;          // false: el resumen quedó en null (no se pudo analizar)
  // Promedios por ciclo
  float ti_s = NAN, te_s = NAN, pausa_s = NAN, ti_ttot = NAN;
  float pif_Lmin = NAN, pef_Lmin = NAN, volInsp_L = NAN, volEsp_L = NAN;
  float pim_cmH2O = NAN, pem_cmH2O = NAN, trabajoInsp_J = NAN, potenciaInsp_W = NAN;
  // Multiciclo
  int   nCiclos = 0, nFasesSueltas = 0;
  float periodo_s = NAN, frecuencia_rpm = NAN, ventilacionMinuto_Lmin = NAN, trabajoTotalInsp_J = NAN;
  float pendientePim = NAN, caidaPim_pct = NAN;
  float pifMax_Lmin = NAN, pefMax_Lmin = NAN, pimMax_cmH2O = NAN, pemMax_cmH2O = NAN;
  int   pifMaxCiclo = 0, pefMaxCiclo = 0, pimMaxCiclo = 0, pemMaxCiclo = 0;   // 0 = no se sabe
};

// ---- Señal de una medición leída del .csv (para graficar) ------------------
struct SerieMedicion {
  std::vector<float> t, p, q, v;   // tiempo (s), presión (cmH2O), flujo (L/min), volumen (L)
  std::vector<float> ve;           // volumen SOLO de la espiración (L): para la curva volumen-tiempo
};

// Archivos de una misma prueba, que se borran siempre juntos: una calibración (<id>_PIM, <id>_ESP y sus
// intentos fallidos <id>_ESP_int<n>_E<c>, cada uno con su .json y su .csv) o un entrenamiento.
struct GrupoPruebas {
  String id;                    // carpeta + id (ej. "/EVALUACIONES/eval_20260901_091500")
  String orden;                 // para ordenar de la más vieja a la más nueva
  std::vector<String> rutas;    // rutas completas de sus archivos
};

class TarjetaSD {
public:
  TarjetaSD(uint8_t pinCS = 19, uint8_t pinSCK = 22, uint8_t pinMISO = 23, uint8_t pinMOSI = 21);

  // =========================================================================
  // TARJETA Y PACIENTE
  // =========================================================================

  // Monta la tarjeta (si hace falta) y lee /perfil.json. Con mostrarDetalle = true
  // explica por el monitor serie qué encontró.
  EstadoTarjeta leerPerfil(bool mostrarDetalle = true);

  bool hayTarjeta();                                 // la tarjeta sigue puesta y responde
  EstadoTarjeta estado() const   { return estadoActual; }   // resultado de la última lectura
  bool hayPaciente() const       { return estadoActual == TARJETA_OK; }

  const PerfilPaciente &paciente() const { return perfil; }
  String usuario() const    { return perfil.usuario; }       // "" si no hay paciente
  String pin() const        { return perfil.pin; }
  String pinVersion() const { return perfil.pin_version; }
  bool faltaPin() const     { return perfil.pin.length() == 0; }   // hay que verificarlo con la web
  String textoLogin() const;   // lo que se muestra en el login: el usuario, "Sin sincronizar", "Sin microSD" o "Perfil dañado"

  bool guardarPerfil(JsonVariantConst perfilWeb);    // paciente nuevo que mandó la web
  bool guardarPin(const String &pin, const String &version);

  // =========================================================================
  // CONFIGURACIÓN
  // =========================================================================
  bool guardarFrecuencia(JsonVariantConst frecuencia);   // pisa solo esa clave del archivo
  FrecuenciaGuardada leerFrecuencia();
  bool guardarResistenciaWeb(JsonVariantConst resistencia);   // pisa solo "resistencia_web"
  bool guardarPredichos(JsonVariantConst predichos);           // pisa solo "predichos"
  ResistenciaWeb leerResistenciaWeb();
  Predichos leerPredichos();
  ResistenciaAuto leerResistenciaAuto();
  // Recalcula "resistencia_auto" con las evaluaciones de /EVALUACIONES: del día más reciente que tenga
  // al menos una PIM y una espirometría válidas, la mejor PIM y la mejor FVC. Llamarla después de cada
  // evaluación y cuando llegan predichos nuevos. Si no hay ninguna evaluación válida, no cambia nada.
  bool actualizarResistenciaAuto();
  // Nivel que se usa y si se puede entrenar. fechaHoy = Reloj::fechaHoraActual() ("sin hora" si no hay)
  EstadoResistencia estadoResistencia(const String &fechaHoy);

  // =========================================================================
  // MEDICIONES (se guardan al terminar cada maniobra)
  // =========================================================================

  // Identificador de una calibración nueva: "eval_AAAAMMDD_HHMMSS",
  // o "eval_SINHORA_n_xxxxxx" si no hay una hora válida (no se repite nunca: ver nuevoIdSinHora).
  String nuevoIdCalibracion(const String &fechaHora);

  // Una calibración se guarda como varios archivos con el mismo id (los nombres los arma esta librería):
  //   <id>_PIM                     la sesión de PIM
  //   <id>_ESP                     la espirometría aceptada
  //   <id>_ESP_int<n>_E<código>    cada espirometría que no validó (se guarda para poder revisarla)
  bool guardarPIM(const String &idCalibracion, const String &fechaHora, const ResumenPIM &pim,
                  const CondicionesAmbiente &amb);   // usa el .csv temporal del intento informado
  bool guardarPIMenCero(const String &idCalibracion, const String &fechaHora,
                        const CondicionesAmbiente &amb);   // equipo sin sensor de PIM
  bool guardarEspirometria(const String &idCalibracion, const String &fechaHora,
                           const std::vector<MuestraEspiro> &datos, const EventoEspiratorio &ev,
                           const CondicionesAmbiente &amb);
  bool guardarEspirometriaFallida(const String &idCalibracion, int intento, const String &fechaHora,
                                  const std::vector<MuestraEspiro> &datos, const EventoEspiratorio &ev,
                                  const CondicionesAmbiente &amb);
  void borrarPIM(const String &idCalibracion);
  void borrarEspirometria(const String &idCalibracion);
  void borrarCalibracion(const String &idCalibracion);   // TODOS los archivos <id>_... (PIM, ESP y fallidas)

  // PIM: cada intento va a un .csv temporal (no entran los 8 en RAM). guardarPIM() usa el del intento
  // informado y borra el resto. Los temporales no salen en el historial.
  bool guardarCsvTemporalPIM(int intento, const std::vector<MuestraEspiro> &datos);
  void borrarTemporalesPIM();

  // =========================================================================
  // ENTRENAMIENTOS (/ENTRENAMIENTOS/<id>.csv y <id>.json)
  // -------------------------------------------------------------------------
  // La sesión no entra en RAM: el .csv se escribe de a bloques mientras se mide, en un temporal
  // <id>.csv.tmp. Al terminar, cargarSenalEntrenamiento() lo vuelve a leer para calcular las
  // features, y guardarEntrenamiento() lo renombra y escribe el .json (con las features si las hay);
  // descartarEntrenamiento() borra todo lo de esa sesión.
  // =========================================================================
  String nuevoIdEntrenamiento(const String &fechaHora);   // "ent_AAAAMMDD_HHMMSS" o "ent_SINHORA_n_xxxxxx"
  bool iniciarEntrenamiento(const String &id);            // crea el .csv.tmp con el encabezado
  bool agregarMuestrasEntrenamiento(const MuestraEspiro *muestras, size_t cantidad);
  bool cerrarEntrenamiento();                             // cierra el .csv.tmp (false si falló la escritura)
  // Lee el .csv.tmp ya cerrado. false si no hay memoria para la señal y el análisis, o si falla la lectura.
  bool cargarSenalEntrenamiento(const String &id, AnalisisEntrenamiento::SenalEntrenamiento &senal);
  // Reescribe el .csv.tmp con una columna más, flujo_proc_lmin: el flujo procesado del análisis (una por
  // cada fila que leyó cargarSenalEntrenamiento). Vacía si flujoProc está vacío (no se pudo analizar).
  // Si falla, el .csv.tmp queda como estaba (sin la columna).
  bool agregarFlujoProcesado(const String &id, const std::vector<int16_t> &flujoProc);
  // estado: "completa" | "incompleta" | "interrumpida". fin: por qué terminó ("objetivo", "detenida",
  // "sin_respiracion", "tiempo_maximo"). features == nullptr: no se pudieron calcular (van en null).
  // fechaHora: la de Reloj::fechaHoraActual() al empezar (si no hay hora, no se guarda)
  // amb: temperatura, humedad y factor BTPS aplicado (si no se pudo leer el DHT22 van en null)
  // nivelResistencia / resistenciaManual: con qué nivel se entrenó y quién lo fijó (0 = no había nivel)
  bool guardarEntrenamiento(const String &id, const String &fechaHora, const CondicionesAmbiente &amb,
                            int nivelResistencia, bool resistenciaManual, float duracion_s, float dcServo,
                            const char *estado, const char *fin, int ciclosObjetivo,
                            const AnalisisEntrenamiento::FeaturesEntrenamiento *features);
  bool descartarEntrenamiento(const String &id);

  // =========================================================================
  // HISTORIAL
  // =========================================================================

  // Mediciones guardadas, de la más nueva a la más vieja (hasta 'max'). -1 si no hay tarjeta.
  int listarMediciones(String *ids, String *fechas, String *tipos, int max);
  bool leerResumen(const String &id, ResumenMedicion &resumen);   // el .json de una medición
  bool cargarSerie(const String &id, SerieMedicion &serie); // el .csv, con el volumen calculado

  // Entrenamientos guardados, del más nuevo al más viejo (hasta 'max'). -1 si no hay tarjeta.
  // tipos = "ciclos hechos/objetivo" (ej. "12/30 ciclos")
  int listarEntrenamientos(String *ids, String *fechas, String *tipos, int max);
  bool leerResumenEntrenamiento(const String &id, ResumenEntrenamiento &resumen);   // el .json

  // =========================================================================
  // MANTENIMIENTO
  // =========================================================================
  int contarPendientes();                            // mediciones con "sincronizado": false
  ResultadoFormato borrarTodo(bool forzar = false);  // sin forzar, no borra si hay pendientes
  // Espacio de la microSD en bytes (0 si no hay tarjeta). La primera vez puede tardar en tarjetas grandes
  uint64_t espacioTotal();
  uint64_t espacioLibre();

  // =========================================================================
  // SUBIDA A LA WEB (ver FlujoConexion::subirPendientes)
  // =========================================================================
  // Pruebas que todavía no se subieron ("sincronizado": false), por nombre de archivo sin la extensión.
  // Hasta max. -> vacío si no hay tarjeta
  std::vector<String> listarPendientes(int max);
  // Cuerpo de POST /dispositivo/evaluaciones: {"usuario", "archivo", "resumen": <el .json>, "csv": "<el .csv>"}.
  // El .csv se agrega línea por línea para no tenerlo dos veces en memoria. false si no se pudo leer
  bool armarEnvio(const String &archivo, const String &usuario, String &cuerpo);
  // Pone "sincronizado": true en el .json, para no volver a subirla
  bool marcarSincronizado(const String &archivo);

  // Entrenamientos (ver FlujoConexion::subirPendientes). El .csv de una sesión puede tener cientos de KB:
  // no entra en RAM, así que se sube aparte, leyéndolo directo de la SD (BronquitoNetwork::subirCsvEntrenamiento)
  std::vector<String> listarEntrenamientosPendientes(int max);   // ids sin "sincronizado": true
  // Cuerpo de POST /dispositivo/entrenamientos: {"usuario", "archivo", "resumen": <el .json>} (sin el .csv)
  bool armarEnvioEntrenamiento(const String &archivo, const String &usuario, String &cuerpo);
  File abrirCsvEntrenamiento(const String &archivo);             // para subirlo; cerrarlo después
  bool marcarEntrenamientoSincronizado(const String &archivo);

  // =========================================================================
  // LIBERAR ESPACIO (ver EspacioSD)
  // =========================================================================
  // Las 'max' pruebas más viejas de /EVALUACIONES y /ENTRENAMIENTOS, de la más vieja a la más nueva.
  // Vacío si no hay tarjeta
  std::vector<GrupoPruebas> gruposMasViejos(int max);
  // true si todos sus .json dicen "sincronizado": true (y tiene al menos uno). Si no, no se puede borrar
  bool grupoSincronizado(const GrupoPruebas &g);
  // Borra todos sus archivos (primero los .csv y al final los .json). false si alguno no se pudo borrar
  bool borrarGrupo(const GrupoPruebas &g);

private:
  uint8_t pinCS, pinSCK, pinMISO, pinMOSI;
  SPIClass bus;
  bool montada = false;
  PerfilPaciente perfil;
  EstadoTarjeta estadoActual = TARJETA_NO_ESTA;
  fs::File archivoEntrenamiento;       // .csv.tmp de la sesión en curso
  bool errorEntrenamiento = false; // falló alguna escritura de la sesión en curso
  size_t muestrasEntrenamiento = 0; // muestras escritas en la sesión en curso (para reservar la memoria justa)

  bool montar();
  void olvidarPaciente(EstadoTarjeta motivo);

  // Escribe <id>.csv (si 'datos' no está vacío) y <id>.json. Si falla el .json, borra el .csv.
  //   ev  != nullptr: espirometría (si no validó se guarda igual, con su código de error)
  //   pim != nullptr: sesión de PIM (valor informado y todos los intentos)
  bool guardarMedicion(const String &id, const String &idCalibracion, const char *tipo, const String &fechaHora,
                       const std::vector<MuestraEspiro> &datos, bool pimEnCero, const EventoEspiratorio *ev,
                       const CondicionesAmbiente *amb = nullptr, const ResumenPIM *pim = nullptr);
  bool usarCsvTemporalPIM(int intento, const String &id);
  void borrarMedicion(const String &id);             // borra <id>.csv y <id>.json

  // Lectura y escritura de archivos
  bool leerJson(const String &ruta, JsonDocument &doc);
  bool escribirJsonSeguro(const String &ruta, JsonDocument &doc, bool legible = true);
  bool escribirCsv(const String &ruta, const std::vector<MuestraEspiro> &datos);
  std::vector<String> listarArchivos(const char *carpeta);

  static String rutaMedicion(const String &id, const char *extension);
  static String rutaTemporalPIM(int intento);
  static String fechaLegible(const String &fechaHora, const String &id);
};

#endif
