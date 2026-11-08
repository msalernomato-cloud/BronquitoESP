#include <esp_system.h>
#include "Estados.h"
#include "UI.h"
#include "TarjetaSD.h"
#include "BTPSSensor.h"
#include "BronquitoNet.h"
#include "RespiratorySensors.h"
#include "Reloj.h"
#include "FlujoSesion.h"
#include "FlujoConfiguracion.h"
#include "FlujoConexion.h"
#include "FlujoCalibracion.h"
#include "FlujoHistorial.h"
#include "EspacioSD.h"

// ---------------------------------------------------------------------------
// HARDWARE
// ---------------------------------------------------------------------------
BronquitoUI ui;
TarjetaSD tarjeta(19, 22, 23, 21);     // microSD: CS, SCK, MISO, MOSI
BronquitoNetwork net;
RespiratorySensors sensoresResp(36, 39);

const int DHT_PIN = 2;                 // DHT22 (temperatura y humedad para la corrección BTPS)
BTPSSensor btps(DHT_PIN, DHT22);

const int RTC_PIN_SDA = 17;
const int RTC_PIN_SCL = 18;
Reloj reloj;                           // RTC DS3231 + hora que manda la web

// ---------------------------------------------------------------------------
// ESTADO Y FLUJOS
// ---------------------------------------------------------------------------
EstadoSistema estadoActual = ESTADO_INICIO_SESION;

FlujoSesion      sesion(ui, tarjeta, net, estadoActual);
FlujoConexion    conexion(ui, net, tarjeta, reloj, sesion, estadoActual);
FlujoCalibracion calibracion(ui, sensoresResp, tarjeta, reloj, btps);
FlujoHistorial   historial(ui, tarjeta);
FlujoConfiguracion configuracion(ui, tarjeta, net);
EspacioSD        espacioSD(ui, tarjeta, conexion, estadoActual);

void leerComandosSerie() {
  if (!Serial.available()) return;
  String c = Serial.readStringUntil('\n');
  c.trim();
  c.toUpperCase();
  if (c == "FORMATEAR")        sesion.borrarDatos(false);
  else if (c == "FORMATEAR!")  sesion.borrarDatos(true);
  else if (c == "HORA")        reloj.imprimirEstado();
  else if (c == "I2C")         reloj.escanearBus();
  else if (c == "OLVIDAR_RED") conexion.olvidarRedes();
  else if (c == "SD")          espacioSD.informarPorSerie();
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
  Serial.println("[BOOT] 4 - tarjeta.leerPerfil()");

  EstadoTarjeta result = tarjeta.leerPerfil();
  if (result == TARJETA_NO_ESTA) {
    ui.mostrarMensaje("Buscando la tarjeta microSD...");
    unsigned long t0 = millis();
    while (result == TARJETA_NO_ESTA && millis() - t0 < 5000) {
      delay(500);
      result = tarjeta.leerPerfil(false);
    }
  }
  sesion.mostrarResultadoTarjeta(result);
  Serial.println("[BOOT] 5 - login dibujado, iniciando RTC");

  if (!reloj.iniciar(RTC_PIN_SDA, RTC_PIN_SCL)) Serial.println("[RELOJ] El RTC no responde: revisá SDA/SCL y alimentación");
  else if (reloj.rtcPerdioLaHora())            Serial.println("[RELOJ] RTC detectado, pero perdió la hora");
  else                                         Serial.println("[RELOJ] Hora: " + reloj.fechaHoraActual());

  Serial.println(">>> Dispositivo Bronquito iniciado correctamente. <<<");
}

void loop() {
  EventoPantalla evento = ui.detectarEvento(estadoActual);

  conexion.actualizarConexion();
  leerComandosSerie();
  conexion.atenderSyncInicial();
  conexion.actualizarIndicador();
  sesion.revisarSD();

  switch (evento) {
    case EVENTO_CONECTAR:
      conexion.menuConectar();
      sesion.redibujarPantallaActual();
      break;

    case EVENTO_VER_CONEXION:
      conexion.mostrarEstado();
      sesion.redibujarPantallaActual();
      break;

    case EVENTO_INGRESAR:          // solo con un paciente cargado en la tarjeta
      if (sesion.ingresar()) {
        Serial.println("[FSM] -> PIN");
        estadoActual = ESTADO_PIN;
        ui.clearPin();
        ui.drawPinScreen();
      }
      break;

    case EVENTO_IR_AL_INICIO:
      Serial.println("[FSM] Volviendo a pantalla de Login");
      estadoActual = ESTADO_INICIO_SESION;
      ui.drawLoginScreen();
      break;

    case EVENTO_PIN_INGRESADO:     // PIN correcto -> menú; incorrecto -> se queda en el PIN
      if (sesion.validarPin()) {
        Serial.println("[FSM] -> Menú principal");
        estadoActual = ESTADO_MENU_PRINCIPAL;
        ui.drawMainMenu();
      } else {
        ui.drawPinScreen();
      }
      break;

    case EVENTO_ENTRENAMIENTO:
      Serial.println("[FSM] Opción: ENTRENAMIENTO MUSCULAR");
      break;

    case EVENTO_CALIBRACION:
      Serial.println("[FSM] Entrando al Menú de Calibración");
      estadoActual = ESTADO_MENU_CALIBRACION;
      ui.drawCalibMenu();
      break;

    case EVENTO_CONFIGURACION:
      Serial.println("[FSM] Opción seleccionada: CONFIGURACIÓN");
      configuracion.ejecutar();
      sesion.redibujarPantallaActual();
      break;

    case EVENTO_HISTORIAL:
      Serial.println("[FSM] Opción seleccionada: HISTORIAL");
      // true = tocó "Enviar a la web": sincroniza y vuelve al historial
      while (historial.ejecutar()) {
        conexion.enviarDatos();
        if (estadoActual == ESTADO_INICIO_SESION) break;   // al sincronizar aceptó un pedido de formatear: ya quedó en el login
      }
      sesion.redibujarPantallaActual();
      break;

    case EVENTO_CALIBRAR:
      Serial.println("[FSM] Calibración guiada: PIM + Espirometría");
      // Si queda poco espacio en la microSD, borra pruebas viejas (ya enviadas a la web) antes de medir
      if (!espacioSD.asegurarEspacio()) {
        if (estadoActual == ESTADO_INICIO_SESION) {   // al sincronizar aceptó un pedido de formatear: ya quedó en el login
          sesion.redibujarPantallaActual();
          break;
        }
        estadoActual = ESTADO_MENU_CALIBRACION;
        ui.drawCalibMenu();
        break;
      }
      calibracion.ejecutar();
      conexion.ofrecerEnvio();   // "¿Enviar a la web?": si acepta sincroniza; si no, se suben en la próxima
      if (estadoActual == ESTADO_INICIO_SESION) {   // al sincronizar aceptó un pedido de formatear: ya quedó en el login
        sesion.redibujarPantallaActual();
        break;
      }
      estadoActual = ESTADO_MENU_CALIBRACION;
      ui.drawCalibMenu();
      break;

    case EVENTO_VOLVER:   // "Volver" del menú de calibración
      Serial.println("[FSM] Volviendo al Menú Principal");
      estadoActual = ESTADO_MENU_PRINCIPAL;
      ui.drawMainMenu();
      break;

    case EVENTO_NINGUNO:
    default:
      break;
  }
}
