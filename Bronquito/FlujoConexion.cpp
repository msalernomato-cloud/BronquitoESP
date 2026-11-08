#include "FlujoConexion.h"

// Pruebas que se suben por sincronización como máximo (si hay más, siguen en la próxima)
static const int MAX_PRUEBAS_POR_SYNC = 100;

FlujoConexion::FlujoConexion(BronquitoUI &ui, BronquitoNetwork &net, BronquitoStorage &storage,
                             Evaluaciones &evaluaciones, BronquitoRTC &rtc, FlujoSesion &sesion, SystemState &estado)
  : ui(ui), net(net), storage(storage), evaluaciones(evaluaciones), rtc(rtc), sesion(sesion), estado(estado) {}

// ---------------------------------------------------------------------------
// ESTADO DE LA CONEXIÓN
// ---------------------------------------------------------------------------
EstadoConexion FlujoConexion::calcularEstado() {
  if (!net.estaConectado()) return CON_NINGUNA;
  return compuOk ? CON_WIFI_Y_COMPU : CON_SOLO_WIFI;
}

void FlujoConexion::actualizarConexion() {
  bool wifiAhora = net.estaConectado();
  if (wifiAhora && !wifiAntes && net.servidorGuardado().length() > 0)
    syncInicialPendiente = true;
  if (!wifiAhora) compuOk = false;
  wifiAntes = wifiAhora;
}

void FlujoConexion::atenderSyncInicial() {
  if (syncInicialPendiente && estado == STATE_LOGIN) {
    syncInicialPendiente = false;
    sincronizar();
    sesion.redibujarPantallaActual();
  }
}

void FlujoConexion::actualizarIndicador() {
  EstadoConexion est = calcularEstado();
  if (est != ultimoEstado) {
    ultimoEstado = est;
    ui.setEstadoConexion(est);
    sesion.redibujarPantallaActual();
  }
}

void FlujoConexion::menuConectar() {
  if (!net.estaConectado()) {
    flujoWifi();
  } else {
    int op = ui.elegirConexion(net.ssidActual());
    if (op == 0) flujoWifi();
    else if (op == 1) flujoCompu();
    else if (op == 2) {
      if (net.servidorGuardado().length() == 0) flujoCompu();
      else sincronizar();
    }
  }
  actualizarConexion();
  syncInicialPendiente = false;
  ultimoEstado = calcularEstado();
  ui.setEstadoConexion(ultimoEstado);
}

void FlujoConexion::mostrarEstado() {
  ui.mostrarEstadoConexion(net.estaConectado() ? net.ssidActual() : "", net.servidorGuardado());
}

void FlujoConexion::mostrarConfiguracion() {
  bool actualizada = actualizarFrecuencia();
  FrecuenciaGuardada f = storage.leerFrecuencia();
  ui.mostrarFrecuencia(f.sesiones, f.dias, f.vigenteDesde, actualizada);
}

void FlujoConexion::ofrecerEnvio() {
  // Sin Wi-Fi o sin compu no se pregunta: las pruebas se suben en la próxima sincronización
  if (!net.estaConectado() || net.servidorGuardado().length() == 0 || !storage.isSynced()) return;
  int pendientes = evaluaciones.listarPendientes(MAX_PRUEBAS_POR_SYNC).size();
  if (pendientes == 0) return;

  bool una = (pendientes == 1);
  String texto = una ? String("Hay 1 prueba sin enviar") : "Hay " + String(pendientes) + " pruebas sin enviar";
  if (!ui.pedirConfirmacion("Enviar a la web", texto, una ? "¿Enviarla ahora?" : "¿Enviarlas ahora?")) {
    Serial.println("[SUBIDA] Se envían en la próxima sincronización");
    return;
  }
  sincronizar();
}

void FlujoConexion::enviarDatos() {
  if (!net.estaConectado() || net.servidorGuardado().length() == 0) {
    ui.mostrarMensaje("No se pudo enviar", net.textoError(SIN_RED), COLOR_ROJO_DEL);   // "Sin Wi-Fi" o "Falta conectar la computadora"
    delay(2200);
    return;
  }
  sincronizar();
}

void FlujoConexion::olvidarRedes() {
  net.olvidarRedes();
  compuOk = false;
}

// ---------------------------------------------------------------------------
// IDENTIFICACIÓN Y PIN
// ---------------------------------------------------------------------------
bool FlujoConexion::dispositivoVacio()   { return storage.getLastError() == ERR_FILE_NOT_FOUND; }
String FlujoConexion::usuarioActual()    { return storage.isSynced() ? storage.getDNI() : ""; }
int FlujoConexion::archivosPendientes()  { return storage.isSynced() ? storage.contarPendientes() : 0; }

bool FlujoConexion::identificarCompu(const String &ip, RespuestaIdentificar &r) {
  bool ok = net.identificar(ip, dispositivoVacio(), usuarioActual(), archivosPendientes(), r);
  if (ok) {
    net.ponerHora(r.epoch, r.offsetMin);
    if (rtc.ajustarDesdeTexto(net.fechaHoraLocal())) Serial.println("[RTC] Hora actualizada desde la compu");
    else Serial.println("[RTC] No se pudo actualizar la hora (¿módulo conectado?)");
  }
  compuOk = ok;
  return ok;
}

int FlujoConexion::verificarPinOnline(const String &pin) {
  ui.mostrarMensaje("Verificando PIN...");
  RespuestaPin r = net.verificarPin(storage.getDNI(), pin);

  if (r.ok) {
    storage.guardarPin(pin, r.pinVersion);
    return 1;
  }
  if (r.http == 401) {
    ui.mostrarMensaje("PIN incorrecto", "", COLOR_ROJO_DEL);
    delay(1500);
    return 0;
  }
  String motivo = net.textoError(r.http);
  if (r.error == "sin_paciente_asignado")    motivo = "No hay un paciente asignado";
  else if (r.error == "usuario_no_asignado") motivo = "Este usuario no está asignado";
  ui.mostrarMensaje("No se pudo verificar", motivo, COLOR_ROJO_DEL);
  delay(2200);
  return -1;
}

bool FlujoConexion::pedirYVerificarPin(const String &titulo, const String &subtitulo) {
  while (true) {
    String pin = ui.pedirPin(titulo, subtitulo);
    if (pin.length() == 0) return false;
    int v = verificarPinOnline(pin);
    if (v == 1) {
      ui.mostrarMensaje("¡PIN correcto!", "", COLOR_VERDE_OK);
      delay(1000);
      return true;
    }
    if (v == -1) return false;
  }
}

// ---------------------------------------------------------------------------
// SOLICITUDES DE LA WEB
// ---------------------------------------------------------------------------
void FlujoConexion::atenderSolicitud(JsonObject s) {
  int id = s["id"] | 0;
  String tipo = String(s["tipo"] | "");
  Serial.println("[SYNC] Solicitud " + String(id) + ": " + tipo);

  if (tipo == "RESET") {
    bool acepta = ui.pedirConfirmacion("Preparar para nuevo paciente", "Tu médico pide borrar", "los datos del paciente actual");
    if (!acepta) { net.confirmarSolicitud(id, false, "rechazado_por_usuario"); return; }

    FormatoResultado fr = storage.formatearFabrica(false);
    if (fr == FORMATO_OK) {
      net.confirmarSolicitud(id, true, "");
      estado = STATE_LOGIN;
      ui.clearPin();
      sesion.refrescarPerfilEnUI();
    } else if (fr == FORMATO_PENDIENTES) {
      net.confirmarSolicitud(id, false, "archivos_pendientes");
    } else if (fr == FORMATO_SIN_SD) {
      ui.mostrarMensaje("No se pudo preparar", "No se detectó la tarjeta SD", COLOR_ROJO_DEL);
      delay(2200);
      net.confirmarSolicitud(id, false, "sin_tarjeta_sd");
    } else {
      net.confirmarSolicitud(id, false, "error_formato");
    }
    return;
  }

  if (tipo == "CARGAR_PERFIL") {
    JsonObject perfil = s["payload"]["perfil"];
    String nombre = String(perfil["nombre"] | "") + " " + String(perfil["apellido"] | "");
    String usuario = String(perfil["usuario"] | "");

    bool acepta = ui.pedirConfirmacion("Cargar paciente", nombre, "Usuario: " + usuario);
    if (!acepta) { net.confirmarSolicitud(id, false, "rechazado_por_usuario"); return; }

    if (!storage.guardarPerfil(perfil)) {
      bool hayTarjeta = storage.tarjetaPresente();
      ui.mostrarMensaje("No se pudo cargar", hayTarjeta ? "Error al escribir la SD" : "No se detectó la tarjeta SD",
                        COLOR_ROJO_DEL);
      delay(2200);
      net.confirmarSolicitud(id, false, hayTarjeta ? "error_sd" : "sin_tarjeta_sd");
      return;
    }
    sesion.refrescarPerfilEnUI();

    pedirYVerificarPin("Hola " + String(perfil["nombre"] | ""), "Ingresá tu PIN para activarlo");
    net.confirmarSolicitud(id, true, "");
    return;
  }

  net.confirmarSolicitud(id, false, "tipo_no_soportado");
}

int FlujoConexion::procesarSolicitudes() {
  for (int vuelta = 0; vuelta < 10; vuelta++) {
    JsonDocument resp;
    int http = net.pedirSolicitudes(resp);
    if (http != 200) return http;

    JsonArray lista = resp["solicitudes"].as<JsonArray>();
    if (lista.size() == 0) return 200;

    for (JsonObject s : lista) atenderSolicitud(s);
  }
  return 200;
}

// Sube a la web las pruebas de la SD con "sincronizado": false y las marca como subidas.
// Devuelve cuántas subió. Si algo la frena lo muestra, y lo que falta queda para la próxima sincronización.
int FlujoConexion::subirPendientes() {
  if (!storage.isSynced()) return 0;   // sin perfil no se sabe de qué paciente son
  std::vector<String> pendientes = evaluaciones.listarPendientes(MAX_PRUEBAS_POR_SYNC);
  int subidas = 0;

  for (size_t i = 0; i < pendientes.size(); i++) {
    ui.mostrarMensaje("Enviando pruebas a la web...", String(i + 1) + " de " + String(pendientes.size()));
    String cuerpo;
    if (!evaluaciones.armarEnvio(pendientes[i], storage.getDNI(), cuerpo)) {
      Serial.println("[SUBIDA] No se pudo leer " + pendientes[i] + " (queda pendiente)");
      continue;
    }
    JsonDocument resp;
    int http = net.subirEvaluacion(cuerpo, resp);
    cuerpo = String();   // libera la memoria antes de armar la siguiente
    String error = String(resp["error"] | "");
    Serial.printf("[SUBIDA] %s -> HTTP %d %s%s\n", pendientes[i].c_str(), http,
                  String(resp["estado"] | "").c_str(), error.c_str());

    if (http == 200) {
      subidas++;
      // Si no se pudo marcar, se vuelve a mandar en la próxima: la web la reconoce y no la duplica
      if (!evaluaciones.marcarSincronizado(pendientes[i]))
        Serial.println("[SUBIDA] No se pudo marcar " + pendientes[i] + " como sincronizada");
      continue;
    }
    if (http == 400) continue;   // a esa prueba le pasa algo: queda pendiente y se sigue con las demás

    // Sin conexión, clave inválida o paciente no asignado: no tiene sentido seguir con las demás
    String motivo = (error == "usuario_no_asignado" || error == "sin_paciente_asignado")
                      ? String("El paciente no está asignado en la web") : net.textoError(http);
    ui.mostrarMensaje("No se enviaron las pruebas", motivo, COLOR_ROJO_DEL);
    delay(2200);
    break;
  }
  return subidas;
}

void FlujoConexion::sincronizar() {
  ui.mostrarMensaje("Sincronizando...");
  RespuestaIdentificar r;
  if (!identificarCompu(net.servidorGuardado(), r)) {
    ui.mostrarMensaje("No se pudo sincronizar", net.textoError(r.http), COLOR_ROJO_DEL);
    delay(2200);
    return;
  }
  Serial.println("[SYNC] Hora local: " + net.fechaHoraLocal());

  if (storage.isSynced() && !storage.pinPendiente() && r.usuario.length() > 0 &&
      r.usuario == storage.getDNI() && r.pinVersion.length() > 0 && r.pinVersion != storage.getPinVersion()) {
    pedirYVerificarPin("PIN actualizado", "Ingresá tu nuevo PIN");
  }

  // Las pruebas van antes que los pedidos del médico: un RESET se niega mientras haya pruebas sin subir
  int subidas = subirPendientes();

  int http = procesarSolicitudes();
  sesion.refrescarPerfilEnUI();
  // Así el panel del médico ve cuántas pruebas quedaron sin subir
  if (subidas > 0) identificarCompu(net.servidorGuardado(), r);

  String detalle = "";
  if (subidas == 1)     detalle = "Se envió 1 prueba";
  else if (subidas > 1) detalle = "Se enviaron " + String(subidas) + " pruebas";
  if (http == 200) ui.mostrarMensaje("¡Sincronizado!", detalle, COLOR_VERDE_OK);
  else             ui.mostrarMensaje("No se pudo sincronizar", net.textoError(http), COLOR_ROJO_DEL);
  delay(subidas > 0 ? 2000 : 1200);
}

// ---------------------------------------------------------------------------
// FRECUENCIA
// ---------------------------------------------------------------------------
bool FlujoConexion::actualizarFrecuencia() {
  if (!net.estaConectado() || net.servidorGuardado().length() == 0 || !storage.isSynced()) return false;

  ui.mostrarMensaje("Actualizando frecuencia...");
  JsonDocument resp;
  int http = net.pedirFrecuencia(storage.getDNI(), resp);
  if (http != 200) return false;

  JsonObject f = resp["frecuencia"];
  if (f.isNull()) return false;
  if (!storage.guardarFrecuencia(f)) {
    Serial.println("[FREC] No se pudo guardar en la SD");
    return false;
  }

  net.confirmarFrecuencia(f["id"]);
  return true;
}

// ---------------------------------------------------------------------------
// WI-FI Y COMPUTADORA
// ---------------------------------------------------------------------------
bool FlujoConexion::flujoCompu() {
  while (true) {
    String lineas[3] = {
      "Asegurate de que la computadora",
      "esté en la misma red e ingresá",
      "el código que aparece en pantalla"
    };
    String codigo = ui.pedirCodigo(lineas, 3, net.prefijoRed());
    if (codigo.length() == 0) return false;

    if (codigo.toInt() > 255) {
      ui.mostrarMensaje("Código inválido", "Tiene que ser de 0 a 255", COLOR_ROJO_DEL);
      delay(1800);
      continue;
    }
    String ip = net.prefijoRed() + String(codigo.toInt());

    ui.mostrarMensaje("Probando la computadora...", ip);
    RespuestaIdentificar r;
    if (identificarCompu(ip, r)) {
      net.guardarServidor(ip);
      ui.mostrarMensaje("¡Computadora conectada!", ip, COLOR_VERDE_OK);
      delay(1500);
      return true;
    }
    ui.mostrarMensaje("La computadora no responde", net.textoError(r.http), COLOR_ROJO_DEL);
    delay(2200);
  }
}

void FlujoConexion::flujoWifi() {
  while (true) {
    ui.mostrarMensaje("Buscando redes...");
    RedWifi redes[MAX_REDES];
    int n = net.escanear(redes, MAX_REDES);
    String nombres[MAX_REDES];
    int rssi[MAX_REDES];
    for (int i = 0; i < n; i++) { nombres[i] = redes[i].ssid; rssi[i] = redes[i].rssi; }

    int sel = ui.elegirRed(nombres, rssi, n);
    if (sel == -3) {
      net.reconectarGuardada();
      return;
    }
    if (sel == -1) continue;

    String ssid;
    if (sel == -2) {
      ssid = ui.pedirTexto("Nombre de la red");
      if (ssid == "") continue;
    } else {
      ssid = nombres[sel];
    }

    String clave = ui.pedirTexto("Contraseña de " + ssid);
    if (clave == "") continue;

    ui.mostrarMensaje("Conectando...", ssid);
    ResultadoWifi r = net.conectar(ssid, clave);

    if (r == RES_WIFI_OK) {
      ui.mostrarMensaje("¡Wi-Fi conectado!", ssid, COLOR_VERDE_OK);
      delay(1000);
      wifiAntes = true;
      compuOk = false;
      flujoCompu();
      return;
    }
    if (r == RES_WIFI_INCOMPATIBLE) {
      ui.mostrarMensaje("Red no compatible", "Usá el Wi-Fi de tu casa o un hotspot", COLOR_ROJO_DEL);
    } else {
      ui.mostrarMensaje("No se pudo conectar", "Revisá la contraseña", COLOR_ROJO_DEL);
    }
    delay(1800);
  }
}
