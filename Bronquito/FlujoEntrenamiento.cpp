#include "FlujoEntrenamiento.h"
#include "AnalisisEntrenamiento.h"

static const float SIN_RESPIRAR_MAX_S = 10.0f;   // sin flujo durante esto, la sesión se corta sola

FlujoEntrenamiento::FlujoEntrenamiento(BronquitoUI &ui, RespiratorySensors &sensoresResp, ValvulaActiva &valvula,
                                       TarjetaSD &tarjeta, Reloj &reloj, BTPSSensor &btps)
  : ui(ui), sensoresResp(sensoresResp), valvula(valvula), tarjeta(tarjeta), reloj(reloj), btps(btps) {}

// Temperatura y humedad para el factor BTPS (lo mismo que hace la evaluación)
CondicionesAmbiente FlujoEntrenamiento::leerAmbiente() {
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

void FlujoEntrenamiento::mostrarError(const String &titulo, const String &mensaje) {
  Serial.println("[ENT] " + titulo + ": " + mensaje);
  ui.mostrarMensaje(titulo, mensaje, COLOR_ROJO_DEL);
  delay(2000);
}

// "AAAA-MM-DD" -> "DD/MM/AAAA"
static String fechaCorta(const String &f) {
  if (f.length() < 10) return f;
  return f.substring(8, 10) + "/" + f.substring(5, 7) + "/" + f.substring(0, 4);
}

void FlujoEntrenamiento::ejecutar() {
  // 0) Nivel de resistencia y vigencia de la evaluación. En modo automático no se puede entrenar sin una
  //    evaluación vigente (15 días); en modo manual (lo fijó el médico) no vence.
  EstadoResistencia res = tarjeta.estadoResistencia(reloj.fechaHoraActual());
  if (!res.puedeEntrenar) {
    if (!res.hayEvaluacion) {
      Serial.println("[ENT] Sin evaluación válida: no se puede entrenar");
      ui.mostrarError("Sin calibración", "Primero hacé una evaluación");
    } else {
      Serial.println("[ENT] Evaluación vencida el " + res.vence + ": no se puede entrenar");
      ui.mostrarError("Calibración vencida", "Venció el " + res.vence.substring(8, 10) + "/" +
                      res.vence.substring(5, 7) + ": hacé una evaluación");
    }
    return;
  }
  if (!res.manual && !res.hayFechaHoy) {
    Serial.println("[ENT] El equipo no tiene la hora: no se puede verificar la vigencia de la evaluación");
    ui.mostrarMensaje("No se pudo verificar la vigencia", "El equipo no tiene la fecha", BRONQUITO_ORANGE);
    delay(2500);
  }
  Serial.printf("[ENT] Resistencia: nivel %d (%s) | última evaluación %s\n", res.nivel,
                res.manual ? "manual" : "automático", res.hayEvaluacion ? res.fechaEvaluacion.c_str() : "ninguna");
  String subtitulo = res.hayEvaluacion ? "Última calibración: " + fechaCorta(res.fechaEvaluacion)
                                       : String("Sin calibraciones (nivel manual)");

  // Hasta ~34 caracteres por renglón: más largo se sale del recuadro (tamaño 2 = 12 px por letra)
  const String lInstr[4] = {
    "1. Poné la boquilla.",
    "2. Vaciá los pulmones e inspirá",
    "   rápido y hondo hasta llenarte.",
    "3. Espirá todo y repetí " + String(ciclosObjetivo) + " veces."
  };
  if (!ui.mostrarPasoCalibracion("Entrenamiento muscular", subtitulo, lInstr, 4, "COMENZAR", COLOR_VERDE_OK))
    return;

  // 1) Preparación: temperatura y humedad (BTPS), K de cada sentido y archivo en la SD
  CondicionesAmbiente amb = leerAmbiente();
  AnalisisEntrenamiento::ConversionFlujo conv;
  conv.kEspiracion  = sensoresResp.getKEspiracion();
  conv.kInspiracion = sensoresResp.getKInspiracion();
  conv.factorBTPS   = amb.valida ? amb.factor_btps : 1.0f;
  String fechaHora = reloj.fechaHoraActual();   // la del inicio: va en el id y en el .json
  String id = tarjeta.nuevoIdEntrenamiento(fechaHora);
  if (!tarjeta.iniciarEntrenamiento(id)) { mostrarError("No se pudo iniciar", "No se pudo escribir en la SD"); return; }

  // Válvula al DC del nivel. El PWM queda prendido SOLO hasta que termina la captura. Va antes de la
  // tara: así el cero se toma con el servo ya quieto en su posición y con su consumo incluido.
  ui.mostrarMensaje("Ajustando la resistencia...", "Nivel " + String(res.nivel));
  if (!valvula.activar(res.nivel)) {
    tarjeta.descartarEntrenamiento(id);
    mostrarError("No se pudo iniciar", "Revisá la válvula (GPIO5)");
    return;
  }
  const float dcEntrenamiento = valvula.dcActual();   // va en el "dc" del .json

  // 2) Pantalla primero y RECIÉN DESPUÉS la tara (igual que en la calibración: escribir la pantalla
  //    después de la tara corre el cero de los sensores)
  ui.drawTrainingRunning();
  sensoresResp.realizarTara();
  ui.setHintPim("Ya puede empezar");   // es la señal para empezar
  delay(50);                           // que la señal se asiente tras escribir
  Serial.printf("[ENT] Iniciado %s | objetivo %d ciclos | cero (tara): %.4f V\n", id.c_str(), ciclosObjetivo,
                sensoresResp.getVZeroFlujo());

  // 3) Captura: las muestras van a la SD de a bloques y, en vivo, SOLO se cuentan los ciclos
  //    (las features se calculan al final). Termina sola al llegar al objetivo, si no respira durante
  //    SIN_RESPIRAR_MAX_S, o cuando tocan "Detener".
  AnalisisEntrenamiento::ContadorCiclos contador;
  contador.configurar(conv);   // el volumen de cada fase (filtro de VOL_MIN_FASE_L) con su K y BTPS
  contador.reiniciar();
  const char *fin = "detenida";
  uint32_t t0 = millis();
  ResultadoCaptura r = sensoresResp.capturarContinua(
      [&](const MuestraEspiro *m, size_t n) {
        if (!tarjeta.agregarMuestrasEntrenamiento(m, n)) return false;
        for (size_t i = 0; i < n; i++)
          if (contador.agregar(m[i].tiempo_s, m[i].flujo_Lmin, m[i].presion_cmH2O))
            Serial.printf("[ENT] Ciclo %d (%.1f s)\n", contador.ciclos(), m[i].tiempo_s);
        return true;   // la pantalla queda fija: no se escribe nada mientras se mide
      },
      [&]() {
        if (contador.ciclos() >= ciclosObjetivo) { fin = "objetivo"; return true; }
        if (contador.segundosSinFlujo() > SIN_RESPIRAR_MAX_S) { fin = "sin_respiracion"; return true; }
        return ui.cancelarTocado();
      });
  float duracion_s = (millis() - t0) / 1000.0f;
  valvula.liberar();   // abierta y sin PWM: no queda prendido fuera del entrenamiento
  bool csvOk = tarjeta.cerrarEntrenamiento();
  Serial.printf("[ENT] Fin (%s): %.1f s | %d ciclos | resultado de la captura %d\n", fin, duracion_s,
                contador.ciclos(), (int)r);

  // 4) Errores: sin memoria o sin SD no hay nada que guardar
  if (r == CAPTURA_SIN_MEMORIA) {
    tarjeta.descartarEntrenamiento(id);
    mostrarError("No se pudo medir", "Falta memoria (ver serie)");
    return;
  }
  if (r == CAPTURA_ERROR_GUARDADO || !csvOk) {
    tarjeta.descartarEntrenamiento(id);
    mostrarError("Error en SD", "No se pudo guardar el entrenamiento");
    return;
  }

  // 5) ¿Guardar? Completa = llegó al objetivo; si se perdieron muestras, queda como interrumpida
  const char *estado = (r == CAPTURA_DESBORDADA) ? "interrumpida"
                     : (contador.ciclos() >= ciclosObjetivo) ? "completa" : "incompleta";
  const char *titulo = (strcmp(estado, "completa") == 0) ? "Entrenamiento completo"
                     : (strcmp(fin, "sin_respiracion") == 0) ? "Sin respiración: se detuvo"
                     : (strcmp(estado, "interrumpida") == 0) ? "Entrenamiento interrumpido"
                     : "Entrenamiento detenido";
  int seg = (int)lroundf(duracion_s);
  char resumen[48];
  snprintf(resumen, sizeof(resumen), "%d de %d ciclos - %d:%02d", min(contador.ciclos(), ciclosObjetivo),
           ciclosObjetivo, seg / 60, seg % 60);
  bool guardar = ui.pedirConfirmacion(titulo, resumen, "¿Guardar la medición?", "No", "Sí");
  if (!guardar) {
    if (!tarjeta.descartarEntrenamiento(id)) mostrarError("No se pudo descartar", "Revisá la tarjeta SD");
    else Serial.println("[ENT] Sesión descartada");
    return;
  }

  // 6) Procesar: se relee la señal de la SD y se calculan las features (como el Python, con toda la
  //    captura). Si no hay memoria se guarda igual, con las features en null.
  ui.mostrarMensaje("Procesando y guardando...", "No retires la tarjeta SD");
  uint32_t tProc = millis();
  AnalisisEntrenamiento::FeaturesEntrenamiento features;
  bool hayFeatures = false;
  std::vector<int16_t> flujoProc;   // flujo procesado de cada muestra, para la columna flujo_proc_lmin del CSV
  {
    AnalisisEntrenamiento::SenalEntrenamiento senal;
    if (tarjeta.cargarSenalEntrenamiento(id, senal))
      hayFeatures = AnalisisEntrenamiento::analizar(senal, features, conv, &flujoProc);
    senal.liberar();
  }
  if (!hayFeatures) std::vector<int16_t>().swap(flujoProc);
  uint32_t tCol = millis();
  tarjeta.agregarFlujoProcesado(id, flujoProc);   // si falla, el CSV queda sin la columna: se guarda igual
  Serial.printf("[ENT] Columna de flujo procesado en %lu ms\n", (unsigned long)(millis() - tCol));
  std::vector<int16_t>().swap(flujoProc);
  if (hayFeatures)
    Serial.printf("[ENT] Features en %lu ms: %d ciclos, %d fases sueltas, %u muestras reparadas, reposo P %.1f cmH2O\n",
                  (unsigned long)(millis() - tProc), features.n_ciclos_completos, features.n_fases_sueltas,
                  (unsigned)features.muestras_reparadas, features.presion_base_cmH2O);
  else
    Serial.println("[ENT] No se pudieron calcular las features: se guardan en null");

  if (!tarjeta.guardarEntrenamiento(id, fechaHora, amb, res.nivel, res.manual, duracion_s, dcEntrenamiento, estado, fin, ciclosObjetivo,
                                    hayFeatures ? &features : nullptr)) {
    mostrarError("Error en SD", "No se pudo guardar la medición");
    return;
  }
  Serial.println("[ENT] Sesión guardada: " + id);
  ui.mostrarMensaje("Entrenamiento guardado",
                    hayFeatures ? "" : "Sin features (ver serie)",
                    hayFeatures ? COLOR_VERDE_OK : COLOR_ROJO_DEL);
  delay(1500);
}
