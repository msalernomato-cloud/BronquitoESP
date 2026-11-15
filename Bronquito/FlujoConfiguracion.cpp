#include "FlujoConfiguracion.h"
#include "NivelResistencia.h"

FlujoConfiguracion::FlujoConfiguracion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, Reloj &reloj)
  : ui(ui), tarjeta(tarjeta), net(net), reloj(reloj) {}

void FlujoConfiguracion::ejecutar() {
  bool actualizada = actualizarDesdeWeb();
  FrecuenciaGuardada f = tarjeta.leerFrecuencia();

  EstadoResistencia e = tarjeta.estadoResistencia(reloj.fechaHoraActual());
  InfoResistencia info;
  info.nivel = e.nivel;
  info.fijadoPorMedico = e.manual;
  info.sinVencimiento = e.manual;
  info.vigenciaDias = e.vigenciaDias;
  info.vence = e.vence;
  info.hayFechaHoy = e.hayFechaHoy;
  info.diasRestantes = e.diasRestantes;

  ui.mostrarConfiguracion(f.sesiones, f.dias, f.vigenteDesde, actualizada, info);
}

// GET /dispositivo/frecuencia trae tres cosas independientes; cada una se guarda (y se confirma) sola:
//   "frecuencia"       -> clave "frecuencia" de CONFIGURACION.JSON, confirma con su id
//   "resistencia_web"  -> clave "resistencia_web", confirma con su id
//   "predichos"        -> clave "predichos"; si cambiaron, se recalcula el nivel automático
bool FlujoConfiguracion::actualizarDesdeWeb() {
  if (!net.estaConectado() || net.servidorGuardado().length() == 0 || !tarjeta.hayPaciente()) return false;

  ui.mostrarMensaje("Actualizando configuración...");
  JsonDocument resp;
  int http = net.pedirFrecuencia(tarjeta.usuario(), resp);
  if (http != 200) return false;

  bool frecuenciaNueva = false;
  JsonObject f = resp["frecuencia"];
  if (!f.isNull()) {
    if (tarjeta.guardarFrecuencia(f)) {
      net.confirmarFrecuencia(f["id"]);
      frecuenciaNueva = true;
    } else {
      Serial.println("[CONFIG] No se pudo guardar la frecuencia en la SD");
    }
  }

  JsonObject r = resp["resistencia_web"];
  if (!r.isNull()) {
    if (tarjeta.guardarResistenciaWeb(r)) net.confirmarResistencia(r["id"]);
    else Serial.println("[CONFIG] No se pudo guardar la resistencia en la SD");
  }

  JsonVariant p = resp["predichos"];
  if (p.is<JsonObject>()) {
    Predichos antes = tarjeta.leerPredichos();
    auto igual = [](float a, JsonVariant v) {
      float b = v.isNull() ? NAN : v.as<float>();
      if (!(b > 0.0f)) b = NAN;
      return (isnan(a) && isnan(b)) || fabsf(a - b) < 0.005f;
    };
    bool cambiaron = !igual(antes.fvc_L, p["fvc_predicha_L"]) || !igual(antes.pim_cmH2O, p["pim_predicha_cmh2o"]);
    if (cambiaron) {
      if (tarjeta.guardarPredichos(p)) {
        Serial.println("[CONFIG] Predichos nuevos: se recalcula el nivel automático");
        tarjeta.actualizarResistenciaAuto();
      } else {
        Serial.println("[CONFIG] No se pudieron guardar los predichos en la SD");
      }
    }
  }
  return frecuenciaNueva;
}
