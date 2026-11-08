#include "FlujoCalibracion.h"

static const uint32_t DURACION_PIM_MS = 10000;   // 10 s
static const uint32_t DURACION_ESP_MS = 15000;   // 15 s
static const bool GRAFICAR_CADA_INTENTO_PIM = false;  // true = muestra la presión en el tiempo al terminar cada intento

FlujoCalibracion::FlujoCalibracion(BronquitoUI &ui, RespiratorySensors &sensoresResp, Evaluaciones &evaluaciones,
                                   Reloj &reloj, BTPSSensor &btps)
  : ui(ui), sensoresResp(sensoresResp), evaluaciones(evaluaciones), reloj(reloj), btps(btps) {}

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
  String fecha = reloj.fechaHoraActual();
  // Ej.: eval_SINHORA_24_ESP_int2_E8 -> calibración 24, segundo intento de espirometría, falló por E8
  String id = calibId + "_ESP_int" + String(intento) + "_E" + String(ev.codigo);
  if (evaluaciones.guardarManiobra(id, calibId, "ESPIROMETRIA", fecha, esp, false, &ev, &amb))
    Serial.println("[SD] Maniobra fallida guardada: " + id);
  else
    Serial.println("[SD] No se pudo guardar la maniobra fallida");
}

bool FlujoCalibracion::capturarManiobra(bool esPim, uint32_t duracionMs, std::vector<MuestraEspiro> &salida,
                                        EventoEspiratorio &ev, float factorBTPS) {
  ev.valido = false;

  // Primero se dibuja la pantalla y RECIÉN DESPUÉS se hace la tara. Redibujar la pantalla después de la
  // tara corría el cero ~12 mV (en flujo, la señal arrancaba en ~70 L/min en vez de 0; en PIM, ~2 cmH2O).
  sensoresResp.configurarModoPresion(esPim);   // con PIM en el sensor de flujo, el MPX2010 mide presión
  if (esPim) {
    ui.drawCalibPimRunning();
    sensoresResp.realizarTara();
    ui.setHintPim("Inspire con fuerza y sostenga");   // es la señal para empezar
    delay(50);
  } else {
    ui.drawCalibEspRunning(duracionMs / 1000.0f);
    sensoresResp.realizarTara();
    ui.setHintEsp("Exhale con fuerza (min. 6s)");   // es la señal para empezar
    delay(50);                                       // dejar que la señal se asiente tras escribir en la pantalla
  }
  Serial.printf("[CAL] Cero del flujo (tara): %.4f V | K_FLUJO = %.2f\n",
                sensoresResp.getVZeroFlujo(), sensoresResp.getKFlujo());

  Serial.println(esPim ? "[CAL] Captura PIM" : "[CAL] Captura Espirometría");
  Serial.println("t_s,v_flujo_V,v_pim_V,Q_Lmin,P_cmH2O,V_L,fase");

  // Muestreo por interrupción: el timer toma las muestras solo (cada 10 ms). Este bucle solo imprime por
  // serie, actualiza la pantalla y mira el botón Cancelar.
  if (!sensoresResp.iniciarMuestreo(duracionMs / 10 + 20)) {
    Serial.println("[CAL] No se pudo iniciar el muestreo (timer o memoria: ver líneas [MEM] / [SENS])");
    ui.mostrarMensaje("No se pudo medir", "Falta memoria o timer (ver serie)", COLOR_ROJO_DEL);
    delay(2000);
    return false;
  }

  uint32_t tInicio = millis();
  uint32_t ultimoToque = tInicio;
  size_t impresas = 0;
  size_t ultimoRefresco = 0;
  const size_t cadaRefresco = esPim ? 20 : 100;   // pantalla: PIM 5 veces por segundo, espirometría 1 vez por segundo
  bool cancelado = false;

  while (!cancelado && (millis() - tInicio < duracionMs) && !sensoresResp.muestreoLleno()) {
    size_t n = sensoresResp.muestrasTomadas();
    while (impresas < n) Serial.println(sensoresResp.obtenerLineaCSV(sensoresResp.muestra(impresas++)));

    if (n > 0 && n / cadaRefresco > ultimoRefresco) {
      ultimoRefresco = n / cadaRefresco;
      MuestraEspiro m = sensoresResp.muestra(n - 1);
      // Escribir en la pantalla mete un pico en la lectura analógica: mientras se escribe y en las 3 muestras
      // siguientes no se lee el ADC (se repite la última muestra) y el filtro nunca ve el pico.
      sensoresResp.congelarMuestreo(true);
      if (esPim) ui.updateCalibPimProgress(m.presion_cmH2O, m.tiempo_s);
      else       ui.updateCalibEspProgress(m.tiempo_s, duracionMs / 1000.0f);
      sensoresResp.congelarMuestreo(false);
      sensoresResp.saltarMuestras(3);
    }

    if (millis() - ultimoToque >= 100) {
      ultimoToque = millis();
      if (ui.cancelarTocado()) cancelado = true;
    }
    delay(1);
  }
  sensoresResp.detenerMuestreo();
  while (impresas < sensoresResp.bufferMuestras.size())
    Serial.println(sensoresResp.obtenerLineaCSV(sensoresResp.bufferMuestras[impresas++]));

  if (cancelado) {
    sensoresResp.bufferMuestras.clear();
    return false;
  }

  // El volumen se calcula DESPUÉS de medir: se detecta el soplido, se retrocede hasta t0 y se integra solo ahí
  if (!esPim) {
    int conFlujo = 0, nReposo = 0;
    float sumaReposo = 0;
    for (size_t i = 0; i < sensoresResp.bufferMuestras.size(); i++) {
      if (sensoresResp.bufferMuestras[i].flujo_Lmin != 0.0f) conFlujo++;
      if (sensoresResp.bufferMuestras[i].tiempo_s < 0.5f) { sumaReposo += fabsf(sensoresResp.bufferMuestras[i].flujo_Lmin); nReposo++; }
    }
    Serial.printf("[CAL] Línea de base: %d%% de las muestras con flujo | flujo medio en los primeros 0.5 s = %.1f L/min (debería ser ~0)\n",
                  (int)(100.0f * conFlujo / (sensoresResp.bufferMuestras.size() ? sensoresResp.bufferMuestras.size() : 1)),
                  nReposo ? sumaReposo / nReposo : 0.0f);
    ev = sensoresResp.calcularVolumenPostEvento(factorBTPS);
    Serial.printf("[CAL] Cero corregido: %+.1f mV | ruido estimado: %.1f mV | umbral de inicio/fin: %.1f mV\n",
                  ev.offset_V * 1000.0f, ev.ruido_V * 1000.0f, ev.umbral_V * 1000.0f);
    if (ev.hayEvento) {
      Serial.printf("[CAL] Evento: integra %.2f-%.2f s | t0 (extrapolado)=%.2f s | FET=%.2f s | vol. último segundo=%.3f L\n",
                    ev.tInicio_s, ev.tFin_s, ev.t0_s, ev.duracion_s, ev.volUltimoSeg_L);
      Serial.printf("[CAL]   FVC=%.2f L, FEV1=%.2f L, PEF=%.0f L/min, BEV=%.3f L (factor BTPS %.4f)\n",
                    ev.fvc_L, ev.fev1_L, ev.pef_Lmin, ev.bev_L, ev.factorBTPS);
    }
    Serial.printf("[CAL] Resultado: E%d - %s\n", ev.codigo, textoCodigoEspiro(ev.codigo));
  }

  salida.swap(sensoresResp.bufferMuestras);
  sensoresResp.bufferMuestras.clear();
  return salida.size() > 0;
}

void FlujoCalibracion::calibracionCancelada() {
  evaluaciones.borrarTemporalesPIM();
  sensoresResp.configurarModoPresion(false);
  Serial.println("[CAL] Cancelada: no se guarda nada");
  ui.mostrarMensaje("Calibración cancelada", "No se guardó nada", COLOR_ROJO_DEL);
  delay(1500);
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
    "1. Boquilla puesta, salida tapada.",
    "2. Exhalá todo el aire.",
    "3. Inhalá con la máxima fuerza",
    "   sostenida durante 3 segundos."
  };
  // Con un solo sensor (PIM en el MPX2010 del venturi) hay que cambiar las mangueras entre una prueba y la otra
  const String lConexPim[4] = {
    "1. Desconectá las 2 mangueras.",
    "2. Entrada del venturi -> P2.",
    "3. Dejá el puerto P1 libre.",
    "4. Tapá la salida del venturi."
  };
  const String lConexEsp[4] = {
    "1. Destapá la salida del venturi.",
    "2. Volvé a conectar el sensor",
    "   al venturi como para flujo",
    "   (entrada y garganta)."
  };
  const String lDescanso[3] = {
    "Descansá unos segundos y",
    "respirá con normalidad.",
    "Seguimos con la espirometría."
  };
  const String lEsp[4] = {
    "1. Inhalá al máximo fuera.",
    "2. Colocá la boquilla.",
    "3. Exhalá con máxima fuerza (6s).",
    "4. Retirá la boquilla para inhalar."
  };

  if (!sensoresResp.usaPIM()) {
    MuestraEspiro cero = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
    pim.push_back(cero);
    cero.tiempo_s = 1.0f;
    pim.push_back(cero);
    Serial.println("[CAL] PIM salteada: se guarda en cero");
  } else {
    // Sesión de PIM: hasta INTENTOS_MAX intentos; termina cuando hay INTENTOS_REPRO reproducibles.
    // Cada intento queda en un .csv temporal; al final se guarda el del intento informado.
    using namespace AnalisisPIM;
    if (sensoresResp.pimEnSensorFlujo() &&
        !ui.mostrarPasoCalibracion("Paso 1 de 2: PIM", "Conectá el sensor para PIM:", lConexPim, 4, "LISTO", BRONQUITO_CYAN))
      return;
    evaluaciones.borrarTemporalesPIM();

    bool listo = false;
    while (rp.nIntentos < INTENTOS_MAX && !listo) {
      int k = rp.nIntentos;
      String titulo = "PIM: intento " + String(k + 1) + " de " + String(INTENTOS_MAX);
      if (!ui.mostrarPasoCalibracion(titulo, k == 0 ? "Instrucciones:" : "Descansá y repetí:", lPim, 4, "COMENZAR", COLOR_VERDE_OK)) {
        calibracionCancelada();
        return;
      }
      if (!capturarManiobra(true, DURACION_PIM_MS, pim, evPim)) { calibracionCancelada(); return; }

      IntentoPIM it = analizarIntentoPIM(pim);   // corrige el cero de la presión en el buffer
      rp.intentos[k] = it;
      rp.nIntentos++;
      if (GRAFICAR_CADA_INTENTO_PIM && !pim.empty()) {
        std::vector<float> gt(pim.size()), gp(pim.size());
        for (size_t i = 0; i < pim.size(); i++) { gt[i] = pim[i].tiempo_s; gp[i] = pim[i].presion_cmH2O; }
        ui.mostrarGrafico("PIM intento " + String(k + 1) + ": " + String(it.pim_cmH2O, 1) + " cmH2O",
                          "Tiempo (s)", "Presión (cmH2O)", gt.data(), gp.data(), (int)gt.size());
      }
      if (!evaluaciones.guardarCsvTemporalPIM(k, pim)) Serial.println("[SD] No se pudo guardar el intento de PIM");
      std::vector<MuestraEspiro>().swap(pim);    // libera la memoria (clear() no la devuelve)

      listo = buscarReproducibles(rp.intentos, rp.nIntentos, rp.idxRepro);
      int parecidos = mayorGrupoReproducible(rp.intentos, rp.nIntentos);
      Serial.printf("[PIM] Intento %d: E%d (%s) | PIM sostenida %.1f cmH2O | pico %.1f | esfuerzo %.2f s | cero corregido %+.1f | parecidos %d de %d\n",
                    k + 1, it.codigo, textoCodigoPIM(it.codigo), it.pim_cmH2O, it.pico_cmH2O, it.duracionEsfuerzo_s,
                    it.offset_cmH2O, parecidos, INTENTOS_REPRO);
      if (INTENTOS_MAX > 1) {   // con varios intentos se muestra cómo va cada uno
        if (it.valido)
          ui.mostrarMensaje("Intento " + String(k + 1) + ": " + String(it.pim_cmH2O, 1) + " cmH2O",
                            "Parecidos: " + String(parecidos) + " de " + String(INTENTOS_REPRO), COLOR_VERDE_OK);
        else
          ui.mostrarMensaje("Intento " + String(k + 1) + " inválido (E" + String(it.codigo) + ")", textoCodigoPIM(it.codigo), COLOR_ROJO_DEL);
        delay(2500);
      }
    }

    cerrarSesionPIM(rp);
    sensoresResp.configurarModoPresion(false);
    std::vector<MuestraEspiro>().swap(pim);
    std::vector<MuestraEspiro>().swap(sensoresResp.bufferMuestras);
    if (rp.codigo == PIM_RES_OK) {
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
      if (calibId.length() == 0) calibId = evaluaciones.generarId(reloj.fechaHoraActual());
      String fechaPim = reloj.fechaHoraActual();
      String idPim = calibId + "_PIM";
      if (rp.idxReportado >= 0 && !evaluaciones.usarCsvTemporalPIM(rp.idxReportado, idPim))
        Serial.println("[SD] No se pudo mover la curva de la PIM");
      const std::vector<MuestraEspiro> sinDatos;
      bool pimGuardada = evaluaciones.guardarManiobra(idPim, calibId, "PIM", fechaPim, sinDatos, false, nullptr, &ambPim, &rp);
      evaluaciones.borrarTemporalesPIM();
      Serial.println(pimGuardada ? "[SD] PIM guardada: " + idPim : String("[SD] No se pudo guardar la PIM"));
      if (pimGuardada)
        ui.mostrarMensaje("PIM guardada en la SD",
                          rp.codigo == PIM_RES_OK ? "PIM: " + String(rp.pim_cmH2O, 1) + " cmH2O"
                                                  : "No reproducible (E" + String(rp.codigo) + ")",
                          rp.codigo == PIM_RES_OK ? COLOR_VERDE_OK : COLOR_ROJO_DEL);
      else
        ui.showErrorMessage("Error en SD", "No se pudo guardar la PIM");
      delay(2000);
    }

    bool sigue = sensoresResp.pimEnSensorFlujo()
      ? ui.mostrarPasoCalibracion("Paso 2 de 2: Espirometría", "Antes de seguir:", lConexEsp, 4, "LISTO", BRONQUITO_CYAN)
      : ui.mostrarPasoCalibracion("Descanso", "Muy bien. PIM lista.", lDescanso, 3, "CONTINUAR", BRONQUITO_CYAN);
    if (!sigue) { calibracionCancelada(); return; }
  }

  CondicionesAmbiente amb;
  while (true) {
    if (!ui.mostrarPasoCalibracion("Paso 2 de 2: Espirometría", "Instrucciones:", lEsp, 4, "COMENZAR", COLOR_VERDE_OK)) {
      calibracionCancelada();
      return;
    }
    amb = leerAmbiente();
    if (!capturarManiobra(false, DURACION_ESP_MS, esp, evEsp, amb.factor_btps)) { calibracionCancelada(); return; }

    if (!evEsp.valido) {
      // No pasó la validación: se guarda igual (json con el código + csv) y se muestra el motivo
      if (calibId.length() == 0) calibId = evaluaciones.generarId(reloj.fechaHoraActual());
      guardarFallida(esp, evEsp, amb, calibId, ++espFallidas);
      ui.mostrarMensaje("Maniobra inválida (E" + String(evEsp.codigo) + ")", textoCodigoEspiro(evEsp.codigo), COLOR_ROJO_DEL);
      delay(3000);
      std::vector<MuestraEspiro>().swap(esp);
      continue;
    }
    int r = ui.revisarManiobra("Paso 2 de 2: Espirometría", "FVC " + String(evEsp.fvc_L, 2) + " L",
                               "FEV1 " + String(evEsp.fev1_L, 2) + " L | PEF " + String(evEsp.pef_Lmin, 0) + " L/min");
    if (r == 1) break;
    if (r < 0) { calibracionCancelada(); return; }
    std::vector<MuestraEspiro>().swap(esp);
  }

  ui.mostrarMensaje("Guardando en SD...", "Por favor aguarde");
  // Mismo id que la PIM y los intentos fallidos de esta calibración
  if (calibId.length() == 0) calibId = evaluaciones.generarId(reloj.fechaHoraActual());
  String fecha = reloj.fechaHoraActual();
  String idPim = calibId + "_PIM", idEsp = calibId + "_ESP";

  bool ok = true;
  if (!sensoresResp.usaPIM())   // sin PIM: se guarda una PIM en cero, como antes
    ok = evaluaciones.guardarManiobra(idPim, calibId, "PIM", fecha, pim, true, nullptr, &amb);
  if (ok) ok = evaluaciones.guardarManiobra(idEsp, calibId, "ESPIROMETRIA", fecha, esp, false, &evEsp, &amb);
  if (!ok) {
    if (!sensoresResp.usaPIM()) evaluaciones.borrar(idPim);   // la PIM medida (ya guardada) no se borra
    evaluaciones.borrar(idEsp);
    Serial.println("[SD] Error al guardar la calibración");
    ui.showErrorMessage("Error en SD", "No se pudo guardar la prueba");
    delay(2000);
    return;
  }

  ui.mostrarMensaje("¡Calibración guardada!", "PIM y espirometría", COLOR_VERDE_OK);
  delay(1500);
}
