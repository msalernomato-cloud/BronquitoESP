#include <esp_system.h>
#include "SystemState.h"
#include "UI.h"
#include "Storage.h"
#include "BTPSSensor.h"
#include "BronquitoNet.h"
#include "RespiratorySensors.h"
#include "BronquitoRTC.h"
#include "Reloj.h"
#include "Evaluaciones.h"
#include "FlujoSesion.h"
#include "FlujoConexion.h"
#include "FlujoCalibracion.h"
#include "FlujoHistorial.h"

// ---------------------------------------------------------------------------
// HARDWARE
// ---------------------------------------------------------------------------
BronquitoUI ui;
BronquitoStorage storage(19, 22, 23, 21);
BronquitoNetwork net;
RespiratorySensors sensoresResp(36, 39);

const int DHT_PIN = 2;                 // DHT22 (temperatura y humedad para la corrección BTPS)
BTPSSensor btps(DHT_PIN, DHT22);

const int RTC_PIN_SDA = 17;
const int RTC_PIN_SCL = 18;
BronquitoRTC rtc;

// ---------------------------------------------------------------------------
// ESTADO Y FLUJOS
// ---------------------------------------------------------------------------
SystemState currentState = STATE_LOGIN;

Reloj            reloj(rtc, net);
Evaluaciones     evaluaciones(storage);
FlujoSesion      sesion(ui, storage, currentState);
FlujoConexion    conexion(ui, net, storage, evaluaciones, rtc, sesion, currentState);
FlujoCalibracion calibracion(ui, sensoresResp, evaluaciones, reloj, btps);
FlujoHistorial   historial(ui, evaluaciones);

// Comandos por el monitor serie: FORMATEAR, FORMATEAR!, HORA, OLVIDAR_RED
void leerComandosSerie() {
  if (!Serial.available()) return;
  String c = Serial.readStringUntil('\n');
  c.trim();
  c.toUpperCase();
  if (c == "FORMATEAR")        sesion.formatearDispositivo(false);
  else if (c == "FORMATEAR!")  sesion.formatearDispositivo(true);
  else if (c == "HORA") {
    String hora = reloj.fechaHoraActual();
    if (hora.length() == 0) hora = "sin hora válida";
    Serial.println("[RTC] Hora actual: " + hora);
    Serial.println(String("[RTC] Responde: ") + (rtc.disponible() ? "sí" : "no") +
                   " | Perdió energía: " + (rtc.perdioEnergia() ? "sí" : "no"));
    float temp;
    if (rtc.leerTemperatura(temp)) Serial.println("[RTC] Temp. del chip: " + String(temp, 2) + " C");
  }
  else if (c == "OLVIDAR_RED") conexion.olvidarRedes();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  // Si el equipo se reinició solo, esto dice por qué (ej. "PANIC" = el programa abortó, "BROWNOUT" = caída de tensión)
  const char *motivos[] = {"desconocido", "encendido", "externo", "software (ESP.restart)", "PANIC (abort/excepción)",
                           "watchdog de interrupciones", "watchdog de tareas", "otro watchdog", "deep sleep", "BROWNOUT", "SDIO"};
  int motivo = (int)esp_reset_reason();
  Serial.printf("[BOOT] Motivo del último reinicio: %d (%s)\n", motivo, motivo >= 0 && motivo <= 10 ? motivos[motivo] : "?");

  pinMode(15, OUTPUT); digitalWrite(15, HIGH);
  pinMode(19, OUTPUT); digitalWrite(19, HIGH);
  delay(100);

  Serial.println("[BOOT] 1 - net.begin()");
  net.begin();
  Serial.println("[BOOT] 2 - ui.begin()");
  ui.begin();
  Serial.println("[BOOT] 3 - sensoresResp.begin()");
  sensoresResp.begin();
  btps.begin();
  Serial.println("[BOOT] 4 - storage.sync()");

  StorageStatus result = storage.sync();
  if (result == ERR_NO_CARD) {
    ui.mostrarMensaje("Buscando la tarjeta microSD...");
    unsigned long t0 = millis();
    while (result == ERR_NO_CARD && millis() - t0 < 5000) {
      delay(500);
      result = storage.sync(false);
    }
  }
  sesion.manejarResultadoSincronizacion(result);
  Serial.println("[BOOT] 5 - login dibujado, iniciando RTC");

  if (!rtc.begin(RTC_PIN_SDA, RTC_PIN_SCL))   Serial.println("[RTC] No responde: revisá SDA/SCL y alimentación");
  else if (rtc.perdioEnergia())               Serial.println("[RTC] Detectado, perdió la hora");
  else                                        Serial.println("[RTC] Hora: " + rtc.fechaHora());

  Serial.println(">>> Dispositivo Bronquito iniciado correctamente. <<<");
}

void loop() {
  UIEvent event = ui.checkTouch(currentState);

  conexion.actualizarConexion();
  leerComandosSerie();
  conexion.atenderSyncInicial();
  conexion.actualizarIndicador();
  sesion.revisarSD();

  switch (event) {
    case EVENT_CONNECT_WEB:
      conexion.menuConectar();
      sesion.redibujarPantallaActual();
      break;

    case EVENT_SHOW_CONNECTION:
      conexion.mostrarEstado();
      sesion.redibujarPantallaActual();
      break;

    case EVENT_GOTO_PIN:
      if (storage.isSynced()) {
        Serial.println("[FSM] Perfil válido -> pantalla de PIN");
        ui.showLoginFeedback();
        delay(300);
        currentState = STATE_PIN_INPUT;
        ui.clearPin();
        ui.drawPinScreen();
      } else {
        Serial.println("[FSM] BLOQUEADO: no hay perfil cargado.");
        ui.showErrorMessage("¡Acceso bloqueado!", "Sincronizá con la web");
        delay(1800);
        ui.drawLoginScreen();
      }
      break;

    case EVENT_GOTO_LOGIN:
      Serial.println("[FSM] Volviendo a pantalla de Login");
      currentState = STATE_LOGIN;
      ui.drawLoginScreen();
      break;

    case EVENT_PIN_SUBMIT: {
      Serial.println("[FSM] Validando PIN");
      String pin = ui.getPinEntered();
      bool entra = false;
      bool usoOnline = storage.pinPendiente();

      if (usoOnline) {
        entra = (conexion.verificarPinOnline(pin) == 1);
      } else {
        entra = (pin == storage.getPin());
      }

      if (entra) {
        Serial.println("[FSM] PIN CORRECTO -> Menú Principal");
        ui.showPinStatus(true);
        delay(800);
        currentState = STATE_MAIN_MENU;
        ui.drawMainMenu();
      } else {
        Serial.println("[FSM] PIN INCORRECTO");
        if (!usoOnline) { ui.showPinStatus(false); delay(1000); }
        ui.clearPin();
        ui.drawPinScreen();
      }
      break;
    }

    case EVENT_SELECT_TRAINING:
      Serial.println("[FSM] Opción: ENTRENAMIENTO MUSCULAR");
      break;

    case EVENT_SELECT_CALIBRATION:
      Serial.println("[FSM] Entrando al Menú de Calibración");
      currentState = STATE_CALIB_MENU;
      ui.drawCalibMenu();
      break;

    case EVENT_SELECT_CONFIG:
      Serial.println("[FSM] Opción seleccionada: CONFIGURACIÓN");
      conexion.mostrarConfiguracion();
      sesion.redibujarPantallaActual();
      break;

    case EVENT_SELECT_HISTORY:
      Serial.println("[FSM] Opción seleccionada: HISTORIAL");
      // true = tocó "Enviar a la web": sincroniza y vuelve al historial
      while (historial.ejecutar()) {
        conexion.enviarDatos();
        if (currentState == STATE_LOGIN) break;   // al sincronizar aceptó un pedido de formatear: ya quedó en el login
      }
      sesion.redibujarPantallaActual();
      break;

    case EVENT_CALIB_START:
      Serial.println("[FSM] Calibración guiada: PIM + Espirometría");
      calibracion.ejecutar();
      conexion.ofrecerEnvio();   // "¿Enviar a la web?": si acepta sincroniza; si no, se suben en la próxima
      if (currentState == STATE_LOGIN) {   // al sincronizar aceptó un pedido de formatear: ya quedó en el login
        sesion.redibujarPantallaActual();
        break;
      }
      currentState = STATE_CALIB_MENU;
      ui.drawCalibMenu();
      break;

    case EVENT_CALIB_BACK_TO_MENU:
      if (currentState == STATE_CALIB_MENU) {
        Serial.println("[FSM] Volviendo al Menú Principal");
        currentState = STATE_MAIN_MENU;
        ui.drawMainMenu();
      } else {
        Serial.println("[FSM] Volviendo al Menú de Calibración");
        currentState = STATE_CALIB_MENU;
        ui.drawCalibMenu();
      }
      break;

    case EVENT_NONE:
    default:
      break;
  }
}
