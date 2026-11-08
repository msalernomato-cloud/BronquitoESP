#include "BronquitoNet.h"
#include <HTTPClient.h>
#include <sys/time.h>
#include <time.h>
#include "Secretos.h"

void BronquitoNetwork::begin() {
  prefs.begin("bronquito", false);
  WiFi.persistent(false);          // la red la guardamos nosotros, no el sistema
  WiFi.mode(WIFI_STA);
  reconectarGuardada();
}

// Empieza a conectarse a la red guardada. Si no está cerca, el core lo reintenta solo
// en segundo plano hasta que aparezca (escanear() frena esos reintentos).
void BronquitoNetwork::reconectarGuardada() {
  if (estaConectado()) return;
  String ssid = prefs.getString("ssid", "");
  if (ssid.length() > 0) {
    String clave = prefs.getString("pass", "");
    Serial.println("[NET] Conectando a la red guardada: " + ssid);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), clave.c_str());
  } else {
    Serial.println("[NET] No hay red guardada.");
  }
}

// Solo dejamos pasar redes con clave común (o abiertas). Las de empresa/universidad
// (WPA2/WPA3-Enterprise) piden usuario y contraseña, y no las soportamos.
static bool tipoCompatible(wifi_auth_mode_t t) {
  return t == WIFI_AUTH_OPEN ||
         t == WIFI_AUTH_WPA_PSK ||
         t == WIFI_AUTH_WPA2_PSK ||
         t == WIFI_AUTH_WPA_WPA2_PSK ||
         t == WIFI_AUTH_WPA3_PSK ||
         t == WIFI_AUTH_WPA2_WPA3_PSK;
}

int BronquitoNetwork::escanear(RedWifi *redes, int max) {
  // Mientras la ESP intenta conectarse (ej. a la red guardada, que no está cerca) el
  // escaneo falla. Se corta ese intento y, si igual falla, se reintenta.
  int n = WIFI_SCAN_FAILED;
  for (int intento = 0; intento < 3 && n < 0; intento++) {
    if (!estaConectado()) {
      WiFi.setAutoReconnect(false);   // si no, el core vuelve a intentar solo
      WiFi.disconnect();
      delay(intento == 0 ? 100 : 500);
    }
    n = WiFi.scanNetworks();
    if (n < 0) Serial.printf("[NET] Fallo el escaneo (%d)\n", n);
  }
  if (n < 0) n = 0;

  RedWifi tmp[20];
  int cant = 0;
  for (int i = 0; i < n && cant < 20; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;                          // redes ocultas
    if (!tipoCompatible(WiFi.encryptionType(i))) continue;  // redes de empresa/universidad
    bool repetida = false;
    for (int j = 0; j < cant; j++) {
      if (tmp[j].ssid == s) {                               // misma red en varias antenas: la más fuerte
        if (WiFi.RSSI(i) > tmp[j].rssi) tmp[j].rssi = WiFi.RSSI(i);
        repetida = true;
        break;
      }
    }
    if (!repetida) { tmp[cant].ssid = s; tmp[cant].rssi = WiFi.RSSI(i); cant++; }
  }
  WiFi.scanDelete();

  // ordenar de mayor a menor señal
  for (int a = 0; a < cant; a++)
    for (int b = a + 1; b < cant; b++)
      if (tmp[b].rssi > tmp[a].rssi) { RedWifi t = tmp[a]; tmp[a] = tmp[b]; tmp[b] = t; }

  int total = (cant < max) ? cant : max;
  for (int i = 0; i < total; i++) redes[i] = tmp[i];
  Serial.printf("[NET] %d redes compatibles encontradas\n", total);
  return total;
}

ResultadoWifi BronquitoNetwork::conectar(const String &ssid, const String &clave, uint32_t timeoutMs) {
  WiFi.disconnect();
  delay(100);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), clave.c_str());

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] No se pudo conectar a " + ssid);
    WiFi.setAutoReconnect(false);   // que no siga probando sola con esa clave (y se pueda escanear)
    WiFi.disconnect();
    return RES_WIFI_FALLO;
  }

  if (!redEs24()) {
    Serial.println("[NET] Red no compatible (máscara " + WiFi.subnetMask().toString() + ")");
    WiFi.disconnect();
    return RES_WIFI_INCOMPATIBLE;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", clave);
  Serial.println("[NET] Conectado. IP: " + WiFi.localIP().toString());
  return RES_WIFI_OK;
}

bool BronquitoNetwork::probarServidor(const String &ip, String &detalle) {
  HTTPClient http;
  http.setTimeout(3000);
  http.begin("http://" + ip + ":" + String(PUERTO_SERVIDOR) + "/ping");
  int codigo = http.GET();
  String resp = (codigo > 0) ? http.getString() : "";
  http.end();
  Serial.printf("[NET] ping %s -> codigo=%d resp=%s\n", ip.c_str(), codigo, resp.c_str());

  if (codigo == 200 && resp == "pong") { detalle = ""; return true; }
  detalle = (codigo > 0) ? ("HTTP " + String(codigo)) : HTTPClient::errorToString(codigo);
  return false;
}

// Envía un texto a la compu. true si la compu respondió 200.
bool BronquitoNetwork::enviarMensaje(const String& texto) {
  String ip = servidorGuardado();
  if (!estaConectado() || ip.length() == 0) return false;

  HTTPClient http;
  http.setConnectTimeout(700);
  http.setTimeout(700);
  http.begin("http://" + ip + ":" + String(PUERTO_SERVIDOR) + "/boton");
  http.addHeader("Content-Type", "text/plain");
  int codigo = http.POST(texto);
  http.end();
  return codigo == 200;
}

// Pregunta si la compu tiene un mensaje para la ESP.
// Devuelve true si llegó un mensaje (queda en 'mensaje').
// 'compuViva' queda en true si la compu contestó (200 o 204 = "nada nuevo").
bool BronquitoNetwork::pedirMensaje(String& mensaje, bool& compuViva) {
  mensaje = "";
  compuViva = false;
  String ip = servidorGuardado();
  if (!estaConectado() || ip.length() == 0) return false;

  HTTPClient http;
  http.setConnectTimeout(700);
  http.setTimeout(700);
  http.begin("http://" + ip + ":" + String(PUERTO_SERVIDOR) + "/mensaje");
  int codigo = http.GET();
  if (codigo == 200) mensaje = http.getString();
  http.end();

  compuViva = (codigo == 200 || codigo == 204);
  return codigo == 200;
}

// ---------------------------------------------------------------------------
// API DE LA WEB
// ---------------------------------------------------------------------------
// El cuerpo no se imprime en el monitor serie (puede llevar el PIN).
int BronquitoNetwork::llamarApi(const String& ip, const char* metodo, const String& ruta,
                                const String& cuerpo, JsonDocument& resp, uint16_t timeoutMs) {
  resp.clear();
  if (!estaConectado() || ip.length() == 0) return SIN_RED;

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(timeoutMs);
  if (!http.begin("http://" + ip + ":" + String(PUERTO_SERVIDOR) + ruta)) return SIN_RED;
  http.addHeader("X-Device-Key", CLAVE_DISPOSITIVO);
  if (cuerpo.length() > 0) http.addHeader("Content-Type", "application/json");

  int codigo = http.sendRequest(metodo, cuerpo);
  if (codigo > 0) {
    String texto = http.getString();
    if (texto.length() > 0) deserializeJson(resp, texto);
  }
  http.end();
  Serial.printf("[NET] %s %s -> %d\n", metodo, ruta.c_str(), codigo);
  return codigo;
}

bool BronquitoNetwork::identificar(const String& ip, bool vacia, const String& usuarioActual,
                                   int pendientes, RespuestaIdentificar& r) {
  JsonDocument cuerpo;
  cuerpo["vacia"] = vacia;
  if (usuarioActual.length() > 0) cuerpo["usuario_actual"] = usuarioActual;
  else                            cuerpo["usuario_actual"] = nullptr;
  cuerpo["archivos_pendientes"] = pendientes;
  String enviar;
  serializeJson(cuerpo, enviar);

  JsonDocument resp;
  r = RespuestaIdentificar();
  r.http = llamarApi(ip, "POST", "/dispositivo/identificar", enviar, resp);
  r.ok = (r.http == 200) && (resp["ok"] | false);
  r.error = String(resp["error"] | "");
  r.epoch = resp["epoch"] | 0L;
  r.offsetMin = resp["offset_minutos"] | 0;
  r.solicitudes = resp["solicitudes_pendientes"] | 0;
  r.usuario = String(resp["usuario"] | "");
  r.pinVersion = String(resp["pin_version"] | "");
  return r.ok;
}

String BronquitoNetwork::textoError(int http) {
  if (http == SIN_RED) return estaConectado() ? "Falta conectar la computadora" : "Sin Wi-Fi";
  if (http < 0)    return "La computadora no responde";
  if (http == 200) return "Respuesta inválida";
  if (http == 400) return "Datos inválidos";
  if (http == 401) return "Clave del equipo inválida";
  if (http == 404) return "No encontrado";
  if (http == 503) return "Equipo no configurado en la web";
  return "Error HTTP " + String(http);
}

// ---------------------------------------------------------------------------
// RELOJ
// ---------------------------------------------------------------------------
void BronquitoNetwork::ponerHora(long epoch, int offsetMin) {
  if (epoch < HORA_MINIMA) return;                 // la web no mandó una hora válida
  struct timeval tv = { (time_t)epoch, 0 };
  settimeofday(&tv, nullptr);
  offsetMinutos = offsetMin;
}

bool BronquitoNetwork::horaValida() const {
  return time(nullptr) >= HORA_MINIMA;
}

String BronquitoNetwork::fechaHoraLocal() const {
  if (!horaValida()) return "sin hora";
  time_t t = time(nullptr) + (time_t)offsetMinutos * 60;
  struct tm tmLocal;
  gmtime_r(&t, &tmLocal);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmLocal);
  return String(buf);
}

void BronquitoNetwork::olvidarRedes() {
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.remove("ip");
  WiFi.disconnect();
  Serial.println("[NET] Red y compu olvidadas");
}

// Valida el PIN contra la web. El PIN va en el cuerpo del pedido y no se imprime en el monitor serie.
RespuestaPin BronquitoNetwork::verificarPin(const String& usuario, const String& pin) {
  RespuestaPin out;
  JsonDocument cuerpo;
  cuerpo["usuario"] = usuario;
  cuerpo["pin"] = pin;
  String enviar;
  serializeJson(cuerpo, enviar);

  JsonDocument resp;
  out.http = llamarApi(servidorGuardado(), "POST", "/dispositivo/verificar-pin", enviar, resp);
  out.ok = (out.http == 200) && (resp["ok"] | false);
  out.error = String(resp["error"] | "");
  out.pinVersion = String(resp["pin_version"] | "");
  out.intentosRestantes = resp["intentos_restantes"] | -1;
  out.retryAfter = resp["retry_after"] | 0;
  return out;
}

// ---------------------------------------------------------------------------
// SOLICITUDES Y FRECUENCIA
// ---------------------------------------------------------------------------
int BronquitoNetwork::pedirSolicitudes(JsonDocument& resp) {
  return llamarApi(servidorGuardado(), "GET", "/dispositivo/solicitudes", "", resp);
}

bool BronquitoNetwork::confirmarSolicitud(int id, bool ok, const char* detalle) {
  JsonDocument cuerpo;
  cuerpo["ok"] = ok;
  if (detalle != nullptr && detalle[0] != '\0') cuerpo["detalle"] = detalle;
  String enviar;
  serializeJson(cuerpo, enviar);

  JsonDocument resp;
  int http = llamarApi(servidorGuardado(), "POST",
                       "/dispositivo/solicitudes/" + String(id) + "/confirmar", enviar, resp);
  return http == 200;
}

int BronquitoNetwork::pedirFrecuencia(const String& usuario, JsonDocument& resp) {
  return llamarApi(servidorGuardado(), "GET", "/dispositivo/frecuencia?usuario=" + usuario, "", resp);
}

void BronquitoNetwork::confirmarFrecuencia(JsonVariantConst idFrecuencia) {
  JsonDocument cuerpo;
  cuerpo["id_frecuencia"] = idFrecuencia;
  String enviar;
  serializeJson(cuerpo, enviar);
  JsonDocument confirmacion;
  llamarApi(servidorGuardado(), "POST", "/dispositivo/frecuencia/confirmar", enviar, confirmacion);
}

// ---------------------------------------------------------------------------
// PRUEBAS DE LA SD
// ---------------------------------------------------------------------------
// Una espirometría son ~25 KB: se le da más tiempo que al resto de los pedidos.
int BronquitoNetwork::subirEvaluacion(const String& cuerpo, JsonDocument& resp) {
  return llamarApi(servidorGuardado(), "POST", "/dispositivo/evaluaciones", cuerpo, resp, 10000);
}
