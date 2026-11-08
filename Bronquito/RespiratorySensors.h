#ifndef RESPIRATORY_SENSORS_H
#define RESPIRATORY_SENSORS_H

#include <Arduino.h>
#include <vector>
#include "AnalisisEspiro.h"
#include "AnalisisPIM.h"

struct MuestraEspiro {
  float tiempo_s;
  float vFlujo_V;
  float vPIM_V;
  float flujo_Lmin;
  float presion_cmH2O;
  float volumen_L;
  int fase;
};

class RespiratorySensors {
public:
  RespiratorySensors(int pinFlujo = 36, int pinPIM = 39);

  void begin();
  void realizarTara();
  void iniciarManiobra();
  MuestraEspiro procesarMuestra();
  String obtenerLineaCSV(const MuestraEspiro& m) const;

  // Se llama al TERMINAR la adquisición: toma la espiración de mayor flujo, retrocede por la curva
  // hasta el cruce por cero (t0) y calcula el volumen solo dentro del evento (FVC, FEV1, PEF).
  // Usa vFlujo_V con signo y sin zona muerta; el umbral sale del ruido de la captura. Ver AnalisisEspiro.h
  // factorBTPS multiplica flujos y volúmenes (1 = sin corregir)
  EventoEspiratorio calcularVolumenPostEvento(float factorBTPS = 1.0f);

  // false = prueba sin PIM
  bool usaPIM() const { return USAR_PIM; }
  // true = la PIM se mide con el MISMO sensor diferencial del flujo (cambiando las mangueras)
  bool pimEnSensorFlujo() const { return USAR_PIM && PIM_EN_SENSOR_FLUJO; }
  // Con PIM en el sensor de flujo: true = la próxima maniobra mide presión (cmH2O), false = flujo.
  // Llamar ANTES de realizarTara().
  void configurarModoPresion(bool presion) { modoPresion = presion; }

  float getVZeroFlujo() const { return vZeroFlujo; }
  float getVZeroPIM() const { return vZeroPIM; }
  float getKFlujo() const { return K_FLUJO; }

  // ---- Muestreo por interrupción ----
  // Un timer de hardware interrumpe cada PERIODO_US (10 ms = 100 Hz). La interrupción NO lee el ADC
  // (analogReadMilliVolts no se puede usar dentro de una ISR y 16 lecturas tardan demasiado): despierta
  // una tarea de FreeRTOS de prioridad alta que toma la muestra. Esa tarea interrumpe al loop (pantalla,
  // táctil, serie), así que el período no depende de lo que esté haciendo el resto del programa.
  void configurarMuestreo();                    // crea el timer y la tarea (se llama desde begin)
  bool iniciarMuestreo(size_t maxMuestras);     // arranca una captura nueva (llama a iniciarManiobra)
  void detenerMuestreo();                       // para el timer y deja bufferMuestras con lo tomado
  size_t muestrasTomadas() const { return nMuestras; }
  bool muestreoLleno() const { return nMuestras >= capacidad; }
  MuestraEspiro muestra(size_t i) const { return bufferMuestras[i]; }
  // Escribir en la pantalla mete un pico en la lectura analógica: mientras se escribe (congelar) y en las
  // muestras siguientes (saltar) no se lee el ADC, se repite la última muestra con el tiempo actualizado.
  void congelarMuestreo(bool c) { congelado = c; }
  void saltarMuestras(int n) { saltarPendiente = n; }

  // Buffer de muestras guardadas en RAM
  std::vector<MuestraEspiro> bufferMuestras;

private:
  const int _pinFlujo;
  const int _pinPIM;

  // Configuration
  const bool USAR_PIM = true;
  // PIM con el MPX2010 del venturi: entrada del venturi (lado boquilla) al puerto P2, P1 abierto al aire
  // y la salida del venturi tapada. Al inspirar, P1 > P2 y la salida sube. Si conectan la boquilla a P1
  // (y P2 al aire), la presión da negativa: en ese caso poner SIGNO_PRESION_PIM = -1.
  const bool  PIM_EN_SENSOR_FLUJO = true;
  const float SIGNO_PRESION_PIM   = 1.0f;

  const float RG_INA_OHM     = 1000.0f;
  const float GANANCIA_FLUJO = 1.0f + 50000.0f / RG_INA_OHM;   // 51 (con RG = 1k)
  const float SENS_MPX_V_PA  = 1.25e-6f;                       // 1,25 mV/kPa
  const float K_PA_A_LMIN    = 3.7848f;
  const float K_FLUJO        = K_PA_A_LMIN / sqrtf(GANANCIA_FLUJO * SENS_MPX_V_PA);   // ≈ 474 (con RG = 1k)

  const float K_PIM         = 114.18f;   // sensor de presión aparte (PSA071040) en el pin de PIM
  // MPX2010 como manómetro: cmH2O = dV / (ganancia * 1,25 uV/Pa * 98,0665 Pa/cmH2O)  -> ~160 cmH2O/V con G = 51
  const float K_PRESION_MPX = 1.0f / (GANANCIA_FLUJO * SENS_MPX_V_PA * 98.0665f);
  bool modoPresion = false;

  const float ALPHA_FILTRO  = 0.30f;
  const float ZONA_MUERTA_V = 0.002f;
  const float UMBRAL_FASE   = 0.2f;
  const float ALPHA_BASE    = 0.02f;
  const int   N_FIN_EVENTO  = 5;
  const float DT            = 0.010f; // 100 Hz nominal

  const int   N_PROM_FLUJO  = 16;
  const int   N_PROM_PIM    = 4;

  float vZeroFlujo;
  float vZeroPIM;
  float vFlujoFiltrado;
  float vPIMFiltrado;

  float volumen_L;
  int fase;
  bool enEvento;
  int muestrasSinFlujo;
  float presionBase;
  float volumenPendiente;
  uint32_t tInicio_us;
  uint32_t tAnterior_us;

  float leerV(int pin, int nProm) const;

  static const uint32_t PERIODO_US = 10000;     // 100 Hz (coincide con DT)
  hw_timer_t *timerMuestreo = nullptr;
  TaskHandle_t tareaMuestreoHandle = nullptr;
  volatile bool muestreando = false;
  volatile bool congelado = false;
  volatile int saltarPendiente = 0;
  volatile size_t nMuestras = 0;
  size_t capacidad = 0;
  MuestraEspiro ultimaMuestra = {0, 0, 0, 0, 0, 0, 0};

  static RespiratorySensors *instancia;
  static void ARDUINO_ISR_ATTR isrMuestreo();
  static void tareaMuestreo(void *arg);
  void tomarMuestraTimer();
};

#endif