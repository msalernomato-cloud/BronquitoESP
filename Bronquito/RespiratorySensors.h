#ifndef RESPIRATORY_SENSORS_H
#define RESPIRATORY_SENSORS_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include <Ticker.h>

struct MuestraEspiro {
  float tiempo_s;
  float vFlujo_V;
  float vPIM_V;
  float flujo_Lmin;
  float presion_cmH2O;
  float volumen_L;
  int fase;
};

// Pasabajos Butterworth de 2º orden, fc = 10 Hz, para fs = 100 Hz (forma directa II transpuesta).
// Coeficientes por transformación bilineal: K = tan(pi*fc/fs), norma = 1 / (1 + sqrt(2)*K + K^2).
// La información útil del flujo y la presión respiratoria está por debajo de unos 6,5 Hz.
struct Pasabajos10Hz {
  static constexpr float B0 = 0.067455f, B1 = 0.134911f, B2 = 0.067455f;
  static constexpr float A1 = -1.142980f, A2 = 0.412802f;
  float z1 = 0.0f, z2 = 0.0f;
  // Arranca "asentado" en x0 (sin transitorio): se llama con el valor de la tara
  void reiniciar(float x0) { z1 = x0 * (1.0f - B0); z2 = x0 * (B2 - A2); }
  float filtrar(float x) {
    float y = B0 * x + z1;
    z1 = B1 * x - A1 * y + z2;
    z2 = B2 * x - A2 * y;
    return y;
  }
};

// Resultado de una captura
enum ResultadoCaptura {
  CAPTURA_OK,
  CAPTURA_CANCELADA,     // tocaron "Cancelar"
  CAPTURA_SIN_MEMORIA,   // no hay memoria contigua para el buffer
  CAPTURA_DESBORDADA,    // captura continua: no se alcanzó a guardar y se perdieron muestras
  CAPTURA_ERROR_GUARDADO // captura continua: guardar() devolvió false (ej. falló la SD)
};

// Solo MIDE: lee el sensor, filtra, convierte a flujo o presión y guarda las muestras.
// No analiza nada (eso lo hacen AnalisisEspiro y AnalisisPIM con las muestras que entrega).
class RespiratorySensors {
public:
  RespiratorySensors(int pinFlujo = 36, int pinPIM = 39);

  void begin();          // configura el ADC
  void realizarTara();   // mide el cero con el paciente quieto (llamar DESPUÉS de dibujar la pantalla)

  // false = prueba sin PIM
  bool usaPIM() const { return USAR_PIM; }

  // Datos que necesita el análisis posterior
  float getVZeroFlujo() const { return vZeroFlujo; }      // cero de la tara (V)
  float getKEspiracion() const  { return K_ESPIRACION; }   // Q = K * sqrt(dV), aire saliendo
  float getKInspiracion() const { return K_INSPIRACION; }  // Q = K * sqrt(dV), aire entrando
  float periodoMuestreo() const { return DT; }            // 0,01 s

  // ---- Captura por interrupción de software (Ticker) ----
  // Igual que el ejemplo de clase con Ticker: InterrupcionSoftware.attach(periodo, funcion).
  // Cada 1 ms (1 kHz) el Ticker llama a alInterrumpir(), que hace UNA lectura de cada canal:
  //   1. Mediana de 3: con las dos lecturas anteriores. Borra los picos sueltos del ADC.
  //   2. Promedio de los últimos 20 ms (20 lecturas) = un ciclo entero de la red: anula los 50 Hz
  //      y sus armónicos, y hace de antialiasing antes de bajar a 100 Hz.
  //   3. Cada 10 lecturas (10 ms) sale una muestra: pasabajos de 10 Hz, flujo, presión y sentido.
  // El Ticker del ESP32 corre la función en la tarea de esp_timer (no dentro de una ISR), por eso
  // ahí se puede usar analogReadMilliVolts. La pantalla, el táctil y el serie siguen en el loop.
  //
  // capturar() mide durante 'duracionMs' y, mientras tanto:
  //   - imprime cada muestra por el monitor serie (CSV);
  //   - cada 'cadaN' muestras llama a mostrar(últimaMuestra). Escribir en la pantalla mete un pico en el
  //     ADC: mientras se ejecuta mostrar() y en las 3 muestras siguientes no se lee el ADC (se repite la
  //     última muestra con el tiempo actualizado);
  //   - cada 100 ms llama a cancelar(); si devuelve true, corta y descarta lo medido.
  ResultadoCaptura capturar(uint32_t duracionMs, size_t cadaN,
                            std::function<void(const MuestraEspiro &)> mostrar,
                            std::function<bool()> cancelar);

  // Igual que capturar(), pero SIN duración fija y sin juntar todo en RAM (para el entrenamiento):
  // las muestras van a un buffer circular chico y, en el loop de la captura, se le pasan de a bloques
  // a guardar(). Mide hasta que detener() devuelve true (se revisa cada 100 ms). La pantalla queda fija.
  //   CAPTURA_OK: el usuario detuvo y se guardó todo
  //   CAPTURA_DESBORDADA: guardar() tardó demasiado y se perdieron muestras (lo guardado sirve)
  //   CAPTURA_ERROR_GUARDADO: guardar() devolvió false
  ResultadoCaptura capturarContinua(std::function<bool(const MuestraEspiro *, size_t)> guardar,
                                    std::function<bool()> detener);
  // Pasa las muestras de la última captura a 'destino' (el sensor se queda sin ellas)
  void entregarMuestras(std::vector<MuestraEspiro> &destino);
  // Libera la memoria del buffer (si quedó algo)
  void liberarMemoria() { std::vector<MuestraEspiro>().swap(bufferMuestras); }

private:
  const int _pinFlujo;
  const int _pinPIM;

  // Configuración
  const bool USAR_PIM = true;

  // ---- Constantes de conversión (placa "PIM ESPIROMETRÍA", circuito final) ----------------
  // Las dos cadenas tienen la referencia en 1,65 V (U6B): sin presión, la salida queda cerca
  // de 1,65 V y la tara mide ese valor exacto. Por eso la señal puede bajar o subir.

  // Canal de flujo (GPIO36): MPX2010DX -> INA128 (U2) -> no inversor (U1A)
  //   INA128:      G = 1 + 50k / RG,  RG = R1 = 2,7k -> 19,52
  //   no inversor: G = 1 + R4 / R6 = 1 + 39k / 10k   -> 4,9
  //   Q [L/min] = 3,7848 * sqrt(dP [Pa])  (Venturi, Cd = 0,97)  y  dP = Vneto / (G * 1,25 uV/Pa)
  const float RG_INA_FLUJO_OHM = 2700.0f;
  const float G_INA_FLUJO      = 1.0f + 50000.0f / RG_INA_FLUJO_OHM;   // 19,52
  const float G_NOINV_FLUJO    = 1.0f + 39000.0f / 10000.0f;          // 4,9
  const float GANANCIA_FLUJO   = G_INA_FLUJO * G_NOINV_FLUJO;         // 95,64
  const float SENS_MPX_V_PA    = 1.25e-6f;                            // 1,25 mV/kPa alimentado a 5 V
  const float K_PA_A_LMIN      = 3.7848f;
  const float K_TEORICA        = K_PA_A_LMIN / sqrtf(GANANCIA_FLUJO * SENS_MPX_V_PA);   // ≈ 346,2 L/min/sqrt(V)
  // El venturi no es simétrico: cada sentido tiene su constante. La espiración usa la teórica.
  // La inspiración se calibró el 08/11/2026 con 3 personas x 4 maniobras (evals 48-59): K de cierre
  // = FVC / FIVC medida, mediana de 11 maniobras válidas = 1,67 (CV 12 %). Ver el protocolo en Docs.
  const float K_CIERRE_INSP    = 1.67f;
  const float K_ESPIRACION     = K_TEORICA;
  const float K_INSPIRACION    = K_CIERRE_INSP * K_TEORICA;   // ≈ 578 L/min/sqrt(V)

  // Canal de presión (GPIO39): PSA071040 -> INA128 (U3), salida SIN amplificar (MAN2, conector J7)
  //   INA128: G = 1 + 50k / RG,  RG = R8 = 3,9k -> 13,82
  //   (la salida amplificada, con el no inversor U1B x3,7 -> G = 51,14 y K = 113,88, queda en J3 por si
  //    hace falta más resolución: en ese caso usar GANANCIA_PIM = G_INA_PIM * 3,7f)
  //   P [cmH2O] = Vneto * 10,19716 / (G * 1,7511 mV/kPa)
  // Mide la PIM y, en la espirometría, dice el sentido del flujo. Con la placa actual la presión da
  // POSITIVA al inspirar y NEGATIVA al espirar (ver AnalisisEspiro::SIGNO_PRESION_ESPIRACION).
  const float RG_INA_PIM_OHM = 3900.0f;
  const float GANANCIA_PIM   = 1.0f + 50000.0f / RG_INA_PIM_OHM;      // 13,82
  const float SENS_PSA_V_KPA = 0.0017511f;                            // 1,7511 mV/kPa (hoja de datos)
  const float K_PIM          = 10.19716f / (GANANCIA_PIM * SENS_PSA_V_KPA);   // ≈ 421,35 cmH2O/V
  // La presión se guarda tal como la mide el sensor (positiva al inspirar). No cambiar este signo para
  // arreglar el sentido: eso se ajusta con AnalisisEspiro::SIGNO_PRESION_ESPIRACION.
  const float SIGNO_PRESION  = 1.0f;

  const float ZONA_MUERTA_V = 0.002f;
  const float UMBRAL_FASE   = 0.2f;
  const float ALPHA_BASE    = 0.02f;
  const int   N_FIN_EVENTO  = 5;
  const float DT            = 0.010f; // 100 Hz nominal

  const int   N_PROM_TARA   = 16;   // lecturas seguidas por punto en la tara (fuera del Ticker)

  float vZeroFlujo;
  float vZeroPIM;
  float vFlujoFiltrado;
  float vPIMFiltrado;
  Pasabajos10Hz filtroFlujo, filtroPIM;

  float volumen_L;
  int fase;
  bool enEvento;
  int muestrasSinFlujo;
  float presionBase;
  float volumenPendiente;
  uint32_t tInicio_us;
  uint32_t tAnterior_us;

  float leerV(int pin, int nProm) const;

  // ---- Muestreo por Ticker ----
  static const uint32_t PERIODO_LECTURA_MS = 1;    // una lectura de cada canal cada 1 ms (1 kHz)
  static const int LECTURAS_POR_MUESTRA    = 10;   // 10 lecturas = 10 ms -> muestras a 100 Hz (DT)
  static const int N_VENTANA_50HZ          = 20;   // 20 lecturas = 20 ms = un ciclo de 50 Hz
  Ticker InterrupcionSoftware;
  uint16_t ventanaFlujo[N_VENTANA_50HZ], ventanaPIM[N_VENTANA_50HZ];   // últimas lecturas (mV, ya con mediana)
  uint32_t sumaFlujo = 0, sumaPIM = 0;            // suma de la ventana (se actualiza en cada lectura)
  uint16_t previasFlujo[2], previasPIM[2];        // dos lecturas crudas anteriores (para la mediana de 3)
  int idxVentana = 0, lecturasDesdeMuestra = 0;
  volatile bool muestreando = false;
  volatile bool congelado = false;
  volatile int saltarPendiente = 0;
  volatile size_t nMuestras = 0;
  size_t capacidad = 0;
  MuestraEspiro ultimaMuestra = {0, 0, 0, 0, 0, 0, 0};

  // Captura continua: bufferMuestras se usa como buffer circular. nMuestras cuenta las muestras
  // escritas (no vuelve a cero) y nLeidas las que ya se pasaron a guardar(); la posición es % capacidad.
  static const size_t CAPACIDAD_CONTINUA = 512;   // ~5 s a 100 Hz de margen para la escritura en la SD
  static const size_t MUESTRAS_POR_BLOQUE = 48;
  volatile bool continua = false;
  volatile bool desbordada = false;
  volatile size_t nLeidas = 0;
  size_t sacarBloque(MuestraEspiro *destino, size_t max);

  // Buffer de la captura en curso (lo escribe la tarea de muestreo)
  std::vector<MuestraEspiro> bufferMuestras;

  void iniciarManiobra();
  MuestraEspiro procesarMuestra(float vFlujoBruto, float vPIMBruto);
  String obtenerLineaCSV(const MuestraEspiro &m) const;
  bool iniciarMuestreo(size_t maxMuestras);     // arranca una captura nueva (llama a iniciarManiobra)
  void detenerMuestreo();                       // para el Ticker y deja bufferMuestras con lo tomado
  void reiniciarVentanas();                     // llena la ventana de 20 ms con el cero de la tara

  static void alInterrumpir(RespiratorySensors *s);   // la llama el Ticker cada 1 ms
  void leerADC();                                     // una lectura por canal + mediana + ventana
  void tomarMuestra();                                // cada 10 lecturas: guarda una muestra
};

#endif