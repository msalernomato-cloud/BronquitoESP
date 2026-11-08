#include "FlujoSesion.h"

static const unsigned long INTERVALO_SD_MS = 2000;

FlujoSesion::FlujoSesion(BronquitoUI &ui, BronquitoStorage &storage, SystemState &estado)
  : ui(ui), storage(storage), estado(estado) {}

void FlujoSesion::manejarResultadoSincronizacion(StorageStatus status) {
  ultimaClaveSD = String((int)status) + "|" + storage.getDNI();
  ui.setDNI(storage.getDNI());
  ui.setSynced(storage.isSynced());
  ui.drawLoginScreen();

  switch (status) {
    case ERR_NO_CARD:
      Serial.println("[FSM] Error: No hay tarjeta MicroSD");
      ui.showErrorMessage("¡Error de tarjeta!", "No se detectó la microSD");
      delay(1800);
      ui.drawLoginScreen();
      break;

    case ERR_FILE_NOT_FOUND:
      Serial.println("[FSM] Error: Archivo perfil.json no encontrado");
      ui.showErrorMessage("¡Archivo faltante!", "No existe perfil.json");
      delay(1800);
      ui.drawLoginScreen();
      break;

    case ERR_JSON_PARSE:
      Serial.println("[FSM] Error: Formato JSON invalido");
      ui.showErrorMessage("¡Archivo dañado!", "Error en los datos del perfil");
      delay(1800);
      ui.drawLoginScreen();
      break;

    case STORAGE_OK:
    default:
      break;
  }
}

void FlujoSesion::refrescarPerfilEnUI() {
  StorageStatus st = storage.sync(false);
  ultimaClaveSD = String((int)st) + "|" + storage.getDNI();
  ui.setDNI(storage.getDNI());
  ui.setSynced(storage.isSynced());
}

void FlujoSesion::redibujarPantallaActual() {
  if (estado == STATE_MAIN_MENU) ui.drawMainMenu();
  else if (estado == STATE_LOGIN) ui.drawLoginScreen();
}

void FlujoSesion::revisarSD() {
  if (estado == STATE_LOGIN && (millis() - ultimoChequeoSD) > INTERVALO_SD_MS) {
    ultimoChequeoSD = millis();
    StorageStatus st = storage.sync(false);
    String clave = String((int)st) + "|" + storage.getDNI();
    if (clave != ultimaClaveSD) {
      ultimaClaveSD = clave;
      Serial.println("[FSM] La SD cambió -> actualizando login");
      ui.setDNI(storage.getDNI());
      ui.setSynced(storage.isSynced());
      ui.drawLoginScreen();
    }
  }
}

void FlujoSesion::formatearDispositivo(bool forzar) {
  ui.mostrarMensaje("Preparando para nuevo paciente...");
  FormatoResultado r = storage.formatearFabrica(forzar);

  switch (r) {
    case FORMATO_OK:
      ui.mostrarMensaje("Listo para un nuevo paciente", "Se borraron los datos", COLOR_VERDE_OK);
      break;
    case FORMATO_PENDIENTES:
      ui.mostrarMensaje("No se pudo preparar", "Hay pruebas sin subir", COLOR_ROJO_DEL);
      break;
    case FORMATO_SIN_SD:
      ui.mostrarMensaje("No se pudo preparar", "No hay tarjeta SD", COLOR_ROJO_DEL);
      break;
    default:
      ui.mostrarMensaje("Preparación incompleta", "Revisá la tarjeta SD", COLOR_ROJO_DEL);
      break;
  }
  delay(2000);

  if (r == FORMATO_OK || r == FORMATO_ERROR) {
    estado = STATE_LOGIN;
    ui.clearPin();
    manejarResultadoSincronizacion(storage.sync());
  } else {
    redibujarPantallaActual();
  }
}
