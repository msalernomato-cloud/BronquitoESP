#ifndef EVALUACIONES_H
#define EVALUACIONES_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "Storage.h"
#include "RespiratorySensors.h"

// Señal completa de una evaluación, leída del .csv
// Volumen = integral del flujo (L/min -> L), con signo: sube al exhalar y baja al inhalar
struct SerieEval {
  std::vector<float> t, p, q, v;   // tiempo (s), presión (cmH2O), flujo (L/min), volumen (L)
};

// Temperatura y humedad del ambiente (DHT22) y factor BTPS que se aplicó
struct CondicionesAmbiente {
  bool  valida = false;          // false = no se pudo leer el sensor (no se corrigió)
  float temperatura_c = 0.0f;
  float humedad_rel_pct = 0.0f;
  float factor_btps = 1.0f;
};

// Archivos de /EVALUACIONES en la SD: <id>.csv (señal) y <id>.json (métricas).
// No dibuja nada: solo lee y escribe la tarjeta.
class Evaluaciones {
public:
  explicit Evaluaciones(BronquitoStorage &storage);

  // id nuevo a partir de la fecha y hora: "eval_AAAAMMDD_HHMMSS", o "eval_SINHORA_n" si no es válida
  String generarId(const String &fechaHora);

  // Evaluaciones guardadas, de la más nueva a la más vieja (hasta max). -1 si no hay tarjeta
  int listar(String *ids, String *fechas, String *tipos, int max);

  // Lee el .json de una evaluación. false si no existe o está dañado
  bool leerResumen(const String &id, JsonDocument &doc);

  // Lee el .csv de una evaluación y calcula el volumen (dentro de la ventana del evento, si la hay)
  bool cargarSerie(const String &id, SerieEval &s);

  // Escribe <id>.csv y <id>.json. Si falla el .json, borra el .csv.
  // Si 'datos' está vacío no escribe el .csv (se usa cuando ya se movió uno temporal con usarCsvTemporalPIM).
  // ev != nullptr = espirometría: si no es válida se guarda igual, con "valida": false, el código de
  // error y las métricas en null (el .csv completo queda para ver qué pasó).
  // pim != nullptr = sesión de PIM: valor informado, todos los intentos y cuáles fueron reproducibles.
  bool guardarManiobra(const String &id, const String &calibId, const char *tipo, const String &fechaHora,
                       const std::vector<MuestraEspiro> &datos, bool pimEnCero, const EventoEspiratorio *ev,
                       const CondicionesAmbiente *amb = nullptr, const ResumenPIM *pim = nullptr);

  // PIM: cada intento se guarda en un .csv temporal (no hay RAM para tener los 8); al final, el del intento
  // informado pasa a ser <id>.csv y el resto se borra. Los temporales no aparecen en el historial.
  bool guardarCsvTemporalPIM(int intento, const std::vector<MuestraEspiro> &datos);
  bool usarCsvTemporalPIM(int intento, const String &id);
  void borrarTemporalesPIM();

  // Borra <id>.csv y <id>.json
  void borrar(const String &id);

  // SUBIDA A LA WEB (ver FlujoConexion::subirPendientes)
  // Pruebas que todavía no se subieron ("sincronizado": false), por nombre de archivo sin la extensión.
  // Hasta max. -> vacío si no hay tarjeta
  std::vector<String> listarPendientes(int max);
  // Cuerpo de POST /dispositivo/evaluaciones: {"usuario", "archivo", "resumen": <el .json>, "csv": "<el .csv>"}.
  // El .csv se agrega línea por línea para no tenerlo dos veces en memoria. false si no se pudo leer
  bool armarEnvio(const String &archivo, const String &usuario, String &cuerpo);
  // Pone "sincronizado": true en el .json, para no volver a subirla
  bool marcarSincronizado(const String &archivo);

private:
  BronquitoStorage &storage;

  static String fechaLegible(const String &fechaHora, const String &id);
  static bool escribirCsv(const String &ruta, const std::vector<MuestraEspiro> &datos);
  static String rutaTemporalPIM(int intento);
};

#endif
