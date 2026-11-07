#include "RespiratorySensors.h"

RespiratorySensors::RespiratorySensors(int pinFlujo, int pinPIM)
  : _pinFlujo(pinFlujo), _pinPIM(pinPIM),
    vZeroFlujo(0.0f), vZeroPIM(0.0f),
    vFlujoFiltrado(0.0f), vPIMFiltrado(0.0f),
    volumen_L(0.0f), fase(0), enEvento(false),
    muestrasSinFlujo(0), presionBase(0.0f),
    volumenPendiente(0.0f), tInicio_us(0), tAnterior_us(0) {}

RespiratorySensors *RespiratorySensors::instancia = nullptr;

// ---- Muestreo por interrupción ----
void ARDUINO_ISR_ATTR RespiratorySensors::isrMuestreo() {
  BaseType_t despertoMasPrioritaria = pdFALSE;
  if (instancia && instancia->tareaMuestreoHandle)
    vTaskNotifyGiveFromISR(instancia->tareaMuestreoHandle, &despertoMasPrioritaria);
  if (despertoMasPrioritaria == pdTRUE) portYIELD_FROM_ISR();
}

void RespiratorySensors::tareaMuestreo(void *arg) {
  RespiratorySensors *s = static_cast<RespiratorySensors *>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // espera la interrupción del timer
    s->tomarMuestraTimer();
  }
}

void RespiratorySensors::tomarMuestraTimer() {
  if (!muestreando) return;
  if (nMuestras >= capacidad) { muestreando = false; return; }

  MuestraEspiro m;
  if (congelado || saltarPendiente > 0) {
    m = ultimaMuestra;
    m.tiempo_s = (micros() - tInicio_us) / 1e6f;
    if (!congelado) saltarPendiente = saltarPendiente - 1;
  } else {
    m = procesarMuestra();
    ultimaMuestra = m;
  }
  bufferMuestras[nMuestras] = m;
  nMuestras = nMuestras + 1;   // recién ahora el loop la puede leer
}

void RespiratorySensors::configurarMuestreo() {
  instancia = this;
  // Misma CPU que el loop (núcleo 1) y más prioridad: la tarea lo interrumpe apenas llega la interrupción.
  // El núcleo 0 queda para el Wi-Fi.
  xTaskCreatePinnedToCore(tareaMuestreo, "muestreo", 4096, this, 5, &tareaMuestreoHandle, 1);
  timerMuestreo = timerBegin(1000000);                 // timer de 1 MHz (1 tick = 1 us)
  if (!timerMuestreo) { Serial.println("[SENS] No se pudo crear el timer de muestreo"); return; }
  timerAttachInterrupt(timerMuestreo, &isrMuestreo);
  timerAlarm(timerMuestreo, PERIODO_US, true, 0);      // cada 10 ms, repetitivo
  timerStop(timerMuestreo);                            // arranca recién con iniciarMuestreo()
}

bool RespiratorySensors::iniciarMuestreo(size_t maxMuestras) {
  if (!timerMuestreo || !tareaMuestreoHandle) return false;
  timerStop(timerMuestreo);
  muestreando = false;
  iniciarManiobra();                                   // reinicia el estado y el tiempo cero

  // Memoria: primero se LIBERA el buffer anterior (clear() no devuelve la memoria) y recién después se pide
  // uno nuevo del tamaño justo. Si no, un vector que crece pide el bloque nuevo con el viejo todavía
  // ocupado y el ESP32 se queda sin memoria contigua (aborta y se reinicia).
  std::vector<MuestraEspiro>().swap(bufferMuestras);
  size_t bytes = maxMuestras * sizeof(MuestraEspiro);
  Serial.printf("[MEM] Captura de %u muestras (%u bytes) | memoria libre %u | mayor bloque libre %u\n",
                (unsigned)maxMuestras, (unsigned)bytes, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (ESP.getMaxAllocHeap() < bytes + 4096) {          // margen para el análisis posterior
    Serial.println("[MEM] No hay memoria suficiente para la captura");
    return false;
  }
  bufferMuestras.assign(maxMuestras, MuestraEspiro{0, 0, 0, 0, 0, 0, 0});
  capacidad = maxMuestras;
  nMuestras = 0;
  saltarPendiente = 0;
  congelado = false;
  ultimaMuestra = MuestraEspiro{0, 0, 0, 0, 0, 0, 0};
  muestreando = true;
  timerRestart(timerMuestreo);
  timerStart(timerMuestreo);
  return true;
}

void RespiratorySensors::detenerMuestreo() {
  if (timerMuestreo) timerStop(timerMuestreo);
  muestreando = false;
  delay(2);                                            // por si la tarea estaba terminando una muestra
  bufferMuestras.resize(nMuestras);
}

void RespiratorySensors::begin() {
  pinMode(_pinFlujo, INPUT);
  if (USAR_PIM) pinMode(_pinPIM, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  configurarMuestreo();
}

float RespiratorySensors::leerV(int pin, int nProm) const {
  uint32_t suma = 0;
  for (int i = 0; i < nProm; i++) {
    suma += analogReadMilliVolts(pin);
  }
  return suma / (1000.0f * nProm);
}

void RespiratorySensors::realizarTara() {
  const int MUESTRAS_TARA = 200;
  float sumaFlujo = 0.0f, sumaPIM = 0.0f;

  for (int i = 0; i < MUESTRAS_TARA; i++) {
    sumaFlujo += leerV(_pinFlujo, N_PROM_FLUJO);
    if (USAR_PIM) sumaPIM += leerV(_pinPIM, N_PROM_PIM);
    delay(5);
  }

  vZeroFlujo = sumaFlujo / MUESTRAS_TARA;
  vZeroPIM   = (USAR_PIM) ? sumaPIM / MUESTRAS_TARA : 0.0f;

  vFlujoFiltrado = vZeroFlujo;
  vPIMFiltrado   = vZeroPIM;
  filtroFlujo.reiniciar(vZeroFlujo);
  filtroPIM.reiniciar(vZeroPIM);

  iniciarManiobra();
}

void RespiratorySensors::iniciarManiobra() {
  volumen_L        = 0.0f;
  fase             = 0;
  enEvento         = false;
  muestrasSinFlujo = 0;
  presionBase      = 0.0f;
  volumenPendiente = 0.0f;
  tInicio_us       = micros();
  tAnterior_us     = tInicio_us;

  bufferMuestras.clear();       // la memoria la reserva iniciarMuestreo(), con el tamaño justo
}

MuestraEspiro RespiratorySensors::procesarMuestra() {
  float vFlujoBruto = leerV(_pinFlujo, N_PROM_FLUJO);
  float vPIMBruto   = (USAR_PIM) ? leerV(_pinPIM, N_PROM_PIM) : 0.0f;

  uint32_t ahora_us = micros();
  float dt = (ahora_us - tAnterior_us) / 1e6f;
  tAnterior_us = ahora_us;
  if (dt <= 0.0f || dt > 0.05f) dt = DT;

  vFlujoFiltrado = filtroFlujo.filtrar(vFlujoBruto);   // pasabajos de 10 Hz

  float presion_cmH2O = 0.0f;
  if (USAR_PIM) {
    vPIMFiltrado  = filtroPIM.filtrar(vPIMBruto);
    presion_cmH2O = SIGNO_PRESION * K_PIM * (vPIMFiltrado - vZeroPIM);
  }

  float vNetoFlujo = fabsf(vFlujoFiltrado - vZeroFlujo);
  float raiz = (vNetoFlujo > ZONA_MUERTA_V) ? sqrtf(vNetoFlujo) : 0.0f;
  float magnitud_Lmin = K_ESPIRACION * raiz;   // si resulta inspiración, se recalcula con K_INSPIRACION

  float flujo_Lmin = 0.0f;

  if (magnitud_Lmin == 0.0f) {
    muestrasSinFlujo++;
    if (muestrasSinFlujo >= N_FIN_EVENTO) {
      enEvento = false;
      fase = 0;
      volumenPendiente = 0.0f;
    }
    if (!enEvento) {
      presionBase += ALPHA_BASE * (presion_cmH2O - presionBase);
    }
  } else {
    muestrasSinFlujo = 0;
    if (!enEvento) {
      enEvento = true;
      fase = 0;
      volumenPendiente = 0.0f;
    }

    // Sentido del flujo: lo da el sensor de presión, en CADA muestra (así se ve el paso de la
    // espiración a la inspiración aunque no haya una pausa). Presión positiva = espiración,
    // negativa = inspiración. Si la presión está cerca de cero se mantiene el sentido anterior.
    if (!USAR_PIM) {   // sin el sensor de presión no se conoce el sentido: se asume espiración
      fase = 1;
    } else {
      float dP = presion_cmH2O - presionBase;
      if (dP > UMBRAL_FASE)       fase =  1;
      else if (dP < -UMBRAL_FASE) fase = -1;
    }

    if (fase == 0) {
      volumenPendiente += (magnitud_Lmin / 60.0f) * dt;
    } else {
      volumenPendiente = 0.0f;
      if (fase < 0) magnitud_Lmin = K_INSPIRACION * raiz;
      flujo_Lmin = fase * magnitud_Lmin;
    }
  }

  MuestraEspiro m;
  m.tiempo_s      = (ahora_us - tInicio_us) / 1e6f;
  m.vFlujo_V      = vFlujoFiltrado;
  m.vPIM_V        = vPIMFiltrado;
  m.flujo_Lmin    = flujo_Lmin;
  m.presion_cmH2O = presion_cmH2O;
  m.volumen_L     = 0.0f; // Se calculará de forma retrospectiva al terminar la prueba
  m.fase          = (magnitud_Lmin > 0.0f) ? fase : 0;

  return m;
}

String RespiratorySensors::obtenerLineaCSV(const MuestraEspiro& m) const {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.3f,%.4f,%.2f,%.2f", m.tiempo_s, m.vFlujo_V, m.flujo_Lmin, m.presion_cmH2O);
  return String(buf);
}                                
// ---- Captura completa ----
ResultadoCaptura RespiratorySensors::capturar(uint32_t duracionMs, size_t cadaN,
                                              std::function<void(const MuestraEspiro &)> mostrar,
                                              std::function<bool()> cancelar) {
  if (!iniciarMuestreo(duracionMs / 10 + 20)) return CAPTURA_SIN_MEMORIA;

  Serial.println("t_s,v_flujo_V,Q_Lmin,P_cmH2O");
  uint32_t tInicio = millis(), ultimoCancelar = tInicio;
  size_t impresas = 0, ultimoRefresco = 0;
  bool cancelada = false;

  while (!cancelada && (millis() - tInicio < duracionMs) && nMuestras < capacidad) {
    size_t n = nMuestras;
    while (impresas < n) Serial.println(obtenerLineaCSV(bufferMuestras[impresas++]));

    if (mostrar && n > 0 && n / cadaN > ultimoRefresco) {
      ultimoRefresco = n / cadaN;
      MuestraEspiro ultima = bufferMuestras[n - 1];
      congelado = true;        // la tarea no lee el ADC mientras se escribe la pantalla...
      mostrar(ultima);
      congelado = false;
      saltarPendiente = 3;     // ...ni en las 3 muestras siguientes
    }

    if (cancelar && millis() - ultimoCancelar >= 100) {
      ultimoCancelar = millis();
      cancelada = cancelar();
    }
    delay(1);
  }
  detenerMuestreo();
  while (impresas < bufferMuestras.size()) Serial.println(obtenerLineaCSV(bufferMuestras[impresas++]));

  if (cancelada) {
    liberarMemoria();
    return CAPTURA_CANCELADA;
  }
  return CAPTURA_OK;
}

void RespiratorySensors::entregarMuestras(std::vector<MuestraEspiro> &destino) {
  std::vector<MuestraEspiro>().swap(destino);   // libera lo que tenía el destino
  destino.swap(bufferMuestras);                 // el destino se queda con las muestras, el sensor vacío
}
