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

// =============================================================================
// TARJETA microSD
// -----------------------------------------------------------------------------
// Es la ÚNICA librería que lee o escribe la tarjeta. Las demás le piden las
// cosas a ella y nunca llaman a SD.open(), SD.remove(), etc.
//
// Qué hay en la tarjeta:
//   /perfil.json                 datos del paciente (incluye el PIN)
//   /CONFIGURACION.JSON          frecuencia de entrenamiento que indicó el médico
//   /EVALUACIONES/<id>.json      resumen de cada medición (métricas, validación, ambiente)
//   /EVALUACIONES/<id>.csv       señal completa de cada medición
//   /ENTRENAMIENTOS/             (reservada para las sesiones de entrenamiento)
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

// ---- Señal de una medición leída del .csv (para graficar) ------------------
struct SerieMedicion {
  std::vector<float> t, p, q, v;   // tiempo (s), presión (cmH2O), flujo (L/min), volumen (L)
  std::vector<float> ve;           // volumen SOLO de la espiración (L): para la curva volumen-tiempo
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
  String textoLogin() const;   // lo que se muestra en el login: el usuario, "Sin sincronizar" o "Perfil dañado"

  bool guardarPerfil(JsonVariantConst perfilWeb);    // paciente nuevo que mandó la web
  bool guardarPin(const String &pin, const String &version);

  // =========================================================================
  // CONFIGURACIÓN
  // =========================================================================
  bool guardarFrecuencia(JsonVariantConst frecuencia);   // pisa solo esa clave del archivo
  FrecuenciaGuardada leerFrecuencia();

  // =========================================================================
  // MEDICIONES (se guardan al terminar cada maniobra)
  // =========================================================================

  // Identificador de una calibración nueva: "eval_AAAAMMDD_HHMMSS",
  // o "eval_SINHORA_n" si no hay una hora válida.
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
  // HISTORIAL
  // =========================================================================

  // Mediciones guardadas, de la más nueva a la más vieja (hasta 'max'). -1 si no hay tarjeta.
  int listarMediciones(String *ids, String *fechas, String *tipos, int max);
  bool leerResumen(const String &id, ResumenMedicion &resumen);   // el .json de una medición
  bool cargarSerie(const String &id, SerieMedicion &serie); // el .csv, con el volumen calculado

  // =========================================================================
  // MANTENIMIENTO
  // =========================================================================
  int contarPendientes();                            // mediciones con "sincronizado": false
  ResultadoFormato borrarTodo(bool forzar = false);  // sin forzar, no borra si hay pendientes

private:
  uint8_t pinCS, pinSCK, pinMISO, pinMOSI;
  SPIClass bus;
  bool montada = false;
  PerfilPaciente perfil;
  EstadoTarjeta estadoActual = TARJETA_NO_ESTA;

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
