#include "FlujoCalibracion.h"

static const uint32_t DURACION_PIM_MS = 10000;   // 10 s
static const uint32_t DURACION_ESP_MS = 10000;   // 10 s
static const bool GRAFICAR_CADA_INTENTO_PIM = false;  // true = muestra la presión en el tiempo al terminar cada intento
// false = la calibración hace solo la espirometría (para probarla). El sensor de presión se sigue
// usando: es el que dice si el aire sale o entra.
static const bool HACER_PIM = true;

FlujoCalibracion::FlujoCalibracion(BronquitoUI &ui, RespiratorySensors &sensoresResp, TarjetaSD &tarjeta,
                                   Reloj &reloj, BTPSSensor &btps)
  : ui(ui), sensoresResp(sensoresResp), tarjeta(tarjeta), reloj(reloj), btps(btps) {}

CondicionesAmbiente FlujoCalibracion::leerAmbiente() {
  CondicionesAmbiente a;
  if (btps.leer()) {
    a.valida = true;
    a.temperatura_c = btps.getTemperatura();
    a.humedad_rel_pct = btps.getHumedad();
    a.factor_btps = btps.getBTPSFactor();
    Serial.printf("[BTPS] %.1f C, %.0f %% HR -> factor BTPS = %.4f\n", a.temperatura_c, a.humedad_rel_pct, a.factor_btps);
  } else {
    Serial.println("[BTPS] No se pudo leer el DHT22: flujos y volúmenes SIN corregir (factor 1)");
  }
  return a;
}

void FlujoCalibracion::guardarFallida(const std::vector<MuestraEspiro> &esp, const EventoEspiratorio &ev,
                                      const CondicionesAmbiente &amb, const String &calibId, int intento) {
  ui.mostrarMensaje("Guardando en SD...", "Por favor aguarde");
  if (tarjeta.guardarEspirometriaFallida(calibId, intento, reloj.fechaHoraActual(), esp, ev, amb))
    Serial.printf("[SD] Maniobra fallida guardada (intento %d, E%d)\n", intento, ev.codigo);
  else
    Serial.println("[SD] No se pudo guardar la maniobra fallida");
}

ResultadoCaptura FlujoCalibracion::capturarManiobra(bool esPim, uint32_t duracionMs, std::vector<MuestraEspiro> &salida,
                                        EventoEspiratorio &ev, float factorBTPS) {
  ev = EventoEspiratorio();

  // 1) Pantalla primero y RECIÉN DESPUÉS la tara. Redibujar la pantalla después de la tara corría
  //    el cero ~12 mV (en flujo, la señal arrancaba en ~70 L/min en vez de 0; en PIM, ~2 cmH2O).
  const float totalSeg = duracionMs / 1000.0f;
  if (esPim) ui.drawCalibPimRunning(totalSeg);
  else       ui.drawCalibEspRunning(totalSeg);
  sensoresResp.realizarTara();
  if (esPim) ui.setHintPim("Inhale con fuerza (min. 1,5 s)");   // es la señal para empezar
  else       ui.setHintEsp("Exhale e inhale con fuerza");
  delay(50);                                                   // que la señal se asiente tras escribir
  Serial.printf("[CAL] %s | cero (tara): %.4f V\n", esPim ? "Captura PIM" : "Captura espirometría",
                sensoresResp.getVZeroFlujo());

  // 2) Captura con la pantalla FIJA: escribir la pantalla mueve el cero de los sensores y obliga a
  //    pausar el muestreo, así que durante la medición no se redibuja nada (solo se revisa "Cancelar").
  ResultadoCaptura r = sensoresResp.capturar(
      duracionMs, 1, nullptr,
      [&]() { return ui.cancelarTocado(); });

  if (r == CAPTURA_SIN_MEMORIA) {
    Serial.println("[CAL] No se pudo iniciar el muestreo (memoria: ver líneas [MEM])");
    ui.mostrarError("No se pudo medir", "Falta memoria (ver serie)");
    return r;
  }
  if (r == CAPTURA_CANCELADA) return r;
  sensoresResp.entregarMuestras(salida);

  // 3) Análisis (solo espirometría; la PIM se analiza en ejecutar(), intento por intento)
  if (!esPim) {
    ev = analizarEventoEspiratorio(salida, sensoresResp.getVZeroFlujo(), sensoresResp.getKEspiracion(),
                                   sensoresResp.getKInspiracion(), sensoresResp.periodoMuestreo(), factorBTPS);
    Serial.printf("[CAL] Cero corregido: %+.1f mV | ruido estimado: %.1f mV | umbral de inicio/fin: %.1f mV\n",
                  ev.offset_V * 1000.0f, ev.ruido_V * 1000.0f, ev.umbral_V * 1000.0f);
    if (ev.hayEvento) {
      Serial.printf("[CAL] Evento: integra %.2f-%.2f s | t0 (extrapolado)=%.2f s | FET=%.2f s\n",
                    ev.tInicio_s, ev.tFin_s, ev.t0_s, ev.duracion_s);
      Serial.printf("[CAL]   FVC=%.2f L, FEV1=%.2f L, PEF=%.0f L/min, BEV=%.3f L (factor BTPS %.4f)\n",
                    ev.fvc_L, ev.fev1_L, ev.pef_Lmin, ev.bev_L, ev.factorBTPS);
      if (ev.hayInspiracion)
        Serial.printf("[CAL] Inspiración: integra %.2f-%.2f s | t0=%.2f s | FIVC=%.2f L, FIV1=%.2f L, PIF=%.0f L/min\n",
                      ev.tInicioInsp_s, ev.tFinInsp_s, ev.t0Insp_s, ev.fivc_L, ev.fiv1_L, ev.pif_Lmin);
      else
        Serial.println("[CAL] Inspiración: no se detectó (o empezó más de 3 s después de la espiración)");
    }
    Serial.printf("[CAL] Resultado: E%d - %s\n", ev.codigo, textoCodigoEspiro(ev.codigo));
  }
  return CAPTURA_OK;
}

// El usuario tocó "Cancelar": se descarta TODA la calibración, también lo que ya se había guardado
// (la PIM y las espirometrías que no validaron). Así "No se guardó nada" es cierto.
void FlujoCalibracion::calibracionCancelada(const String &calibId) {
  tarjeta.borrarTemporalesPIM();
  if (calibId.length() > 0) tarjeta.borrarCalibracion(calibId);
  Serial.println("[CAL] Cancelada: no se guarda nada");
  ui.mostrarMensaje("Calibración cancelada", "No se guardó nada", COLOR_ROJO_DEL);
  delay(1500);
}

// No se pudo medir (falta de memoria): no es una cancelación, así que lo ya guardado
// (con sus códigos de error) se conserva.
void FlujoCalibracion::calibracionInterrumpida() {
  tarjeta.borrarTemporalesPIM();
  Serial.println("[CAL] Interrumpida por un error: se conserva lo ya guardado");
}

void FlujoCalibracion::ejecutar() {
  std::vector<MuestraEspiro> pim, esp;
  EventoEspiratorio evPim = {}, evEsp = {};
  ResumenPIM rp = {};
  // Un solo id para toda la calibración: <id>_PIM, <id>_ESP (la válida) y <id>_ESP_int<n>_E<código>
  // (los intentos de espirometría que no validaron). Se crea con el primer archivo que se guarda.
  String calibId = "";
  int espFallidas = 0;

  const String lPim[4] = {
    "1. Colocá la tapa.",
    "2. Exhalá todo el aire.",
    "3. Inhalá con máxima fuerza por la",
    "   boquilla durante al menos 1,5 s."
  };
  const String lDescanso[3] = {
    "Descansá unos segundos y",
    "respirá con normalidad.",
    "Seguimos con la espirometría."
  };
  const String lEsp[4] = {
    "1. Inhalá hondo y poné la boquilla.",
    "2. Exhalá con máxima fuerza.",
    "3. Sin sacarla, inhalá enseguida",
    "   con máxima fuerza hasta llenarte"
  };

  if (!sensoresResp.usaPIM()) {
    Serial.println("[CAL] Equipo sin PIM: se guarda en cero al final");
  } else if (!HACER_PIM) {
    Serial.println("[CAL] PIM salteada (HACER_PIM = false): solo espirometría");
  } else {
    // Sesión de PIM: hasta INTENTOS_MAX intentos; termina cuando hay INTENTOS_REPRO reproducibles.
    // Cada intento queda en un .csv temporal; al final se guarda el del intento informado.
    using namespace AnalisisPIM;
    tarjeta.borrarTemporalesPIM();

    bool listo = false;
    int repeticiones = 0;   // con un solo intento: cuántas veces el paciente eligió repetirlo
    while (rp.nIntentos < INTENTOS_MAX && !listo) {
      int k = rp.nIntentos;
      String titulo = INTENTOS_MAX > 1 ? "PIM: intento " + String(k + 1) + " de " + String(INTENTOS_MAX)
                    : repeticiones > 0 ? "PIM: repetición " + String(repeticiones)
                    : "Paso 1 de 2: PIM";
      bool primera = (k == 0 && repeticiones == 0);
      if (!ui.mostrarPasoCalibracion(titulo, primera ? "Instrucciones:" : "Descansá y repetí:", lPim, 4, "COMENZAR", COLOR_VERDE_OK)) {
        calibracionCancelada(calibId);
        return;
      }
      ResultadoCaptura rc = capturarManiobra(true, DURACION_PIM_MS, pim, evPim);
      if (rc == CAPTURA_CANCELADA) { calibracionCancelada(calibId); return; }
      if (rc != CAPTURA_OK) { calibracionInterrumpida(); return; }

      IntentoPIM it = analizarIntentoPIM(pim);   // corrige el cero de la presión en el buffer
      rp.intentos[k] = it;
      rp.nIntentos++;
      if (GRAFICAR_CADA_INTENTO_PIM && !pim.empty()) {
        std::vector<float> gt(pim.size()), gp(pim.size());
        for (size_t i = 0; i < pim.size(); i++) { gt[i] = pim[i].tiempo_s; gp[i] = pim[i].presion_cmH2O; }
        ui.mostrarGrafico("PIM intento " + String(k + 1) + ": " + String(it.pim_cmH2O, 1) + " cmH2O",
                          "Tiempo (s)", "Presión (cmH2O)", gt.data(), gp.data(), (int)gt.size());
      }
      if (!tarjeta.guardarCsvTemporalPIM(k, pim)) Serial.println("[SD] No se pudo guardar el intento de PIM");
      std::vector<MuestraEspiro>().swap(pim);    // libera la memoria (clear() no la devuelve)

      listo = buscarReproducibles(rp.intentos, rp.nIntentos, rp.idxRepro);
      int parecidos = mayorGrupoReproducible(rp.intentos, rp.nIntentos);
      Serial.printf("[PIM] Intento %d: E%d (%s) | PIM sostenida %.1f cmH2O | pico %.1f | esfuerzo %.2f s | cero corregido %+.1f | parecidos %d de %d\n",
                    k + 1, it.codigo, textoCodigoPIM(it.codigo), it.pim_cmH2O, it.pico_cmH2O, it.duracionEsfuerzo_s,
                    it.offset_cmH2O, parecidos, INTENTOS_REPRO);
      if (INTENTOS_MAX == 1) {
        // Un solo intento (sin exigir reproducibilidad): el paciente ve el resultado y decide si lo repite.
        // Si lo repite, el intento anterior se descarta (su .csv temporal se pisa con el nuevo).
        bool repetir;
        if (it.valido) {
          int r = ui.revisarManiobra(titulo, "PIM " + String(it.pim_cmH2O, 1) + " cmH2O",
                                     "Pico " + String(it.pico_cmH2O, 1) + " cmH2O | " + String(it.duracionEsfuerzo_s, 1) + " s");
          if (r < 0) { calibracionCancelada(calibId); return; }
          repetir = (r == 0);
        } else {
          repetir = ui.pedirConfirmacion("PIM inválida (E" + String(it.codigo) + ")", textoCodigoPIM(it.codigo),
                                         "Podés repetirla o seguir así", "Seguir", "Repetir",
                                         COLOR_TEXTO_GRIS, BRONQUITO_CYAN);
        }
        if (repetir) {
          Serial.println("[PIM] El paciente repite la maniobra (se descarta el intento anterior)");
          rp.nIntentos = 0;
          repeticiones++;
          listo = false;
          continue;
        }
        listo = true;
      } else {   // con varios intentos se muestra cómo va cada uno
        if (it.valido)
          ui.mostrarMensaje("Intento " + String(k + 1) + ": " + String(it.pim_cmH2O, 1) + " cmH2O",
                            "Parecidos: " + String(parecidos) + " de " + String(INTENTOS_REPRO), COLOR_VERDE_OK);
        else
          ui.mostrarMensaje("Intento " + String(k + 1) + " inválido (E" + String(it.codigo) + ")", textoCodigoPIM(it.codigo), COLOR_ROJO_DEL);
        delay(2500);
      }
    }

    cerrarSesionPIM(rp);
    std::vector<MuestraEspiro>().swap(pim);
    sensoresResp.liberarMemoria();
    if (rp.codigo == PIM_OK) {
      Serial.printf("[PIM] Resultado: %.1f cmH2O (intento %d)\n", rp.pim_cmH2O, rp.idxReportado + 1);
      String grupo = "";   // intentos reproducibles (la cantidad sale de INTENTOS_REPRO)
      for (int k = 0; k < INTENTOS_REPRO; k++) grupo += (k ? ", " : "") + String(rp.idxRepro[k] + 1);
      Serial.println("[PIM] Intentos reproducibles: " + grupo);
    } else {
      Serial.printf("[PIM] Resultado: E%d - %s\n", rp.codigo, textoCodigoPIM(rp.codigo));
    }

    // La PIM se guarda YA, sin esperar a la espirometría: si después se cancela, falla o se corta la luz,
    // la PIM no se pierde. La espirometría se guarda después con el mismo id de calibración.
    {
      ui.mostrarMensaje("Guardando PIM en SD...", "Por favor aguarde");
      CondicionesAmbiente ambPim = leerAmbiente();
      if (calibId.length() == 0) calibId = tarjeta.nuevoIdCalibracion(reloj.fechaHoraActual());
      bool pimGuardada = tarjeta.guardarPIM(calibId, reloj.fechaHoraActual(), rp, ambPim);
      Serial.println(pimGuardada ? "[SD] PIM guardada: " + calibId : String("[SD] No se pudo guardar la PIM"));
      if (pimGuardada)
        ui.mostrarMensaje("PIM guardada en la SD",
                          rp.codigo == PIM_OK ? "PIM: " + String(rp.pim_cmH2O, 1) + " cmH2O"
                                                  : "No reproducible (E" + String(rp.codigo) + ")",
                          rp.codigo == PIM_OK ? COLOR_VERDE_OK : COLOR_ROJO_DEL);
      else
        ui.showErrorMessage("Error en SD", "No se pudo guardar la PIM");   // espera el OK
      if (pimGuardada) delay(2000);
    }

    bool sigue = ui.mostrarPasoCalibracion("Descanso", "Muy bien. PIM lista.", lDescanso, 3, "CONTINUAR", BRONQUITO_CYAN);
    if (!sigue) { calibracionCancelada(calibId); return; }
  }

  CondicionesAmbiente amb;
  while (true) {
    if (!ui.mostrarPasoCalibracion(HACER_PIM ? "Paso 2 de 2: Espirometría" : "Espirometría", "Instrucciones:", lEsp, 4, "COMENZAR", COLOR_VERDE_OK)) {
      calibracionCancelada(calibId);
      return;
    }
    amb = leerAmbiente();
    ResultadoCaptura rc = capturarManiobra(false, DURACION_ESP_MS, esp, evEsp, amb.factor_btps);
    if (rc == CAPTURA_CANCELADA) { calibracionCancelada(calibId); return; }
    if (rc != CAPTURA_OK) { calibracionInterrumpida(); return; }

    if (!evEsp.valido) {
      // No pasó la validación: se guarda igual (json con el código + csv), se muestra el motivo
      // y el usuario elige si la repite o termina la calibración así (no está obligado a repetir).
      if (calibId.length() == 0) calibId = tarjeta.nuevoIdCalibracion(reloj.fechaHoraActual());
      guardarFallida(esp, evEsp, amb, calibId, ++espFallidas);
      std::vector<MuestraEspiro>().swap(esp);
      bool repetir = ui.pedirConfirmacion("Maniobra inválida (E" + String(evEsp.codigo) + ")",
                                          textoCodigoEspiro(evEsp.codigo), "Quedó guardada con su código",
                                          "Terminar", "Repetir", COLOR_TEXTO_GRIS, BRONQUITO_CYAN);
      if (repetir) continue;
      Serial.printf("[CAL] Terminada con la espirometría inválida (E%d)\n", evEsp.codigo);
      ui.mostrarMensaje("Calibración guardada", "Espirometría con error E" + String(evEsp.codigo), BRONQUITO_CYAN);
      delay(1800);
      return;
    }
    int r = ui.revisarManiobra(HACER_PIM ? "Paso 2 de 2: Espirometría" : "Espirometría", "FVC " + String(evEsp.fvc_L, 2) + " L",
                               "FEV1 " + String(evEsp.fev1_L, 2) + " L | PEF " + String(evEsp.pef_Lmin, 0) + " L/min");
    if (r == 1) break;
    if (r < 0) { calibracionCancelada(calibId); return; }
    std::vector<MuestraEspiro>().swap(esp);
  }

  ui.mostrarMensaje("Guardando en SD...", "Por favor aguarde");
  // Mismo id que la PIM y los intentos fallidos de esta calibración
  if (calibId.length() == 0) calibId = tarjeta.nuevoIdCalibracion(reloj.fechaHoraActual());
  String fecha = reloj.fechaHoraActual();

  bool ok = true;
  if (!sensoresResp.usaPIM())   // sin PIM: se guarda una PIM en cero
    ok = tarjeta.guardarPIMenCero(calibId, fecha, amb);
  if (ok) ok = tarjeta.guardarEspirometria(calibId, fecha, esp, evEsp, amb);
  if (!ok) {
    if (!sensoresResp.usaPIM()) tarjeta.borrarPIM(calibId);   // la PIM medida (ya guardada) no se borra
    tarjeta.borrarEspirometria(calibId);
    Serial.println("[SD] Error al guardar la calibración");
    ui.showErrorMessage("Error en SD", "No se pudo guardar la prueba");
    return;
  }

  ui.mostrarMensaje("¡Calibración guardada!", HACER_PIM ? "PIM y espirometría" : "Espirometría", COLOR_VERDE_OK);
  delay(1500);
}
