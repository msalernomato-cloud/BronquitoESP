// =============================================================================
// PRUEBA DE SEÑALES - Bronquito (ESP32)
// Lee el canal de flujo (venturi, GPIO36) y el de presión (PSA071040, GPIO39) a 100 Hz
// y los manda por el puerto serie (115200) para verlos con ver_senal.py (Python).
//
// Salida CSV, una línea por muestra:
//   t_s, v_flujo_V, v_pim_V, Q_Lmin, P_cmH2O, V_L, fase, v_flujo_crudo_V, v_pim_crudo_V
//   (las 7 primeras columnas son las mismas que el medidor del Arduino UNO)
//   fase: +1 espiración, -1 inspiración, 0 sin flujo (el sentido lo da la presión)
//   v_*_V: tensión filtrada (10 Hz) | v_*_crudo_V: tensión sin filtrar
//
// Al arrancar hace la tara durante 1 s: NO soplar ni aspirar.
// Comandos (los manda ver_senal.py, o escribiéndolos en el monitor serie):
//   t -> repetir la tara        f -> filtro de 10 Hz sí / no
//   m -> cambiar a formato para el Serial Plotter de Arduino (y volver)
// =============================================================================
#include <Arduino.h>
#include <math.h>

// ---- Pines ----
const int PIN_FLUJO   = 36;
const int PIN_PRESION = 39;

// ---- Conversión (las mismas constantes que RespiratorySensors.h) ----
const float K_FLUJO       = 346.15f;   // Q [L/min] = K * sqrt(V_neto)        (G flujo = 19,52 x 4,9 = 95,64)
const float K_PRESION     = 113.88f;   // P [cmH2O] = K * (V - V_cero)        (G presión = 13,82 x 3,7 = 51,14)
const float SIGNO_PRESION = 1.0f;      // poner -1 si al SOPLAR la presión da negativa

// ---- Filtro y detección ----
const int   N_PROMEDIO    = 16;        // lecturas del ADC promediadas por muestra
const float ZONA_MUERTA_V = 0.002f;    // V_neto menor a esto -> flujo = 0
const float UMBRAL_FASE   = 2.0f;      // cmH2O que tiene que cambiar la presión para decidir el sentido
const float ALPHA_BASE    = 0.02f;     // seguimiento de la presión de reposo (~0,5 s)
const int   N_FIN_EVENTO  = 5;         // 50 ms sin flujo -> fin del evento

// ---- Muestreo ----
const uint32_t PERIODO_US = 10000;     // 100 Hz
const float    DT         = PERIODO_US / 1e6f;

// ---- Pasabajos Butterworth 2º orden, fc = 10 Hz, fs = 100 Hz ----
struct Pasabajos10Hz {
  static constexpr float B0 = 0.067455f, B1 = 0.134911f, B2 = 0.067455f;
  static constexpr float A1 = -1.142980f, A2 = 0.412802f;
  float z1 = 0, z2 = 0;
  void reiniciar(float x0) { z1 = x0 * (1.0f - B0); z2 = x0 * (B2 - A2); }
  float filtrar(float x) { float y = B0 * x + z1; z1 = B1 * x - A1 * y + z2; z2 = B2 * x - A2 * y; return y; }
};

Pasabajos10Hz filtroFlujo, filtroPresion;
float vZeroFlujo = 0, vZeroPIM = 0;
float volumen_L = 0, presionBase = 0, volumenPendiente = 0;
int   fase = 0, muestrasSinFlujo = 0;
bool  enEvento = false, conFiltro = true, modoPlotter = false;
uint32_t t0_us = 0, proximo_us = 0;

float leerV(int pin) {
  uint32_t suma = 0;
  for (int i = 0; i < N_PROMEDIO; i++) suma += analogReadMilliVolts(pin);
  return (suma / (float)N_PROMEDIO) / 1000.0f;
}

void realizarTara() {
  Serial.println("# >> Tara en reposo (NO soplar ni aspirar)...");
  float sf = 0, sp = 0;
  for (int i = 0; i < 200; i++) { sf += leerV(PIN_FLUJO); sp += leerV(PIN_PRESION); delay(5); }
  vZeroFlujo = sf / 200;
  vZeroPIM   = sp / 200;
  filtroFlujo.reiniciar(vZeroFlujo);
  filtroPresion.reiniciar(vZeroPIM);
  volumen_L = 0; presionBase = 0; volumenPendiente = 0;
  fase = 0; enEvento = false; muestrasSinFlujo = 0;
  Serial.printf("# V_Zero Flujo: %.4f V   V_Zero PIM: %.4f V\n", vZeroFlujo, vZeroPIM);
  proximo_us = micros();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_FLUJO, ADC_11db);
  analogSetPinAttenuation(PIN_PRESION, ADC_11db);
  realizarTara();
  Serial.println("t_s,v_flujo_V,v_pim_V,Q_Lmin,P_cmH2O,V_L,fase,v_flujo_crudo_V,v_pim_crudo_V");
  t0_us = micros();
  proximo_us = t0_us;
}

void loop() {
  // ---- Comandos ----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 't')      realizarTara();
    else if (c == 'f') { conFiltro = !conFiltro; Serial.printf("# Filtro 10 Hz: %s\n", conFiltro ? "SI" : "NO"); }
    else if (c == 'm') modoPlotter = !modoPlotter;
  }

  // ---- Muestreo cada 10 ms ----
  if ((int32_t)(micros() - proximo_us) < 0) return;
  proximo_us += PERIODO_US;

  // A. Lectura
  float vfCrudo = leerV(PIN_FLUJO), vpCrudo = leerV(PIN_PRESION);

  // B. Filtro de 10 Hz (se actualiza siempre, para poder prenderlo y apagarlo sin saltos)
  float vfFilt = filtroFlujo.filtrar(vfCrudo), vpFilt = filtroPresion.filtrar(vpCrudo);
  float vf = conFiltro ? vfFilt : vfCrudo;
  float vp = conFiltro ? vpFilt : vpCrudo;

  // C. Presión con signo: + espiración, - inspiración
  float presion = SIGNO_PRESION * K_PRESION * (vp - vZeroPIM);

  // D. Magnitud del flujo (el venturi no da el sentido), con zona muerta
  float vNeto = fabsf(vf - vZeroFlujo);
  float magnitud = (vNeto > ZONA_MUERTA_V) ? K_FLUJO * sqrtf(vNeto) : 0.0f;

  // E. Sentido: se decide al comienzo de cada evento con la presión (igual que el medidor del UNO)
  float flujo = 0.0f;
  if (magnitud == 0.0f) {
    if (++muestrasSinFlujo >= N_FIN_EVENTO) { enEvento = false; fase = 0; volumenPendiente = 0; }
    if (!enEvento) presionBase += ALPHA_BASE * (presion - presionBase);
  } else {
    muestrasSinFlujo = 0;
    if (!enEvento) { enEvento = true; fase = 0; volumenPendiente = 0; }
    if (fase == 0) {
      float dP = presion - presionBase;
      if (dP > UMBRAL_FASE)  fase = 1;
      if (dP < -UMBRAL_FASE) fase = -1;
      if (fase == 0) volumenPendiente += (magnitud / 60.0f) * DT;
      else { volumen_L += fase * volumenPendiente; volumenPendiente = 0; }
    }
    if (fase != 0) flujo = fase * magnitud;
  }

  // F. Volumen = integral del flujo con signo (+ espiración, - inspiración)
  volumen_L += (flujo / 60.0f) * DT;

  // G. Salida
  if (modoPlotter) {
    Serial.printf("Flujo_Lmin:%.1f,Presion_cmH2O:%.2f\n", flujo, presion);
  } else {
    int faseSalida = (magnitud > 0.0f) ? fase : 0;
    Serial.printf("%.3f,%.4f,%.4f,%.2f,%.2f,%.4f,%d,%.4f,%.4f\n",
                  (micros() - t0_us) / 1e6f, vfFilt, vpFilt, flujo, presion, volumen_L, faseSalida, vfCrudo, vpCrudo);
  }
}
