#include "FlujoConexion.h"

// Pruebas que se suben por sincronización como máximo (si hay más, siguen en la próxima)
static const int MAX_PRUEBAS_POR_SYNC = 100;

FlujoConexion::FlujoConexion(BronquitoUI &ui, BronquitoNetwork &net, TarjetaSD &tarjeta, Reloj &reloj,
                             FlujoSesion &sesion, const EstadoSistema &estado)
  : ui(ui), net(net), tarjeta(tarjeta), reloj(reloj), sesion(sesion), estado(estado) {}

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
  if (syncInicialPendiente && estado == ESTADO_INICIO_SESION) {
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
  terminarFlujoConexion();
}

void FlujoConexion::terminarFlujoConexion() {
  actualizarConexion();
  syncInicialPendiente = false;
  ultimoEstado = calcularEstado();
  ui.setEstadoConexion(ultimoEstado);
}

bool FlujoConexion::conectarYSincronizar() {
  bool ok = false;
  if (!net.estaConectado()) {
    ui.mostrarMensaje("Hay que enviar pruebas a la web", "Conectate a Wi-Fi", BRONQUITO_CYAN);
    delay(2500);
    flujoWifi();   // si se conecta, también pide la computadora
  }
  if (net.estaConectado()) {
    if (net.servidorGuardado().length() == 0) flujoCompu();
    if (net.servidorGuardado().length() > 0) {
      ok = sincronizar();
      // La compu guardada no respondió (¿es de otra red?): se pide el código y se reintenta una vez
      if (!ok && !compuOk && estado != ESTADO_INICIO_SESION && flujoCompu()) ok = sincronizar();
    }
  }
  terminarFlujoConexion();
  return ok;
}

void FlujoConexion::mostrarEstado() {
  ui.mostrarEstadoConexion(net.estaConectado() ? net.ssidActual() : "", net.servidorGuardado());
}

void FlujoConexion::ofrecerEnvio() {
  // Sin Wi-Fi o sin compu no se pregunta: las pruebas se suben en la próxima sincronización
  if (!net.estaConectado() || net.servidorGuardado().length() == 0 || !tarjeta.hayPaciente()) return;
  int pendientes = tarjeta.listarPendientes(MAX_PRUEBAS_POR_SYNC).size();
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
// IDENTIFICACIÓN
// ---------------------------------------------------------------------------
bool FlujoConexion::dispositivoVacio()   { return tarjeta.estado() == TARJETA_SIN_PACIENTE; }
String FlujoConexion::usuarioActual()    { return tarjeta.hayPaciente() ? tarjeta.usuario() : ""; }
int FlujoConexion::archivosPendientes()  { return tarjeta.hayPaciente() ? tarjeta.contarPendientes() : 0; }

bool FlujoConexion::identificarCompu(const String &ip, RespuestaIdentificar &r) {
  bool ok = net.identificar(ip, dispositivoVacio(), usuarioActual(), archivosPendientes(), r);
  if (ok) {
    if (reloj.ponerHoraDeLaWeb(r.epoch, r.offsetMin)) Serial.println("[RELOJ] RTC puesto en hora desde la web");
    else Serial.println("[RELOJ] No se pudo poner en hora el RTC (¿módulo conectado?)");
  }
  compuOk = ok;
  return ok;
}

// ---------------------------------------------------------------------------
// SOLICITUDES DE LA WEB
// ---------------------------------------------------------------------------
void FlujoConexion::atenderSolicitud(JsonObject s) {
  int id = s["id"] | 0;
  String tipo = String(s["tipo"] | "");
  Serial.println("[SYNC] Solicitud " + String(id) + ": " + tipo);

  // Lo que pide la web sobre el paciente lo resuelve FlujoSesion; acá solo se le responde a la web
  String motivo;
  if (tipo == "RESET")              motivo = sesion.prepararNuevoPaciente();
  else if (tipo == "CARGAR_PERFIL") motivo = sesion.cargarPaciente(s["payload"]["perfil"].as<JsonObjectConst>());
  else                              motivo = "tipo_no_soportado";
  net.confirmarSolicitud(id, motivo.length() == 0, motivo.c_str());
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
  if (!tarjeta.hayPaciente()) return 0;   // sin perfil no se sabe de qué paciente son
  std::vector<String> pendientes = tarjeta.listarPendientes(MAX_PRUEBAS_POR_SYNC);
  int subidas = 0;

  for (size_t i = 0; i < pendientes.size(); i++) {
    ui.mostrarMensaje("Enviando pruebas a la web...", String(i + 1) + " de " + String(pendientes.size()));
    String cuerpo;
    if (!tarjeta.armarEnvio(pendientes[i], tarjeta.usuario(), cuerpo)) {
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
      if (!tarjeta.marcarSincronizado(pendientes[i]))
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

bool FlujoConexion::sincronizar() {
  ui.mostrarMensaje("Sincronizando...");
  RespuestaIdentificar r;
  if (!identificarCompu(net.servidorGuardado(), r)) {
    ui.mostrarMensaje("No se pudo sincronizar", net.textoError(r.http), COLOR_ROJO_DEL);
    delay(2200);
    return false;
  }
  Serial.println("[SYNC] Hora local: " + reloj.fechaHoraActual());

  if (tarjeta.hayPaciente() && !tarjeta.faltaPin() && r.usuario.length() > 0 &&
      r.usuario == tarjeta.usuario() && r.pinVersion.length() > 0 && r.pinVersion != tarjeta.pinVersion()) {
    sesion.pedirPinNuevo("Ingresá tu PIN", "");
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
  return http == 200;
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
    bool guardadas[MAX_REDES];
    for (int i = 0; i < n; i++) {
      String sinUso;
      nombres[i] = redes[i].ssid;
      rssi[i] = redes[i].rssi;
      guardadas[i] = net.claveRecordada(nombres[i], sinUso);
    }

    int sel = ui.elegirRed(nombres, rssi, n, guardadas);
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

    // Si ya se conectó antes a esta red, usa la contraseña guardada y no la pide
    String clave;
    bool recordada = net.claveRecordada(ssid, clave);
    if (!recordada) {
      clave = ui.pedirTexto("Contraseña de " + ssid);
      if (clave == "") continue;
    }

    ui.mostrarMensaje("Conectando...", ssid);
    ResultadoWifi r = net.conectar(ssid, clave);

    if (r == RES_WIFI_FALLO && recordada) {
      // La contraseña guardada no anduvo (¿la cambiaron?): se olvida y se vuelve a la lista de redes.
      // Al elegir la red de nuevo se pide la contraseña, como la primera vez
      net.olvidarRed(ssid);
      ui.mostrarMensaje("No se pudo conectar a " + ssid.substring(0, 18), "Elegí la red e ingresá la contraseña",
                        COLOR_ROJO_DEL);
      delay(2500);
      continue;
    }

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
