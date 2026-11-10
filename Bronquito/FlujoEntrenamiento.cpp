#include "FlujoEntrenamiento.h"
#include "AnalisisEntrenamiento.h"

static const float DC_ENTRENAMIENTO   = 4.0f;    // duty cycle del PWM del servo durante el entrenamiento (%)
static const float SIN_RESPIRAR_MAX_S = 10.0f;   // sin flujo durante esto, la sesión se corta sola

FlujoEntrenamiento::FlujoEntrenamiento(BronquitoUI &ui, RespiratorySensors &sensoresResp, ValvulaActiva &valvula,
                                       TarjetaSD &tarjeta, Reloj &reloj)
  : ui(ui), sensoresResp(sensoresResp), valvula(valvula), tarjeta(tarjeta), reloj(reloj) {}

void FlujoEntrenamiento::mostrarError(const String &titulo, const String &mensaje) {
  Serial.println("[ENT] " + titulo + ": " + mensaje);
  ui.mostrarMensaje(titulo, mensaje, COLOR_ROJO_DEL);
  delay(2000);
}

void FlujoEntrenamiento::ejecutar() {
  const String lInstr[4] = {
    "1. Poné la boquilla.",
    "2. Desde vaciar los pulmones, inspirá",
    "   rápido y profundo hasta llenarte.",
    "3. Espirá todo y repetí (" + String(ciclosObjetivo) + " ciclos)."
  };
  if (!ui.mostrarPasoCalibracion("Entrenamiento muscular", "Instrucciones:", lInstr, 4, "COMENZAR", COLOR_VERDE_OK))
    return;

  // 1) Preparación: archivo en la SD y válvula en la posición de entrenamiento
  if (!valvula.disponible()) { mostrarError("No se pudo iniciar", "Revisá la válvula (GPIO5)"); return; }
  String id = tarjeta.nuevoIdEntrenamiento(reloj.fechaHoraActual());
  if (!tarjeta.iniciarEntrenamiento(id)) { mostrarError("No se pudo iniciar", "No se pudo escribir en la SD"); return; }
  if (!valvula.configurarEntrenamiento()) {
    valvula.abrir();
    tarjeta.descartarEntrenamiento(id);
    mostrarError("No se pudo iniciar", "No se pudo ajustar la válvula");
    return;
  }

  // 2) Pantalla primero y RECIÉN DESPUÉS la tara (igual que en la calibración: escribir la pantalla
  //    después de la tara corre el cero de los sensores)
  ui.drawTrainingRunning();
  sensoresResp.realizarTara();
  ui.setHintPim("Ya puede empezar");   // es la señal para empezar
  delay(50);                           // que la señal se asiente tras escribir
  Serial.printf("[ENT] Iniciado %s | DC %.1f%% | objetivo %d ciclos | cero (tara): %.4f V\n", id.c_str(),
                DC_ENTRENAMIENTO, ciclosObjetivo, sensoresResp.getVZeroFlujo());

  // 3) Captura: las muestras van a la SD de a bloques y, en vivo, SOLO se cuentan los ciclos
  //    (las features se calculan al final). Termina sola al llegar al objetivo, si no respira durante
  //    SIN_RESPIRAR_MAX_S, o cuando tocan "Detener".
  AnalisisEntrenamiento::ContadorCiclos contador;
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
  bool csvOk = tarjeta.cerrarEntrenamiento();
  if (!valvula.abrir()) Serial.println("[ENT] Error: la válvula no pudo volver a la posición abierta");
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
  {
    AnalisisEntrenamiento::SenalEntrenamiento senal;
    if (tarjeta.cargarSenalEntrenamiento(id, senal)) hayFeatures = AnalisisEntrenamiento::analizar(senal, features);
    senal.liberar();
  }
  if (hayFeatures)
    Serial.printf("[ENT] Features en %lu ms: %d ciclos, %d fases sueltas, %u muestras reparadas, reposo P %.1f cmH2O\n",
                  (unsigned long)(millis() - tProc), features.n_ciclos_completos, features.n_fases_sueltas,
                  (unsigned)features.muestras_reparadas, features.presion_base_cmH2O);
  else
    Serial.println("[ENT] No se pudieron calcular las features: se guardan en null");

  if (!tarjeta.guardarEntrenamiento(id, duracion_s, DC_ENTRENAMIENTO, estado, fin, ciclosObjetivo,
                                    hayFeatures ? &features : nullptr)) {
    mostrarError("Error en SD", "No se pudo guardar la medición");
    return;
  }
  Serial.println("[ENT] Sesión guardada: " + id);
  ui.mostrarMensaje("Entrenamiento guardado",
                    hayFeatures ? "La válvula quedó abierta" : "Sin features (ver serie)",
                    hayFeatures ? COLOR_VERDE_OK : COLOR_ROJO_DEL);
  delay(1500);
}
