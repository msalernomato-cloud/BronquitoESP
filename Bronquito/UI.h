#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <vector>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "SystemState.h"

#define TOUCH_CS   33
#define TOUCH_CLK  32
#define TOUCH_DIN  25
#define TOUCH_DO   26

#define COLOR_FONDO       0xFFFF
#define BRONQUITO_YELLOW  0xF6AC
#define BRONQUITO_ORANGE  0xF3C4
#define BRONQUITO_CYAN    0x0559
#define BRONQUITO_BROWN   0x3944
#define COLOR_CAMPO_BG    0xF7BE
#define COLOR_TEXTO_GRIS  0x7BEF
#define COLOR_VERDE_OK    0x05E0
#define COLOR_ROJO_DEL    0xE880
#define POLLO_PICO_CLARO  0xED0D
#define POLLO_PICO_OSCURO 0xDBE7
#define POLLO_OJO_ARO     0xCC6B
#define POLLO_OJO         0x5143
#define COLOR_TARJETA_BG  0xF7DE

#define DOT_R 4
enum EstadoConexion { CON_NINGUNA, CON_SOLO_WIFI, CON_WIFI_Y_COMPU };

// Una métrica para mostrar en el resumen (ej: "FEV1", "2.35", "L")
struct MetricaItem {
  String etiqueta;
  String valor;
  String unidad;
};

inline MetricaItem metrica(const char *etiqueta, const String &valor, const char *unidad) {
  MetricaItem m;
  m.etiqueta = etiqueta;
  m.valor = valor;
  m.unidad = unidad;
  return m;
}

class BronquitoUI {
public:
  BronquitoUI();
  void begin();

  void drawLoginScreen();
  void drawPinScreen();
  void drawMainMenu();

  // Pantallas de Calibración
  void drawCalibMenu();
  void drawCalibPimInstructions();
  void drawCalibEspInstructions();
  // Espirometría: solo se muestra el tiempo (nada de flujo ni volumen mientras se mide)
  void drawCalibEspRunning(float tTotalSec);
  void updateCalibEspProgress(float tiempoSec, float tTotalSec);
  void setHintEsp(const String& texto);   // cambia solo el renglón de ayuda (escribe poco en la pantalla)

  // Recorrido guiado de calibración (PIM + Espirometría)
  void drawCalibPimRunning();
  void updateCalibPimProgress(float presionCmH2O, float tiempoSec);
  void setHintPim(const String& texto);   // renglón de ayuda de la PIM (ej. "Espere..." -> "Inspire con fuerza")
  // Sin delay: se puede llamar en pleno muestreo. true si tocaron "Cancelar" durante la captura
  bool cancelarTocado();
  // Pantalla de un paso (instrucciones o descanso). true = botón principal, false = Cancelar
  bool mostrarPasoCalibracion(const String& titulo, const String& subtitulo, const String *lineas, int n,
                              const String& textoBoton, uint16_t colorBoton);
  // Revisión de una maniobra. 1 = Aceptar, 0 = Repetir, -1 = Cancelar todo
  int  revisarManiobra(const String& titulo, const String& l1, const String& l2 = "");

  void showSyncFeedback();
  void showLoginFeedback();
  void showPinStatus(bool correct);
  void showErrorMessage(String title, String message);
  void setSynced(bool synced) { isSynced = synced; }
  void setDNI(String dni) { userDNI = dni; }
  void setEstadoConexion(EstadoConexion e) { estadoCon = e; }
  int  elegirConexion(const String& ssid);

  void mostrarMensaje(const String &l1, const String &l2 = "", uint16_t color = BRONQUITO_CYAN);
  int elegirRed(const String *nombres, const int *rssi, int n);
  String pedirTexto(const String &titulo, String inicial = "");
  String pedirCodigo(const String *lineas, int nLineas, const String &prefijo);
  bool pedirConfirmacion(const String& titulo, const String& l1, const String& l2 = "");
  String pedirPin(const String& titulo, const String& subtitulo = "");
  void mostrarEstadoConexion(const String& ssid, const String& ipCompu);
  void mostrarFrecuencia(int sesiones, const bool *dias, const String& vigenteDesde, bool recienActualizada);

  // Historial
  int  mostrarHistorial();   // 0 = Calibraciones, 1 = Entrenamientos, 2 = Enviar a la web, -1 = Volver al menú
  int  elegirCalibracion(const String *fechas, const String *tipos, int n);  // índice, o -1 = Volver
  // Resumen de una evaluación. Devuelve 0 = Volver, 1 = Graficar
  // "pie" = condiciones ambientales, en dos renglones separados por " | "
  int  mostrarResumenCalibracion(const String& titulo, const String& fecha, const MetricaItem *m, int n, const String& pie = "");
  // Gráfico x-y. Devuelve 0 = Volver, 1 = botón alternativo (si botonAlt no está vacío)
  int  mostrarGrafico(const String& titulo, const String& etiquetaX, const String& etiquetaY,
                      const float *x, const float *y, int n, const String& botonAlt = "");

  UIEvent checkTouch(SystemState currentState);

  String getPinEntered() const { return pinEntered; }
  void clearPin() { pinEntered = ""; }

private:
  TFT_eSPI tft;
  SPIClass touchSPI;
  XPT2046_Touchscreen touch;

  String pinEntered;
  String userDNI;
  bool lastTouchState;
  bool isSynced;
  EstadoConexion estadoCon;

  int dotX, dotY;
  bool hasDot;
  bool cancelArmado;   // true recién cuando se soltó la pantalla (evita tomar como Cancelar el toque de "Comenzar")

  void escribir(const String &s, int x, int y);
  void dibujarIndicadorConexion(int cx, int cy);
  void updatePinDisplay();
  void redrawArea(int x, int y, int w, int h, SystemState currentState);
  void clearTouchDot(SystemState currentState);
  void drawTouchDot(int x, int y);

  bool leerToque(int &x, int &y);
  void dibujarBoton(int x, int y, int w, int h, const String &texto, uint16_t color, int tam = 2);
  void dibujarCampoTexto(const String &texto);
  void dibujarTeclado(int capa);
};

#endif