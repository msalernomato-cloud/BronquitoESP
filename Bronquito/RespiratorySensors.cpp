#include "RespiratorySensors.h"
#include "AnalisisEspiro.h"   // SIGNO_PRESION_ESPIRACION

RespiratorySensors::RespiratorySensors(int pinFlujo, int pinPIM)
  : _pinFlujo(pinFlujo), _pinPIM(pinPIM),
    vZeroFlujo(0.0f), vZeroPIM(0.0f),
    vFlujoFiltrado(0.0f), vPIMFiltrado(0.0f),
    volumen_L(0.0f), fase(0), enEvento(false),
    muestrasSinFlujo(0), presionBase(0.0f),
    volumenPendiente(0.0f), tInicio_us(0), tAnterior_us(0) {}

// =============================================================================
// MUESTREO POR INTERRUPCIÓN DE SOFTWARE (Ticker)
// =============================================================================
// Mediana de 3 valores: si una sola lectura salta (pico del ADC), queda afuera
static inline uint16_t mediana3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return (a > b) ? a : b;
}

// La llama el Ticker cada 1 ms. Tiene que ser corta: lee, guarda y vuelve.
void RespiratorySensors::alInterrumpir(RespiratorySensors *s) {
  s->leerADC();
}

void RespiratorySensors::leerADC() {
  if (!muestreando) return;

  // Mientras se escribe la pantalla (y 3 muestras después) no se lee el ADC
  bool leer = !(congelado || saltarPendiente > 0);
  if (leer) {
    uint16_t f = analogReadMilliVolts(_pinFlujo);
    uint16_t p = USAR_PIM ? analogReadMilliVolts(_pinPIM) : 0;

    // 1. Mediana de 3 (esta lectura y las dos anteriores)
    uint16_t fm = mediana3(f, previasFlujo[0], previasFlujo[1]);
    uint16_t pm = mediana3(p, previasPIM[0], previasPIM[1]);
    previasFlujo[1] = previasFlujo[0]; previasFlujo[0] = f;
    previasPIM[1]   = previasPIM[0];   previasPIM[0]   = p;

    // 2. Ventana de 20 ms: se saca la lectura más vieja y se suma la nueva
    sumaFlujo += fm; sumaFlujo -= ventanaFlujo[idxVentana]; ventanaFlujo[idxVentana] = fm;
    sumaPIM   += pm; sumaPIM   -= ventanaPIM[idxVentana];   ventanaPIM[idxVentana]   = pm;
    idxVentana = (idxVentana + 1) % N_VENTANA_50HZ;
  }

  // 3. Cada 10 lecturas (10 ms) sale una muestra
  if (++lecturasDesdeMuestra < LECTURAS_POR_MUESTRA) return;
  lecturasDesdeMuestra = 0;
  tomarMuestra();
}

void RespiratorySensors::tomarMuestra() {
  if (nMuestras >= capacidad) { muestreando = false; return; }

  MuestraEspiro m;
  if (congelado || saltarPendiente > 0) {
    m = ultimaMuestra;
    m.tiempo_s = (micros() - tInicio_us) / 1e6f;
    if (!congelado) saltarPendiente = saltarPendiente - 1;
  } else {
    float vFlujo = sumaFlujo / (1000.0f * N_VENTANA_50HZ);   // promedio de 20 ms, en V
    float vPIM   = sumaPIM   / (1000.0f * N_VENTANA_50HZ);
    m = procesarMuestra(vFlujo, vPIM);
    ultimaMuestra = m;
  }
  bufferMuestras[nMuestras] = m;
  nMuestras = nMuestras + 1;   // recién ahora el loop la puede leer
}

// La ventana arranca llena con el cero de la tara: así la primera muestra no arrastra un transitorio
void RespiratorySensors::reiniciarVentanas() {
  uint16_t zf = (uint16_t)lroundf(vZeroFlujo * 1000.0f);
  uint16_t zp = (uint16_t)lroundf(vZeroPIM * 1000.0f);
  for (int i = 0; i < N_VENTANA_50HZ; i++) { ventanaFlujo[i] = zf; ventanaPIM[i] = zp; }
  sumaFlujo = (uint32_t)zf * N_VENTANA_50HZ;
  sumaPIM   = (uint32_t)zp * N_VENTANA_50HZ;
  previasFlujo[0] = previasFlujo[1] = zf;
  previasPIM[0]   = previasPIM[1]   = zp;
  idxVentana = 0;
  lecturasDesdeMuestra = 0;
}

bool RespiratorySensors::iniciarMuestreo(size_t maxMuestras) {
  InterrupcionSoftware.detach();
  muestreando = false;
  iniciarManiobra();                                   // reinicia el estado y el tiempo cero
  reiniciarVentanas();
  filtroFlujo.reiniciar(vZeroFlujo);
  filtroPIM.reiniciar(vZeroPIM);

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
  tInicio_us = micros();
  tAnterior_us = tInicio_us;
  muestreando = true;

  // Se habilita al final, cuando ya está todo listo (como en el ejemplo: si no, interrumpe a mitad
  // de la preparación). Interrumpe cada 1 ms y va a alInterrumpir(this).
  InterrupcionSoftware.attach_ms(PERIODO_LECTURA_MS, alInterrumpir, this);
  return true;
}

void RespiratorySensors::detenerMuestreo() {
  muestreando = false;
  InterrupcionSoftware.detach();
  delay(3);                                            // por si estaba terminando una lectura
  bufferMuestras.resize(nMuestras);
}

void RespiratorySensors::begin() {
  pinMode(_pinFlujo, INPUT);
  if (USAR_PIM) pinMode(_pinPIM, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
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
  float acumFlujo = 0.0f, acumPIM = 0.0f;

  for (int i = 0; i < MUESTRAS_TARA; i++) {     // ~1 s: el promedio ya cancela los 50 Hz
    acumFlujo += leerV(_pinFlujo, N_PROM_TARA);
    if (USAR_PIM) acumPIM += leerV(_pinPIM, N_PROM_TARA);
    delay(5);
  }

  vZeroFlujo = acumFlujo / MUESTRAS_TARA;
  vZeroPIM   = (USAR_PIM) ? acumPIM / MUESTRAS_TARA : 0.0f;

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

// vFlujoBruto y vPIMBruto ya vienen con la mediana y el promedio de 20 ms (sin 50 Hz)
MuestraEspiro RespiratorySensors::procesarMuestra(float vFlujoBruto, float vPIMBruto) {

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
    // espiración a la inspiración aunque no haya una pausa). Con la placa actual la presión es
    // NEGATIVA al espirar y POSITIVA al inspirar; dP se multiplica por SIGNO_PRESION_ESPIRACION para
    // que dP > 0 sea espiración. Si la presión está cerca de cero se mantiene el sentido anterior.
    // (Esto es solo para ver en vivo: el análisis decide el sentido de nuevo, por evento.)
    if (!USAR_PIM) {   // sin el sensor de presión no se conoce el sentido: se asume espiración
      fase = 1;
    } else {
      float dP = AnalisisEspiro::SIGNO_PRESION_ESPIRACION * (presion_cmH2O - presionBase);
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
