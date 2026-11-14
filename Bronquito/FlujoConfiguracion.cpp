#include "FlujoConfiguracion.h"

FlujoConfiguracion::FlujoConfiguracion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, Reloj &reloj)
  : ui(ui), tarjeta(tarjeta), net(net), reloj(reloj) {}

// Días desde el 1/1/1970 para una fecha "AAAA-MM-DD" (algoritmo de días civiles). -1 si no tiene ese formato
static long diaDelCalendario(const String &f) {
  if (f.length() < 10 || f[4] != '-' || f[7] != '-') return -1;
  int a = f.substring(0, 4).toInt(), m = f.substring(5, 7).toInt(), d = f.substring(8, 10).toInt();
  if (a < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  a -= (m <= 2);
  long era = a / 400;
  long anioEra = a - era * 400;
  long diaAnio = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long diaEra = anioEra * 365 + anioEra / 4 - anioEra / 100 + diaAnio;
  return era * 146097 + diaEra - 719468;
}

void FlujoConfiguracion::ejecutar() {
  bool actualizada = actualizarFrecuencia();
  FrecuenciaGuardada f = tarjeta.leerFrecuencia();

  ResistenciaGuardada r = tarjeta.leerResistencia();
  InfoResistencia info;
  info.nivel = r.nivel;
  info.vigenciaDias = r.vigenciaDias;
  info.vence = r.vence;
  info.fijadoPorMedico = r.fijadoPorMedico;
  long hoy = diaDelCalendario(reloj.fechaHoraActual());   // "AAAA-MM-DD HH:MM:SS" o "sin hora"
  long vence = diaDelCalendario(r.vence);
  info.hayFechaHoy = (hoy >= 0 && vence >= 0);
  if (info.hayFechaHoy) info.diasRestantes = (int)(vence - hoy);

  ui.mostrarConfiguracion(f.sesiones, f.dias, f.vigenteDesde, actualizada, info);
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
