#ifndef BRONQUITO_NET_H
#define BRONQUITO_NET_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#define MAX_REDES 8
#define MAX_REDES_RECORDADAS 5   // redes con la contraseña guardada (si hay más, se olvida la menos usada)
#define PUERTO_SERVIDOR 8000   // puerto de la web (FastAPI)
#define SIN_RED -1000          // llamarApi: no hay WiFi o no hay compu guardada

struct RedWifi {
  String ssid;
  int rssi;
};

enum ResultadoWifi {
  RES_WIFI_OK,
  RES_WIFI_FALLO,          // clave mal o red fuera de alcance
  RES_WIFI_INCOMPATIBLE    // conecta, pero la red no es del tipo /24
};

// Respuesta de POST /dispositivo/identificar
struct RespuestaIdentificar {
  bool ok = false;
  int http = 0;
  String error = "";
  long epoch = 0;
  int offsetMin = 0;
  int solicitudes = 0;
  String usuario = "";        // "" si la web no tiene paciente asignado
  String pinVersion = "";
};

struct RespuestaPin {
  int http = 0;
  bool ok = false;
  String error = "";
  String pinVersion = "";
  int intentosRestantes = -1;
  int retryAfter = 0;
};

// Se ocupa solo del WiFi. No dibuja nada y no sabe nada de pacientes.
class BronquitoNetwork {
public:
  // Abre la memoria y, si hay una red guardada, empieza a conectarse (sin bloquear)
  void begin();

  // Si no hay WiFi, vuelve a intentar con la red guardada (sin bloquear)
  void reconectarGuardada();

  // Busca redes compatibles (WPA/WPA2/WPA3 con clave, o abiertas). Las de empresa/
  // universidad (con usuario y contraseña) no aparecen. Ordenadas de más a menos señal.
  int escanear(RedWifi *redes, int max);

  // Conecta (bloqueante, hasta timeoutMs). Solo recuerda la red y su contraseña si conectó Y es compatible.
  ResultadoWifi conectar(const String &ssid, const String &clave, uint32_t timeoutMs = 15000);

  // true si ya se conectó antes a esa red: deja su contraseña en 'clave' (así no se vuelve a pedir)
  bool claveRecordada(const String &ssid, String &clave);

  // Inicio de la IP de esta red, sin el último número (ej: "10.200.0.")
  String prefijoRed() const {
    String ip = WiFi.localIP().toString();
    return ip.substring(0, ip.lastIndexOf('.') + 1);
  }
  // true si basta con el último número para armar la IP de la compu
  bool redEs24() {
      IPAddress m = WiFi.subnetMask();
      return m[0] == 255 && m[1] == 255 && m[2] == 255;   // /24 o más chica
  }

  // Le hace un ping al servidor (GET /ping). detalle = motivo del error si falla
  bool probarServidor(const String &ip, String &detalle);
  void guardarServidor(const String &ip) { prefs.putString("ip", ip); }
  String servidorGuardado() { return prefs.getString("ip", ""); }

  bool estaConectado() const { return WiFi.status() == WL_CONNECTED; }
  String ipLocal() const { return WiFi.localIP().toString(); }
  String ssidActual() const { return WiFi.SSID(); }

  bool enviarMensaje(const String& texto);                   // POST /boton
  bool pedirMensaje(String& mensaje, bool& compuViva);       // GET /mensaje (true si llegó algo)

  // Pedido a la web: http://<ip>:PUERTO_SERVIDOR<ruta>, con el header X-Device-Key.
  // Devuelve el código HTTP (negativo si falló la conexión, SIN_RED si no hay red/compu).
  // Si la respuesta trae JSON, queda en 'resp'.
  int llamarApi(const String& ip, const char* metodo, const String& ruta, const String& cuerpo, JsonDocument& resp,
                uint16_t timeoutMs = 5000);

  // POST /dispositivo/identificar. true si respondió 200 con ok:true.
  bool identificar(const String& ip, bool vacia, const String& usuarioActual, int pendientes, RespuestaIdentificar& r);

  RespuestaPin verificarPin(const String& usuario, const String& pin);

  // Pedidos que la web le dejó al equipo (GET /dispositivo/solicitudes). Devuelve el código HTTP
  int pedirSolicitudes(JsonDocument& resp);
  // POST /dispositivo/solicitudes/<id>/confirmar. true si respondió 200
  bool confirmarSolicitud(int id, bool ok, const char* detalle);

  // Frecuencia de entrenamiento del paciente (GET /dispositivo/frecuencia). Devuelve el código HTTP
  int pedirFrecuencia(const String& usuario, JsonDocument& resp);
  // POST /dispositivo/frecuencia/confirmar
  void confirmarFrecuencia(JsonVariantConst idFrecuencia);

  // POST /dispositivo/evaluaciones: sube una prueba de la SD (el cuerpo lo arma TarjetaSD::armarEnvio).
  // Devuelve el código HTTP. Si la web contesta un error, queda en resp["error"]
  int subirEvaluacion(const String& cuerpo, JsonDocument& resp);

  // Texto corto (sin acentos) para mostrar en pantalla según el código de llamarApi
  String textoError(int http);

  // Borra todas las redes recordadas y la compu guardada, y desconecta el WiFi
  void olvidarRedes();

private:
  Preferences prefs;
  // Redes recordadas en la memoria: "nRed" = cuántas, "rs<i>" = nombre, "rp<i>" = contraseña (la 0 es la última usada).
  // "ssid"/"pass" = la última red, a la que se reconecta al prender.
  void recordarRed(const String &ssid, const String &clave);
};

#endif