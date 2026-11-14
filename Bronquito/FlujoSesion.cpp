#include "FlujoSesion.h"

static const unsigned long INTERVALO_SD_MS = 2000;

FlujoSesion::FlujoSesion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, EstadoSistema &estado)
  : ui(ui), tarjeta(tarjeta), net(net), estado(estado) {}

// =============================================================================
// INICIO DE SESIÓN
// =============================================================================
void FlujoSesion::mostrarResultadoTarjeta(EstadoTarjeta resultado) {
  ultimaClaveSD = String((int)resultado) + "|" + tarjeta.usuario();
  ultimoEstadoSD = resultado;
  ui.setUsuario(tarjeta.textoLogin());
  ui.setSynced(tarjeta.hayPaciente());
  ui.drawLoginScreen();

  const char *titulo = nullptr, *detalle = nullptr;
  switch (resultado) {
    case TARJETA_NO_ESTA:       titulo = "¡Error de tarjeta!";  detalle = "No se detectó la microSD";      break;
    case TARJETA_SIN_PACIENTE:  titulo = "¡Archivo faltante!";  detalle = "No existe perfil.json";          break;
    case TARJETA_PERFIL_DANADO: titulo = "¡Archivo dañado!";    detalle = "Error en los datos del perfil"; break;
    case TARJETA_OK:            break;
  }
  if (titulo) {
    Serial.println(String("[SESION] ") + titulo + " " + detalle);
    ui.showErrorMessage(titulo, detalle);   // espera el OK
    ui.drawLoginScreen();
  }
}

void FlujoSesion::refrescarPerfilEnUI() {
  EstadoTarjeta st = tarjeta.leerPerfil(false);
  ultimaClaveSD = String((int)st) + "|" + tarjeta.usuario();
  ultimoEstadoSD = st;
  ui.setUsuario(tarjeta.textoLogin());
  ui.setSynced(tarjeta.hayPaciente());
}

void FlujoSesion::redibujarPantallaActual() {
  if (estado == ESTADO_MENU_PRINCIPAL) ui.drawMainMenu();
  else if (estado == ESTADO_INICIO_SESION) ui.drawLoginScreen();
}

void FlujoSesion::revisarSD() {
  if (estado != ESTADO_INICIO_SESION || millis() - ultimoChequeoSD <= INTERVALO_SD_MS) return;
  ultimoChequeoSD = millis();
  EstadoTarjeta st = tarjeta.leerPerfil(false);
  String clave = String((int)st) + "|" + tarjeta.usuario();
  if (clave != ultimaClaveSD) {          // cambió la tarjeta o el paciente
    bool sacada = (st == TARJETA_NO_ESTA && ultimoEstadoSD != TARJETA_NO_ESTA);
    bool puesta = (st != TARJETA_NO_ESTA && ultimoEstadoSD == TARJETA_NO_ESTA);
    ultimaClaveSD = clave;
    ultimoEstadoSD = st;
    Serial.println("[SESION] La SD cambió -> actualizando login");
    ui.setUsuario(tarjeta.textoLogin());
    ui.setSynced(tarjeta.hayPaciente());
    if (sacada) {
      Serial.println("[SESION] MicroSD desconectada");
      ui.mostrarError("MicroSD desconectada", "Volvé a colocar la tarjeta");
    } else if (puesta) {
      Serial.println("[SESION] MicroSD conectada");
      ui.mostrarMensaje("MicroSD conectada", "", COLOR_VERDE_OK);
      delay(1500);
    }
    ui.drawLoginScreen();
  }
}

// =============================================================================
// BOTONES
// =============================================================================
bool FlujoSesion::ingresar() {
  if (tarjeta.hayPaciente()) {
    ui.showLoginFeedback();
    delay(300);
    return true;
  }
  Serial.println("[SESION] Bloqueado: no hay perfil cargado");
  if (tarjeta.estado() == TARJETA_NO_ESTA)
    ui.showErrorMessage("¡Acceso bloqueado!", "Colocá la microSD");
  else
    ui.showErrorMessage("¡Acceso bloqueado!", "Sincronizá con la web");
  ui.drawLoginScreen();
  return false;
}

bool FlujoSesion::validarPin() {
  String pin = ui.getPinEntered();
  ui.clearPin();

  // Si la web cambió el PIN y el equipo todavía no lo bajó, se verifica en línea
  if (tarjeta.faltaPin()) {
    bool correcto = (verificarPinEnLinea(pin) == 1);
    Serial.println(correcto ? "[SESION] PIN correcto (en línea)" : "[SESION] PIN no verificado (en línea)");
    return correcto;
  }

  bool correcto = (pin == tarjeta.pin());
  Serial.println(correcto ? "[SESION] PIN correcto" : "[SESION] PIN incorrecto");
  ui.showPinStatus(correcto);
  delay(correcto ? 800 : 1000);
  return correcto;
}

// =============================================================================
// PACIENTE
// =============================================================================
String FlujoSesion::cargarPaciente(JsonObjectConst perfil) {
  String nombre = String(perfil["nombre"] | "") + " " + String(perfil["apellido"] | "");
  String usuario = String(perfil["usuario"] | "");
  if (!ui.pedirConfirmacion("Cargar paciente", nombre, "Usuario: " + usuario)) return "rechazado_por_usuario";

  if (!tarjeta.guardarPerfil(perfil)) {
    bool hayTarjeta = tarjeta.hayTarjeta();
    ui.mostrarError("No se pudo cargar", hayTarjeta ? "Error al escribir la SD" : "No se detectó la tarjeta SD");
    return hayTarjeta ? "error_sd" : "sin_tarjeta_sd";
  }
  refrescarPerfilEnUI();
  pedirPinNuevo("Hola " + String(perfil["nombre"] | ""), "Ingresá tu PIN para activarlo");
  return "";
}

String FlujoSesion::prepararNuevoPaciente() {
  if (!ui.pedirConfirmacion("Preparar para nuevo paciente", "Tu médico pide borrar", "los datos del paciente actual"))
    return "rechazado_por_usuario";
  switch (borrarDatos(false)) {
    case FORMATO_OK:             return "";
    case FORMATO_HAY_PENDIENTES: return "archivos_pendientes";
    case FORMATO_SIN_TARJETA:    return "sin_tarjeta_sd";
    default:                     return "error_formato";
  }
}

ResultadoFormato FlujoSesion::borrarDatos(bool forzar) {
  ui.mostrarMensaje("Preparando para nuevo paciente...");
  ResultadoFormato r = tarjeta.borrarTodo(forzar);

  switch (r) {
    case FORMATO_OK:             ui.mostrarMensaje("Listo para un nuevo paciente", "Se borraron los datos", COLOR_VERDE_OK);
                                 delay(2000);                                                              break;
    case FORMATO_HAY_PENDIENTES: ui.mostrarError("No se pudo preparar", "Hay pruebas sin subir");         break;
    case FORMATO_SIN_TARJETA:    ui.mostrarError("No se pudo preparar", "No hay tarjeta SD");             break;
    default:                     ui.mostrarError("Preparación incompleta", "Revisá la tarjeta SD");       break;
  }

  if (r == FORMATO_OK || r == FORMATO_ERROR) {   // ya no hay paciente: siempre vuelve al inicio de sesión
    estado = ESTADO_INICIO_SESION;
    ui.clearPin();
    refrescarPerfilEnUI();
    ui.drawLoginScreen();
  } else {
    redibujarPantallaActual();
  }
  return r;
}

bool FlujoSesion::pedirPinNuevo(const String &titulo, const String &subtitulo) {
  while (true) {
    String pin = ui.pedirPin(titulo, subtitulo);
    if (pin.length() == 0) return false;
    int v = verificarPinEnLinea(pin);
    if (v == 1) {
      ui.mostrarMensaje("¡PIN correcto!", "", COLOR_VERDE_OK);
      delay(1000);
      return true;
    }
    if (v == -1) return false;     // incorrecto (0): se vuelve a pedir
  }
}

int FlujoSesion::verificarPinEnLinea(const String &pin) {
  ui.mostrarMensaje("Verificando PIN...");
  RespuestaPin r = net.verificarPin(tarjeta.usuario(), pin);

  if (r.ok) {
    tarjeta.guardarPin(pin, r.pinVersion);   // queda en la tarjeta para la próxima vez
    return 1;
  }
  if (r.http == 401) {
    ui.mostrarError("PIN incorrecto", "");
    return 0;
  }
  String motivo = net.textoError(r.http);
  if (r.error == "sin_paciente_asignado")    motivo = "No hay un paciente asignado";
  else if (r.error == "usuario_no_asignado") motivo = "Este usuario no está asignado";
  ui.mostrarError("No se pudo verificar", motivo);
  return -1;
}
