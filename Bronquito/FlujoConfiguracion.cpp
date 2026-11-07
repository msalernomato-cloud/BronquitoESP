#include "FlujoConfiguracion.h"

FlujoConfiguracion::FlujoConfiguracion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net)
  : ui(ui), tarjeta(tarjeta), net(net) {}

void FlujoConfiguracion::ejecutar() {
  bool actualizada = actualizarFrecuencia();
  FrecuenciaGuardada f = tarjeta.leerFrecuencia();
  ui.mostrarFrecuencia(f.sesiones, f.dias, f.vigenteDesde, actualizada);
}

bool FlujoConfiguracion::actualizarFrecuencia() {
  if (!net.estaConectado() || net.servidorGuardado().length() == 0 || !tarjeta.hayPaciente()) return false;

  ui.mostrarMensaje("Actualizando frecuencia...");
  JsonDocument resp;
  int http = net.pedirFrecuencia(tarjeta.usuario(), resp);
  if (http != 200) return false;

  JsonObject f = resp["frecuencia"];
  if (f.isNull()) return false;
  if (!tarjeta.guardarFrecuencia(f)) {
    Serial.println("[FREC] No se pudo guardar en la SD");
    return false;
  }
  net.confirmarFrecuencia(f["id"]);
  return true;
}
