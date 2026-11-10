#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <vector>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "Estados.h"

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

// Lo que muestra la pantalla de Configuración sobre el nivel de resistencia
struct InfoResistencia {
  int nivel = 0;                 // 1 a NIVELES_RESISTENCIA; 0 = todavía no hay
  int vigenciaDias = 0;
  String vence;                  // "AAAA-MM-DD"
  bool fijadoPorMedico = false;
  bool hayFechaHoy = false;      // false = el reloj no tiene hora: no se puede saber si venció
  int diasRestantes = 0;         // hasta "vence" (negativo = ya venció); vale solo si hayFechaHoy
};
const int NIVELES_RESISTENCIA = 5;

class BronquitoUI {
public:
  BronquitoUI();
  void begin();

  void drawLoginScreen();
  void drawPinScreen();
  void drawMainMenu();

  // Pantallas de Calibración
  void drawCalibMenu();
  // Espirometría: solo se muestra el tiempo (nada de flujo ni volumen mientras se mide)
  void drawCalibEspRunning(float tTotalSec);
  void updateCalibEspProgress(float tiempoSec, float tTotalSec);
  void setHintEsp(const String& texto);   // cambia solo el renglón de ayuda (escribe poco en la pantalla)

  // Recorrido guiado de calibración (PIM + Espirometría)
  void drawCalibPimRunning(float tTotalSec);
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
  // Cartel de error sobre la pantalla actual, con botón OK: espera a que lo toquen para volver
  void showErrorMessage(String title, String message);
  void mostrarError(const String &titulo, const String &detalle = "");   // lo mismo (nombre en castellano)
  void setSynced(bool synced) { isSynced = synced; }
  void setUsuario(const String &texto) { textoUsuario = texto; }   // lo que se ve en el login
  void setEstadoConexion(EstadoConexion e) { estadoCon = e; }
  int  elegirConexion(const String& ssid);

  void mostrarMensaje(const String &l1, const String &l2 = "", uint16_t color = BRONQUITO_CYAN);
  // guardadas[i] = true muestra "guardada" (no va a pedir la contraseña). Puede ser nullptr
  int elegirRed(const String *nombres, const int *rssi, int n, const bool *guardadas = nullptr);
  String pedirTexto(const String &titulo, String inicial = "");
  String pedirCodigo(const String *lineas, int nLineas, const String &prefijo);
  // Pregunta con dos botones. true = botón derecho (textoSi), false = izquierdo (textoNo)
  bool pedirConfirmacion(const String& titulo, const String& l1, const String& l2 = "",
                         const String& textoNo = "Rechazar", const String& textoSi = "Aceptar",
                         uint16_t colorNo = COLOR_ROJO_DEL, uint16_t colorSi = COLOR_VERDE_OK);
  String pedirPin(const String& titulo, const String& subtitulo = "");
  void mostrarEstadoConexion(const String& ssid, const String& ipCompu);
  // Pantalla de Configuración: frecuencia de entrenamiento (arriba) y nivel de resistencia (abajo)
  void mostrarConfiguracion(int sesiones, const bool *dias, const String& vigenteDesde, bool recienActualizada,
                            const InfoResistencia &res);

  // Historial
  int  mostrarHistorial();   // 0 = Calibraciones, 1 = Entrenamientos, 2 = Enviar a la web, -1 = Volver al menú
  // Lista paginada. Devuelve el índice elegido, o -1 = Volver. 'pagina' entra con la página a mostrar
  // y sale con la última que se vio (así, al volver de una evaluación, se sigue en la misma página)
  int  elegirCalibracion(const String *fechas, const String *tipos, int n, int &pagina);
  // Resumen de una evaluación. Devuelve 0 = Volver, 1 = Graficar
  // "pie" = condiciones ambientales, en dos renglones separados por " | "
  int  mostrarResumenCalibracion(const String& titulo, const String& fecha, const MetricaItem *m, int n, const String& pie = "");
  // Gráfico x-y. Devuelve 0 = Volver, 1 = botón alternativo (si botonAlt no está vacío)
  // flujo: si se pasa, cada tramo se pinta según su signo (espiración / inspiración) y se agrega la leyenda
  int  mostrarGrafico(const String& titulo, const String& etiquetaX, const String& etiquetaY,
                      const float *x, const float *y, int n, const String& botonAlt = "",
                      const float *flujo = nullptr);

  EventoPantalla detectarEvento(EstadoSistema estadoActual);

  String getPinEntered() const { return pinEntered; }
  void clearPin() { pinEntered = ""; }

private:
  TFT_eSPI tft;
  SPIClass touchSPI;
  XPT2046_Touchscreen touch;

  String pinEntered;
  String textoUsuario;
  bool lastTouchState;
  bool isSynced;
  EstadoConexion estadoCon;

  int dotX, dotY;
  bool hasDot;
  bool cancelArmado;   // true recién cuando se soltó la pantalla (evita tomar como Cancelar el toque de "Comenzar")

  void escribir(const String &s, int x, int y);
  void dibujarIndicadorConexion(int cx, int cy);
  void updatePinDisplay();
  void redrawArea(int x, int y, int w, int h, EstadoSistema estadoActual);
  void clearTouchDot(EstadoSistema estadoActual);
  void drawTouchDot(int x, int y);

  bool leerToque(int &x, int &y);
  void dibujarBoton(int x, int y, int w, int h, const String &texto, uint16_t color, int tam = 2);
  void dibujarCampoTexto(const String &texto);
  void dibujarTeclado(int capa);
};

#endif