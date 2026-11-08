#include "UI.h"

static String paraPantalla(const String &s) {
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 0x80) { out += (char)c; continue; }
    if ((c == 0xC2 || c == 0xC3) && i + 1 < s.length()) {
      uint16_t u = ((c & 0x1F) << 6) | ((uint8_t)s[++i] & 0x3F);
      uint8_t g;
      switch (u) {
        case 0xE1: g = 0xA0; break;   // á
        case 0xE9: g = 0x82; break;   // é
        case 0xED: g = 0xA1; break;   // í
        case 0xF3: g = 0xA2; break;   // ó
        case 0xFA: g = 0xA3; break;   // ú
        case 0xF1: g = 0xA4; break;   // ñ
        case 0xD1: g = 0xA5; break;   // Ñ
        case 0xFC: g = 0x81; break;   // ü
        case 0xDC: g = 0x9A; break;   // Ü
        case 0xC9: g = 0x90; break;   // É
        case 0xBF: g = 0xA8; break;   // ¿
        case 0xA1: g = 0xAD; break;   // ¡
        case 0xC1: g = 'A';  break;
        case 0xCD: g = 'I';  break;
        case 0xD3: g = 'O';  break;
        case 0xDA: g = 'U';  break;
        default:   g = '?';  break;
      }
      out += (char)g;
      continue;
    }
    while (i + 1 < s.length() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) i++;
    out += '?';
  }
  return out;
}

void BronquitoUI::escribir(const String &s, int x, int y) {
  tft.drawString(paraPantalla(s), x, y);
}

BronquitoUI::BronquitoUI()
  : tft(), touchSPI(HSPI), touch(TOUCH_CS), pinEntered(""), textoUsuario("Sin sincronizar"), lastTouchState(false), isSynced(false), estadoCon(CON_NINGUNA), dotX(0), dotY(0), hasDot(false), cancelArmado(false) {}

void BronquitoUI::begin() {
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  delay(50);
  digitalWrite(4, HIGH);
  delay(100);

  tft.init();
  tft.setRotation(1);
  tft.setAttribute(UTF8_SWITCH, false);
  tft.setAttribute(CP437_SWITCH, true);

  touchSPI.begin(TOUCH_CLK, TOUCH_DO, TOUCH_DIN, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(3);
}

void BronquitoUI::redrawArea(int x, int y, int w, int h, EstadoSistema estadoActual) {
  tft.setViewport(x, y, w, h, false);
  if (estadoActual == ESTADO_INICIO_SESION) {
    drawLoginScreen();
  } else if (estadoActual == ESTADO_PIN) {
    drawPinScreen();
  } else if (estadoActual == ESTADO_MENU_PRINCIPAL) {
    drawMainMenu();
  }
  tft.resetViewport();
}

void BronquitoUI::clearTouchDot(EstadoSistema estadoActual) {
  if (hasDot) {
    redrawArea(dotX - DOT_R - 1, dotY - DOT_R - 1, DOT_R * 2 + 3, DOT_R * 2 + 3, estadoActual);
    hasDot = false;
  }
}

void BronquitoUI::drawTouchDot(int x, int y) {
  x = constrain(x, DOT_R, 479 - DOT_R);
  y = constrain(y, DOT_R, 319 - DOT_R);
  tft.fillCircle(x, y, DOT_R, TFT_RED);
  dotX = x;
  dotY = y;
  hasDot = true;
}

void BronquitoUI::drawLoginScreen() {
  tft.fillScreen(COLOR_FONDO);

  tft.fillEllipse(74, 258, 89, 96, BRONQUITO_YELLOW);
  tft.fillEllipse(-5, 197, 53, 43, BRONQUITO_YELLOW);
  tft.fillRect(0, 197, 74, 123, BRONQUITO_YELLOW);
  tft.fillRect(10, 120, 74, 72, BRONQUITO_YELLOW);
  tft.fillEllipse(84, 120, 74, 69, BRONQUITO_YELLOW);
  tft.fillCircle(110, 111, 14, POLLO_OJO_ARO);
  tft.fillCircle(110, 111, 11, POLLO_OJO);
  tft.fillCircle(113, 107, 4, COLOR_FONDO);
  tft.fillTriangle(155, 101, 190, 128, 157, 128, POLLO_PICO_CLARO);
  tft.fillTriangle(157, 127, 190, 128, 155, 141, POLLO_PICO_OSCURO);

  dibujarBoton(15, 265, 120, 40, "Conectar", BRONQUITO_CYAN);
  tft.drawRoundRect(15, 265, 120, 40, 6, COLOR_FONDO);
  dibujarIndicadorConexion(163, 285);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(4);
  escribir("BRONQUITO", 332, 50);

  tft.drawRoundRect(200, 125, 265, 180, 12, BRONQUITO_CYAN);
  tft.drawRoundRect(201, 126, 263, 178, 11, BRONQUITO_CYAN);

  tft.setTextSize(2);
  escribir("Iniciar sesión", 332, 142);

  tft.fillRoundRect(215, 175, 235, 38, 6, COLOR_CAMPO_BG);
  tft.drawRoundRect(215, 175, 235, 38, 6, BRONQUITO_CYAN);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_CAMPO_BG);
  tft.setTextSize(2);
  
  String texto = textoUsuario;
  if (texto.length() > 18) texto = texto.substring(0, 17) + ".";
  escribir(texto, 332, 186);

  tft.fillRoundRect(215, 230, 235, 48, 8, BRONQUITO_CYAN);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  escribir("Ingresar", 332, 245);

  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::updatePinDisplay() {
  int boxW = 30, boxH = 35, gap = 12, boxY = 52;
  int totalW = 4 * boxW + 3 * gap;
  int startX = 240 - totalW / 2;

  for (int i = 0; i < 4; i++) {
    int currentX = startX + i * (boxW + gap);
    tft.fillRoundRect(currentX, boxY, boxW, boxH, 5, COLOR_CAMPO_BG);
    tft.drawRoundRect(currentX, boxY, boxW, boxH, 5, BRONQUITO_CYAN);

    if (i < pinEntered.length()) {
      tft.fillCircle(currentX + boxW / 2, boxY + boxH / 2, 7, BRONQUITO_CYAN);
    }
  }
}

void BronquitoUI::drawPinScreen() {
  tft.fillScreen(COLOR_FONDO);

  tft.drawRoundRect(12, 12, 44, 36, 6, BRONQUITO_ORANGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_ORANGE, COLOR_FONDO);
  tft.setTextSize(3);
  escribir("<", 34, 31);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(2);
  escribir("Ingresá tu PIN", 240, 18);

  updatePinDisplay();

  const char* keys[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"C", "0", "OK"}
  };

  tft.setTextDatum(MC_DATUM);
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int x = 135 + col * 75;
      int y = 102 + row * 52;

      uint16_t btnColor = BRONQUITO_CYAN;
      if (row == 3 && col == 0) btnColor = COLOR_ROJO_DEL;
      if (row == 3 && col == 2) btnColor = COLOR_VERDE_OK;

      tft.fillRoundRect(x, y, 60, 42, 8, btnColor);
      tft.setTextColor(COLOR_FONDO, btnColor);
      tft.setTextSize((row == 3 && col == 2) ? 2 : 3);
      escribir(keys[row][col], x + 30, y + 22);
    }
  }

  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::drawMainMenu() {
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Menú principal", 15, 23);

  tft.fillRoundRect(320, 6, 148, 33, 6, BRONQUITO_ORANGE);
  tft.drawRoundRect(320, 6, 148, 33, 6, COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_ORANGE);
  tft.setTextSize(1);
  escribir("Cerrar sesión", 394, 22);

  dibujarIndicadorConexion(203, 23);
  tft.fillRoundRect(224, 6, 90, 33, 6, COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(1);
  escribir("Conectar", 269, 22);

  // 1. Botón Entrenamiento
  tft.fillRoundRect(25, 60, 205, 110, 10, COLOR_TARJETA_BG);
  tft.drawRoundRect(25, 60, 205, 110, 10, BRONQUITO_CYAN);
  tft.drawRoundRect(26, 61, 203, 108, 9, BRONQUITO_CYAN);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Entrenamiento", 127, 115);

  // 2. Botón Calibración
  tft.fillRoundRect(250, 60, 205, 110, 10, COLOR_TARJETA_BG);
  tft.drawRoundRect(250, 60, 205, 110, 10, BRONQUITO_CYAN);
  tft.drawRoundRect(251, 61, 203, 108, 9, BRONQUITO_CYAN);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Calibración", 352, 115);

  // 3. Botón Configuración
  tft.fillRoundRect(25, 185, 205, 110, 10, COLOR_TARJETA_BG);
  tft.drawRoundRect(25, 185, 205, 110, 10, BRONQUITO_CYAN);
  tft.drawRoundRect(26, 186, 203, 108, 9, BRONQUITO_CYAN);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Configuración", 127, 240);

  // 4. Botón Historial
  tft.fillRoundRect(250, 185, 205, 110, 10, COLOR_TARJETA_BG);
  tft.drawRoundRect(250, 185, 205, 110, 10, BRONQUITO_CYAN);
  tft.drawRoundRect(251, 186, 203, 108, 9, BRONQUITO_CYAN);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Historial", 352, 240);

  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::drawCalibMenu() {
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Calibración de Sensores", 15, 23);

  // Botón único: recorrido guiado PIM + Espirometría
  tft.fillRoundRect(25, 65, 430, 135, 12, COLOR_TARJETA_BG);
  tft.drawRoundRect(25, 65, 430, 135, 12, BRONQUITO_CYAN);
  tft.drawRoundRect(26, 66, 428, 133, 11, BRONQUITO_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(3);
  escribir("Calibrar", 240, 105);
  tft.setTextSize(2);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_TARJETA_BG);
  escribir("PIM + Espirometría", 240, 143);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_TARJETA_BG);
  escribir("Dos maniobras guiadas, una a continuación de la otra", 240, 175);

  dibujarBoton(90, 245, 300, 50, "Volver", COLOR_TEXTO_GRIS);
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::drawCalibEspRunning(float tTotalSec) {
  cancelArmado = false;
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 40, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Midiendo Espirometría...", 15, 20);

  tft.fillRoundRect(30, 55, 420, 175, 10, COLOR_CAMPO_BG);
  tft.drawRoundRect(30, 55, 420, 175, 10, BRONQUITO_CYAN);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_CAMPO_BG);
  // Pantalla fija durante la medición (no se actualiza: ver FlujoCalibracion::capturarManiobra)
  escribir("La medición dura " + String((int)lroundf(tTotalSec)) + " segundos", 240, 100);
  tft.setTextPadding(400);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
  escribir("Espere... no respire", 240, 165);     // se cambia por "Exhale..." recién terminada la tara
  tft.setTextPadding(0);

  dibujarBoton(120, 245, 240, 50, "Cancelar", COLOR_ROJO_DEL);
  tft.setTextDatum(TL_DATUM);
}


void BronquitoUI::dibujarIndicadorConexion(int cx, int cy) {
  uint16_t color = estadoCon == CON_WIFI_Y_COMPU ? COLOR_VERDE_OK
                 : estadoCon == CON_SOLO_WIFI    ? 0xFE60
                                                 : COLOR_ROJO_DEL;
  tft.fillCircle(cx, cy, 14, COLOR_FONDO);
  tft.fillCircle(cx, cy, 11, color);
  tft.drawCircle(cx, cy, 14, BRONQUITO_BROWN);
}

void BronquitoUI::showSyncFeedback() {
  tft.fillRoundRect(15, 265, 120, 40, 6, BRONQUITO_ORANGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_ORANGE);
  tft.setTextSize(1);
  escribir("Sincronizando...", 75, 285);
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::showLoginFeedback() {
  tft.fillRoundRect(215, 230, 235, 48, 8, BRONQUITO_ORANGE);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_ORANGE);
  tft.setTextSize(2);
  escribir("Ingresando...", 332, 245);
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::showPinStatus(bool correct) {
  tft.fillRect(120, 48, 240, 45, COLOR_FONDO);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);

  if (correct) {
    tft.setTextColor(COLOR_VERDE_OK, COLOR_FONDO);
    escribir("¡PIN correcto!", 240, 62);
  } else {
    tft.setTextColor(COLOR_ROJO_DEL, COLOR_FONDO);
    escribir("PIN incorrecto", 240, 62);
  }
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::showErrorMessage(String title, String message) {
  tft.fillRoundRect(60, 80, 360, 160, 10, COLOR_FONDO);
  tft.drawRoundRect(60, 80, 360, 160, 10, COLOR_ROJO_DEL);
  tft.drawRoundRect(61, 81, 358, 158, 9, COLOR_ROJO_DEL);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_ROJO_DEL, COLOR_FONDO);
  tft.setTextSize(2);
  escribir(title, 240, 100);

  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(2);
  escribir(message, 240, 145);

  tft.setTextDatum(TL_DATUM);
}

EventoPantalla BronquitoUI::detectarEvento(EstadoSistema estadoActual) {
  bool currentTouchState = touch.touched();
  EventoPantalla eventoDetectado = EVENTO_NINGUNO;

  if (currentTouchState && !lastTouchState) {
    TS_Point p = touch.getPoint();

    int x = map(p.x, 300, 3800, 0, 480);
    int y = map(p.y, 300, 3800, 0, 320);
    x = constrain(x, 0, 480);
    y = constrain(y, 0, 320);

    clearTouchDot(estadoActual);

    if (estadoActual == ESTADO_INICIO_SESION) {
      if (x >= 10 && x <= 140 && y >= 255 && y <= 315) {
        eventoDetectado = EVENTO_CONECTAR;
      } else if (x >= 143 && x <= 190 && y >= 260 && y <= 312) {
        eventoDetectado = EVENTO_VER_CONEXION;
      } else if (x >= 200 && x <= 460 && y >= 210 && y <= 290) {
        eventoDetectado = EVENTO_INGRESAR;
      }
    } else if (estadoActual == ESTADO_PIN) {
      if (x >= 0 && x <= 70 && y >= 0 && y <= 60) {
        eventoDetectado = EVENTO_IR_AL_INICIO;
      } else {
        for (int row = 0; row < 4; row++) {
          for (int col = 0; col < 3; col++) {
            int btnX = 135 + col * 75;
            int btnY = 102 + row * 52;

            if (x >= (btnX - 6) && x <= (btnX + 66) && y >= (btnY - 5) && y <= (btnY + 47)) {
              if (row < 3) {
                int num = (row * 3) + (col + 1);
                if (pinEntered.length() < 4) {
                  pinEntered += String(num);
                  updatePinDisplay();
                }
              } else if (col == 0) {
                if (pinEntered.length() > 0) {
                  pinEntered.remove(pinEntered.length() - 1);
                  updatePinDisplay();
                }
              } else if (col == 1) {
                if (pinEntered.length() < 4) {
                  pinEntered += "0";
                  updatePinDisplay();
                }
              } else if (col == 2) {
                eventoDetectado = EVENTO_PIN_INGRESADO;
              }
            }
          }
        }
      }
    } else if (estadoActual == ESTADO_MENU_PRINCIPAL) {
      if (x >= 186 && x <= 220 && y >= 0 && y <= 45) {
        eventoDetectado = EVENTO_VER_CONEXION;
      } else if (x >= 221 && x <= 316 && y >= 0 && y <= 45) {
        eventoDetectado = EVENTO_CONECTAR;
      } else if (x >= 318 && x <= 475 && y >= 0 && y <= 45) {
        eventoDetectado = EVENTO_IR_AL_INICIO;
      } else if (x >= 25 && x <= 230 && y >= 60 && y <= 170) {
        eventoDetectado = EVENTO_ENTRENAMIENTO;
      } else if (x >= 250 && x <= 455 && y >= 60 && y <= 170) {
        eventoDetectado = EVENTO_CALIBRACION;
      } else if (x >= 25 && x <= 230 && y >= 185 && y <= 295) {
        eventoDetectado = EVENTO_CONFIGURACION;
      } else if (x >= 250 && x <= 455 && y >= 185 && y <= 295) {
        eventoDetectado = EVENTO_HISTORIAL;
      }
    } else if (estadoActual == ESTADO_MENU_CALIBRACION) {
      if (x >= 25 && x <= 455 && y >= 65 && y <= 200) {
        eventoDetectado = EVENTO_CALIBRAR;
      } else if (x >= 90 && x <= 390 && y >= 245 && y <= 295) {
        eventoDetectado = EVENTO_VOLVER;
      }
    }
    drawTouchDot(x, y);
  }

  lastTouchState = currentTouchState;
  delay(15);
  return eventoDetectado;
}

static bool dentro(int px, int py, int x, int y, int w, int h) {
  return px >= x && px <= x + w && py >= y && py <= y + h;
}

static const char *CAPAS[3][4] = {
  {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
  {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
  {"!@#$%^&*()", "-_=+[]{}|\\", ":;\"',/?<>", "~`.,?!/"}
};
static const int KW = 44, KH = 40, PASO_X = 46;
static const int FILA_Y0 = 80, FILA_PASO = 44;
static const int FILA_X[4] = {10, 10, 33, 74};

bool BronquitoUI::leerToque(int &x, int &y) {
  bool ahora = touch.touched();
  bool nuevo = ahora && !lastTouchState;
  lastTouchState = ahora;
  if (nuevo) {
    TS_Point p = touch.getPoint();
    x = constrain(map(p.x, 300, 3800, 0, 480), 0, 479);
    y = constrain(map(p.y, 300, 3800, 0, 320), 0, 319);
  }
  delay(15);
  return nuevo;
}

void BronquitoUI::dibujarBoton(int x, int y, int w, int h, const String &texto, uint16_t color, int tam) {
  tft.fillRoundRect(x, y, w, h, 6, color);
  tft.setTextColor(COLOR_FONDO, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(tam);
  escribir(texto, x + w / 2, y + h / 2);
}

void BronquitoUI::dibujarCampoTexto(const String &texto) {
  tft.fillRoundRect(10, 34, 460, 38, 6, COLOR_CAMPO_BG);
  tft.drawRoundRect(10, 34, 460, 38, 6, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
  tft.setTextSize(2);
  String vista = texto;
  if (vista.length() > 36) vista = vista.substring(vista.length() - 36);
  escribir(vista + "_", 18, 53);
}

void BronquitoUI::dibujarTeclado(int capa) {
  tft.fillRect(0, FILA_Y0 - 2, 480, 240, COLOR_FONDO);
  for (int r = 0; r < 4; r++) {
    const char *fila = CAPAS[capa][r];
    for (int i = 0; fila[i]; i++) {
      dibujarBoton(FILA_X[r] + i * PASO_X, FILA_Y0 + r * FILA_PASO, KW, KH, String(fila[i]), BRONQUITO_BROWN);
    }
  }
  int y3 = FILA_Y0 + 3 * FILA_PASO, y4 = FILA_Y0 + 4 * FILA_PASO;
  dibujarBoton(10, y3, 60, KH, "Aa", capa == 1 ? BRONQUITO_ORANGE : COLOR_TEXTO_GRIS);
  dibujarBoton(400, y3, 70, KH, "<-", COLOR_ROJO_DEL);
  dibujarBoton(10, y4, 70, KH, capa == 2 ? "abc" : "123", COLOR_TEXTO_GRIS);
  dibujarBoton(84, y4, 250, KH, "espacio", BRONQUITO_BROWN, 1);
  dibujarBoton(338, y4, 60, KH, ".", BRONQUITO_BROWN);
  dibujarBoton(402, y4, 68, KH, "OK", COLOR_VERDE_OK);
}

String BronquitoUI::pedirTexto(const String &titulo, String texto) {
  hasDot = false;
  int capa = 0;

  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(2);
  escribir(titulo.substring(0, 26), 10, 8);
  dibujarBoton(380, 4, 90, 28, "Volver", COLOR_TEXTO_GRIS, 1);
  dibujarCampoTexto(texto);
  dibujarTeclado(capa);

  int y3 = FILA_Y0 + 3 * FILA_PASO, y4 = FILA_Y0 + 4 * FILA_PASO;

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    int capaAntes = capa;

    if (dentro(x, y, 380, 4, 90, 28)) { tft.setTextDatum(TL_DATUM); return ""; }

    for (int r = 0; r < 4; r++) {
      if (!dentro(x, y, 0, FILA_Y0 + r * FILA_PASO, 480, KH)) continue;
      const char *fila = CAPAS[capa][r];
      for (int i = 0; fila[i]; i++) {
        if (dentro(x, y, FILA_X[r] + i * PASO_X, 0, KW, 480) && texto.length() < 63) {
          texto += fila[i];
          if (capa == 1) capa = 0;
        }
      }
    }
    if (dentro(x, y, 10, y3, 60, KH))        capa = (capa == 1) ? 0 : 1;
    else if (dentro(x, y, 400, y3, 70, KH))  { if (texto.length()) texto.remove(texto.length() - 1); }
    else if (dentro(x, y, 10, y4, 70, KH))   capa = (capa == 2) ? 0 : 2;
    else if (dentro(x, y, 84, y4, 250, KH))  { if (texto.length() < 63) texto += ' '; }
    else if (dentro(x, y, 338, y4, 60, KH))  { if (texto.length() < 63) texto += '.'; }
    else if (dentro(x, y, 402, y4, 68, KH))  {
      if (texto.length() > 0) { tft.setTextDatum(TL_DATUM); return texto; }
    }

    dibujarCampoTexto(texto);
    if (capa != capaAntes) dibujarTeclado(capa);
  }
}

void BronquitoUI::mostrarMensaje(const String &l1, const String &l2, uint16_t color) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(color, COLOR_FONDO);
  escribir(l1, 240, 130);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
  escribir(l2, 240, 170);
  tft.setTextDatum(TL_DATUM);
}

int BronquitoUI::elegirRed(const String *nombres, const int *rssi, int n) {
  hasDot = false;
  if (n > 6) n = 6;

  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(2);
  escribir("Elegí tu red Wi-Fi", 10, 8);

  tft.setTextDatum(ML_DATUM);
  for (int i = 0; i < n; i++) {
    int y = 36 + i * 40;
    tft.fillRoundRect(10, y, 460, 36, 6, COLOR_CAMPO_BG);
    tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
    tft.setTextSize(2);
    escribir(nombres[i].substring(0, 28), 20, y + 18);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_CAMPO_BG);
    escribir(String(rssi[i]) + " dBm", 400, y + 18);
  }
  if (n == 0) {
    tft.setTextSize(2);
    tft.setTextColor(COLOR_ROJO_DEL, COLOR_FONDO);
    escribir("No encontré redes", 20, 100);
  }
  dibujarBoton(10, 282, 150, 34, "Actualizar", BRONQUITO_CYAN);
  dibujarBoton(165, 282, 150, 34, "Otra red", COLOR_TEXTO_GRIS);
  dibujarBoton(320, 282, 150, 34, "Volver", COLOR_ROJO_DEL);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    for (int i = 0; i < n; i++)
      if (dentro(x, y, 10, 36 + i * 40, 460, 36)) return i;
    if (dentro(x, y, 10, 282, 150, 34))  return -1;
    if (dentro(x, y, 165, 282, 150, 34)) return -2;
    if (dentro(x, y, 320, 282, 150, 34)) return -3;
  }
}

String BronquitoUI::pedirCodigo(const String *lineas, int nLineas, const String &prefijo) {
  hasDot = false;
  String digitos = "";

  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  tft.setTextSize(2);
  for (int i = 0; i < nLineas && i < 3; i++) escribir(lineas[i], 10, 8 + i * 24);

  const char *teclas[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"<-", "0", "OK"}};
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      uint16_t color = BRONQUITO_BROWN;
      if (r == 3 && c == 0) color = COLOR_ROJO_DEL;
      if (r == 3 && c == 2) color = COLOR_VERDE_OK;
      dibujarBoton(95 + c * 100, 134 + r * 44, 90, 40, teclas[r][c], color);
    }
  }
  dibujarBoton(10, 266, 75, 40, "Volver", COLOR_TEXTO_GRIS, 1);

  auto mostrarCampo = [&]() {
    tft.fillRoundRect(10, 84, 460, 40, 6, COLOR_CAMPO_BG);
    tft.drawRoundRect(10, 84, 460, 40, 6, BRONQUITO_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
    tft.setTextSize(3);
    escribir(prefijo + digitos + "_", 240, 104);
  };
  mostrarCampo();

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;

    if (dentro(x, y, 10, 266, 75, 40)) {
      tft.setTextDatum(TL_DATUM);
      return "";
    }
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 3; c++) {
        if (!dentro(x, y, 95 + c * 100, 134 + r * 44, 90, 40)) continue;
        if (r < 3 || c == 1) {
          if (digitos.length() < 3) digitos += teclas[r][c];
        } else if (c == 0) {
          if (digitos.length() > 0) digitos.remove(digitos.length() - 1);
        } else if (digitos.length() > 0) {
          tft.setTextDatum(TL_DATUM);
          return digitos;
        }
      }
    }
    mostrarCampo();
  }
}

int BronquitoUI::elegirConexion(const String& ssid) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  tft.setTextSize(2);
  escribir("Conectado a:", 240, 22);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(ssid.length() <= 14 ? 3 : 2);
  escribir(ssid.substring(0, 28), 240, 56);

  dibujarBoton(90, 90,  300, 46, "Sincronizar",   COLOR_VERDE_OK);
  dibujarBoton(90, 146, 300, 46, "Cambiar red Wi-Fi",   BRONQUITO_CYAN);
  dibujarBoton(90, 202, 300, 46, "Cambiar computadora", BRONQUITO_ORANGE);
  dibujarBoton(90, 258, 300, 46, "Volver",        COLOR_TEXTO_GRIS);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 90, 90,  300, 46)) return 2;
    if (dentro(x, y, 90, 146, 300, 46)) return 0;
    if (dentro(x, y, 90, 202, 300, 46)) return 1;
    if (dentro(x, y, 90, 258, 300, 46)) return -1;
  }
}

bool BronquitoUI::pedirConfirmacion(const String &titulo, const String &l1, const String &l2,
                                    const String &textoNo, const String &textoSi, uint16_t colorNo, uint16_t colorSi) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  escribir(titulo.substring(0, 30), 240, 50);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  escribir(l1.substring(0, 36), 240, 115);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
  escribir(l2.substring(0, 36), 240, 150);
  dibujarBoton(40, 220, 190, 60, textoNo, colorNo);
  dibujarBoton(250, 220, 190, 60, textoSi, colorSi);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 40, 220, 190, 60))  return false;
    if (dentro(x, y, 250, 220, 190, 60)) return true;
  }
}

String BronquitoUI::pedirPin(const String &titulo, const String &subtitulo) {
  hasDot = false;
  String pin = "";

  tft.fillScreen(COLOR_FONDO);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  escribir(titulo.substring(0, 36), 10, 8);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  escribir(subtitulo.substring(0, 36), 10, 36);

  const char *teclas[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"<-", "0", "OK"}};
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      uint16_t color = BRONQUITO_BROWN;
      if (r == 3 && c == 0) color = COLOR_ROJO_DEL;
      if (r == 3 && c == 2) color = COLOR_VERDE_OK;
      dibujarBoton(95 + c * 100, 134 + r * 44, 90, 40, teclas[r][c], color);
    }
  }
  dibujarBoton(10, 266, 75, 40, "Volver", COLOR_TEXTO_GRIS, 1);

  auto mostrarCampo = [&]() {
    String estrellas = "";
    for (unsigned int i = 0; i < pin.length(); i++) estrellas += "*";
    tft.fillRoundRect(10, 84, 460, 40, 6, COLOR_CAMPO_BG);
    tft.drawRoundRect(10, 84, 460, 40, 6, BRONQUITO_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
    tft.setTextSize(3);
    escribir(estrellas + "_", 240, 104);
  };
  mostrarCampo();

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;

    if (dentro(x, y, 10, 266, 75, 40)) { tft.setTextDatum(TL_DATUM); return ""; }
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 3; c++) {
        if (!dentro(x, y, 95 + c * 100, 134 + r * 44, 90, 40)) continue;
        if (r < 3 || c == 1) {
          if (pin.length() < 4) pin += teclas[r][c];
        } else if (c == 0) {
          if (pin.length() > 0) pin.remove(pin.length() - 1);
        } else if (pin.length() == 4) {
          tft.setTextDatum(TL_DATUM);
          return pin;
        }
      }
    }
    mostrarCampo();
  }
}

void BronquitoUI::mostrarFrecuencia(int sesiones, const bool *dias, const String &vigenteDesde, bool recienActualizada) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Configuración", 15, 23);

  tft.setTextDatum(MC_DATUM);
  if (sesiones <= 0) {
    tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
    escribir("Todavía no hay frecuencia", 240, 125);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
    escribir("Pedile a tu médico que la cargue", 240, 165);
  } else {
    tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
    escribir("Frecuencia recomendada", 240, 70);
    tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
    tft.setTextSize(3);
    escribir(String(sesiones) + (sesiones == 1 ? " vez" : " veces") + " por semana", 240, 108);

    static const char *DIAS[7] = {"Lu", "Ma", "Mi", "Ju", "Vi", "Sa", "Do"};
    tft.setTextSize(2);
    for (int i = 0; i < 7; i++) {
      int x = 35 + i * 60;
      uint16_t fondo = dias[i] ? BRONQUITO_CYAN : COLOR_CAMPO_BG;
      tft.fillRoundRect(x, 138, 50, 50, 8, fondo);
      tft.setTextColor(dias[i] ? COLOR_FONDO : COLOR_TEXTO_GRIS, fondo);
      escribir(DIAS[i], x + 25, 163);
    }

    String fecha = vigenteDesde;
    if (fecha.length() == 10)
      fecha = fecha.substring(8, 10) + "/" + fecha.substring(5, 7) + "/" + fecha.substring(0, 4);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
    escribir("Desde el " + fecha, 240, 213);

    tft.setTextSize(1);
    if (recienActualizada) {
      tft.setTextColor(COLOR_VERDE_OK, COLOR_FONDO);
      escribir("Actualizada desde la web", 240, 238);
    } else {
      tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
      escribir("Guardada en la tarjeta SD", 240, 238);
    }
  }

  dibujarBoton(90, 258, 300, 46, "Volver", COLOR_TEXTO_GRIS);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 90, 258, 300, 46)) return;
  }
}

void BronquitoUI::mostrarEstadoConexion(const String &ssid, const String &ipCompu) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Estado de la conexión", 15, 23);

  dibujarIndicadorConexion(40, 78);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  escribir(estadoCon == CON_WIFI_Y_COMPU ? "Todo conectado"
         : estadoCon == CON_SOLO_WIFI    ? "Falta conectar la computadora"
                                         : "Sin conexión a Wi-Fi", 65, 78);

  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  escribir("Wi-Fi", 30, 122);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
  escribir(ssid.length() > 0 ? ("Conectado a " + ssid).substring(0, 36) : "Sin conexión", 30, 146);

  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  escribir("Computadora", 30, 184);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
  if (ipCompu.length() == 0)               escribir("Todavía no se configuró", 30, 208);
  else if (estadoCon == CON_WIFI_Y_COMPU)  escribir("Conectada (" + ipCompu + ")", 30, 208);
  else                                     escribir("No conectada (" + ipCompu + ")", 30, 208);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  escribir("Para sincronizar o cambiar la red, tocá Conectar.", 240, 240);

  dibujarBoton(90, 258, 300, 46, "Volver", COLOR_TEXTO_GRIS);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 90, 258, 300, 46)) return;
  }
}

// ======================= HISTORIAL =======================

int BronquitoUI::mostrarHistorial() {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Historial", 15, 23);

  // Botón Calibraciones
  tft.fillRoundRect(25, 65, 205, 135, 12, COLOR_TARJETA_BG);
  tft.drawRoundRect(25, 65, 205, 135, 12, BRONQUITO_CYAN);
  tft.drawRoundRect(26, 66, 203, 133, 11, BRONQUITO_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Calibraciones", 127, 120);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_TARJETA_BG);
  escribir("PIM y Espirometría", 127, 155);

  // Botón Entrenamientos
  tft.fillRoundRect(250, 65, 205, 135, 12, COLOR_TARJETA_BG);
  tft.drawRoundRect(250, 65, 205, 135, 12, BRONQUITO_CYAN);
  tft.drawRoundRect(251, 66, 203, 133, 11, BRONQUITO_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir("Entrenamientos", 352, 120);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_TARJETA_BG);
  escribir("Sesiones realizadas", 352, 155);

  dibujarBoton(25, 230, 205, 60, "Volver al menú", COLOR_TEXTO_GRIS);
  dibujarBoton(250, 230, 205, 60, "Enviar a la web", COLOR_VERDE_OK);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 25, 65, 205, 135))  return 0;
    if (dentro(x, y, 250, 65, 205, 135)) return 1;
    if (dentro(x, y, 25, 230, 205, 60))  return -1;
    if (dentro(x, y, 250, 230, 205, 60)) return 2;
  }
}

int BronquitoUI::elegirCalibracion(const String *fechas, const String *tipos, int n, int &pagina) {
  hasDot = false;
  const int POR_PAG = 5;
  const int FILA_Y = 60, FILA_H = 36, FILA_P = 40;
  int paginas = (n + POR_PAG - 1) / POR_PAG;
  if (paginas < 1) paginas = 1;
  int pag = constrain(pagina, 0, paginas - 1);

  tft.fillScreen(COLOR_FONDO);
  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Calibraciones guardadas", 15, 23);

  auto dibujarPagina = [&]() {
    tft.fillRect(0, 46, 480, 274, COLOR_FONDO);
    tft.setTextDatum(ML_DATUM);

    if (n == 0) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
      escribir("No hay calibraciones", 240, 120);
      tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
      escribir("guardadas todavía", 240, 150);
    } else {
      tft.setTextSize(1);
      tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
      escribir("FECHA", 20, 53);
      escribir("TIPO", 225, 53);

      for (int i = 0; i < POR_PAG; i++) {
        int idx = pag * POR_PAG + i;
        if (idx >= n) break;
        int y = FILA_Y + i * FILA_P;

        tft.fillRoundRect(10, y, 460, FILA_H, 6, COLOR_CAMPO_BG);
        tft.setTextDatum(ML_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
        escribir(fechas[idx], 20, y + FILA_H / 2);
        tft.setTextColor(BRONQUITO_CYAN, COLOR_CAMPO_BG);
        escribir(tipos[idx], 225, y + FILA_H / 2);
        dibujarBoton(385, y + 3, 75, FILA_H - 6, "Ver", BRONQUITO_CYAN);
      }
    }

    dibujarBoton(10, 266, 150, 40, "Volver", COLOR_TEXTO_GRIS);
    if (paginas > 1) {
      dibujarBoton(250, 266, 50, 40, "<", pag > 0 ? BRONQUITO_CYAN : COLOR_TEXTO_GRIS);
      dibujarBoton(380, 266, 50, 40, ">", pag < paginas - 1 ? BRONQUITO_CYAN : COLOR_TEXTO_GRIS);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
      escribir(String(pag + 1) + "/" + String(paginas), 340, 286);
    }
    tft.setTextDatum(TL_DATUM);
  };
  dibujarPagina();

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;

    if (dentro(x, y, 10, 266, 150, 40)) { pagina = pag; return -1; }

    if (paginas > 1) {
      if (dentro(x, y, 250, 266, 50, 40) && pag > 0)            { pag--; dibujarPagina(); continue; }
      if (dentro(x, y, 380, 266, 50, 40) && pag < paginas - 1)  { pag++; dibujarPagina(); continue; }
    }

    for (int i = 0; i < POR_PAG; i++) {
      int idx = pag * POR_PAG + i;
      if (idx >= n) break;
      if (dentro(x, y, 375, FILA_Y + i * FILA_P, 95, FILA_H)) { pagina = pag; return idx; }
    }
  }
}

int BronquitoUI::mostrarResumenCalibracion(const String &titulo, const String &fecha, const MetricaItem *m, int n, const String &pie) {
  hasDot = false;
  if (n > 8) n = 8;
  const bool compacto = (n > 6);   // 4 filas más bajas en lugar de 3

  tft.fillScreen(COLOR_FONDO);
  tft.fillRect(0, 0, 480, 45, BRONQUITO_CYAN);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  tft.setTextDatum(ML_DATUM);
  escribir(titulo, 15, 23);
  tft.setTextDatum(MR_DATUM);
  escribir(fecha, 465, 23);

  const int TW = 215, TH = compacto ? 46 : 62, PASO = compacto ? 50 : 68;
  const int Y_VALOR = compacto ? 18 : 28, Y_UNIDAD = compacto ? 25 : 36, Y_ETIQUETA = compacto ? 5 : 8;
  for (int i = 0; i < n; i++) {
    int x = (i % 2 == 0) ? 15 : 250;
    int y = 54 + (i / 2) * PASO;

    tft.fillRoundRect(x, y, TW, TH, 8, COLOR_TARJETA_BG);
    tft.drawRoundRect(x, y, TW, TH, 8, BRONQUITO_CYAN);

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_TARJETA_BG);
    escribir(m[i].etiqueta, x + 12, y + Y_ETIQUETA);

    tft.setTextSize(3);
    tft.setTextColor(BRONQUITO_CYAN, COLOR_TARJETA_BG);
    escribir(m[i].valor, x + 12, y + Y_VALOR);
    int w = tft.textWidth(paraPantalla(m[i].valor));

    tft.setTextSize(2);
    tft.setTextColor(BRONQUITO_BROWN, COLOR_TARJETA_BG);
    escribir(m[i].unidad, x + 12 + w + 6, y + Y_UNIDAD);
  }

  // Botones abajo; con "pie" (condiciones ambientales) quedan a la izquierda
  int vx = 60, vw = 170, gx = 250, gw = 170;
  if (pie.length() > 0) {
    vx = 15; vw = 130; gx = 153; gw = 150;
    int k = pie.indexOf(" | ");
    String l1 = (k < 0) ? pie : pie.substring(0, k);
    String l2 = (k < 0) ? String("") : pie.substring(k + 3);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
    escribir(l1, 395, 272);
    escribir(l2, 395, 290);
  }
  dibujarBoton(vx, 258, vw, 46, "Volver", COLOR_TEXTO_GRIS);
  dibujarBoton(gx, 258, gw, 46, "Graficar", BRONQUITO_CYAN);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, vx, 258, vw, 46)) return 0;
    if (dentro(x, y, gx, 258, gw, 46)) return 1;
  }
}

// ======================= GRÁFICOS =======================

// Paso "redondo" (1, 2 o 5 x 10^k) para que el eje tenga ~objetivo divisiones
static float pasoBonito(float rango, int objetivo) {
  if (rango <= 0) return 1.0f;
  float crudo = rango / objetivo;
  float mag = powf(10.0f, floorf(log10f(crudo)));
  float r = crudo / mag;
  float f = r < 1.5f ? 1.0f : r < 3.5f ? 2.0f : r < 7.5f ? 5.0f : 10.0f;
  return f * mag;
}

int BronquitoUI::mostrarGrafico(const String &titulo, const String &etiquetaX, const String &etiquetaY,
                                const float *x, const float *y, int n, const String &botonAlt,
                                const float *flujo) {
  const uint16_t COLOR_ESPIRACION  = BRONQUITO_CYAN;
  const uint16_t COLOR_INSPIRACION = BRONQUITO_ORANGE;
  hasDot = false;
  const int PX = 50, PY = 54, PW = 415, PH = 186;
  const uint16_t COLOR_GRILLA = 0xDEFB;

  tft.fillScreen(COLOR_FONDO);
  tft.fillRect(0, 0, 480, 40, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir(titulo, 15, 20);

  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  escribir(etiquetaY, PX, 43);
  tft.setTextDatum(TC_DATUM);
  escribir(etiquetaX, PX + PW / 2, 258);

  // Leyenda de colores (arriba a la derecha)
  if (flujo) {
    tft.setTextDatum(ML_DATUM);
    tft.fillRect(300, 44, 10, 8, COLOR_ESPIRACION);
    escribir("Espiración", 314, 48);
    tft.fillRect(386, 44, 10, 8, COLOR_INSPIRACION);
    escribir("Inspiración", 400, 48);
    tft.setTextDatum(TL_DATUM);
  }

  if (n < 2) {
    tft.drawRect(PX, PY, PW, PH, COLOR_TEXTO_GRIS);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
    escribir("Sin datos para graficar", PX + PW / 2, PY + PH / 2);
  } else {
    // Rangos (siempre incluyen el cero)
    float xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    for (int i = 0; i < n; i++) {
      if (x[i] < xmin) xmin = x[i];
      if (x[i] > xmax) xmax = x[i];
      if (y[i] < ymin) ymin = y[i];
      if (y[i] > ymax) ymax = y[i];
    }
    if (xmax - xmin < 1e-6f) xmax = xmin + 1.0f;
    if (ymax - ymin < 1e-6f) ymax = ymin + 1.0f;

    float pasoX = pasoBonito(xmax - xmin, 6);
    float pasoY = pasoBonito(ymax - ymin, 5);
    xmin = floorf(xmin / pasoX) * pasoX;  xmax = ceilf(xmax / pasoX) * pasoX;
    ymin = floorf(ymin / pasoY) * pasoY;  ymax = ceilf(ymax / pasoY) * pasoY;

    auto mapX = [&](float v) { return PX + (int)((v - xmin) / (xmax - xmin) * PW + 0.5f); };
    auto mapY = [&](float v) { return PY + PH - (int)((v - ymin) / (ymax - ymin) * PH + 0.5f); };
    int decX = pasoX >= 1.0f ? 0 : (pasoX >= 0.1f ? 1 : 2);
    int decY = pasoY >= 1.0f ? 0 : (pasoY >= 0.1f ? 1 : 2);

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);

    // Grilla + números de los ejes
    int nx = (int)((xmax - xmin) / pasoX + 0.5f);
    for (int i = 0; i <= nx; i++) {
      float v = xmin + i * pasoX;
      if (fabsf(v) < pasoX * 0.001f) v = 0;
      int sx = mapX(v);
      tft.drawFastVLine(sx, PY, PH, COLOR_GRILLA);
      tft.setTextDatum(TC_DATUM);
      escribir(String(v, decX), constrain(sx, PX, PX + PW), PY + PH + 4);
    }
    int ny = (int)((ymax - ymin) / pasoY + 0.5f);
    for (int i = 0; i <= ny; i++) {
      float v = ymin + i * pasoY;
      if (fabsf(v) < pasoY * 0.001f) v = 0;
      int sy = mapY(v);
      tft.drawFastHLine(PX, sy, PW, COLOR_GRILLA);
      tft.setTextDatum(MR_DATUM);
      escribir(String(v, decY), PX - 4, sy);
    }

    // Ejes en cero (si el cero queda dentro del gráfico)
    if (ymin < 0) tft.drawFastHLine(PX, mapY(0), PW, COLOR_TEXTO_GRIS);
    if (xmin < 0) tft.drawFastVLine(mapX(0), PY, PH, COLOR_TEXTO_GRIS);

    // Señal
    int px0 = constrain(mapX(x[0]), PX, PX + PW);
    int py0 = constrain(mapY(y[0]), PY, PY + PH);
    for (int i = 1; i < n; i++) {
      int px1 = constrain(mapX(x[i]), PX, PX + PW);
      int py1 = constrain(mapY(y[i]), PY, PY + PH);
      if (px1 == px0 && py1 == py0) continue;
      uint16_t color = (flujo && flujo[i] < 0.0f) ? COLOR_INSPIRACION : COLOR_ESPIRACION;
      tft.drawLine(px0, py0, px1, py1, color);
      tft.drawLine(px0 + 1, py0, px1 + 1, py1, color);
      tft.drawLine(px0, py0 + 1, px1, py1 + 1, color);
      px0 = px1; py0 = py1;
    }
    tft.drawRect(PX, PY, PW, PH, COLOR_TEXTO_GRIS);
  }

  dibujarBoton(10, 274, 150, 40, "Volver", COLOR_TEXTO_GRIS);
  if (botonAlt.length() > 0) dibujarBoton(170, 274, 300, 40, botonAlt, BRONQUITO_CYAN);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int tx, ty;
    if (!leerToque(tx, ty)) continue;
    if (dentro(tx, ty, 10, 274, 150, 40)) return 0;
    if (botonAlt.length() > 0 && dentro(tx, ty, 170, 274, 300, 40)) return 1;
  }
}

// ======================= CALIBRACIÓN GUIADA =======================

void BronquitoUI::drawCalibPimRunning(float tTotalSec) {
  cancelArmado = false;
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);

  tft.fillRect(0, 0, 480, 40, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir("Midiendo PIM...", 15, 20);

  tft.fillRoundRect(30, 55, 420, 175, 10, COLOR_CAMPO_BG);
  tft.drawRoundRect(30, 55, 420, 175, 10, BRONQUITO_CYAN);

  // Pantalla fija durante la medición (no se actualiza: ver FlujoCalibracion::capturarManiobra)
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_CAMPO_BG);
  escribir("La medición dura " + String((int)lroundf(tTotalSec)) + " segundos", 240, 100);
  setHintPim("Espere... no respire");   // se cambia por "Inspire..." recién terminada la tara

  dibujarBoton(120, 245, 240, 50, "Cancelar", COLOR_ROJO_DEL);
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::updateCalibPimProgress(float presionCmH2O, float tiempoSec) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(400);

  tft.setTextColor(BRONQUITO_CYAN, COLOR_CAMPO_BG);
  tft.setTextSize(4);
  escribir(String(presionCmH2O, 1) + " cmH2O", 240, 110);

  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_CAMPO_BG);
  tft.setTextSize(2);
  escribir("Tiempo: " + String(tiempoSec, 1) + " s", 240, 170);

  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

// Solo cuenta regresiva y barra. El flujo y el volumen se calculan recién al terminar la medición.
// Se redibuja lo mínimo para no frenar el muestreo.
void BronquitoUI::updateCalibEspProgress(float tiempoSec, float tTotalSec) {
  float restante = tTotalSec - tiempoSec;
  if (restante < 0.0f) restante = 0.0f;

  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(200);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_CAMPO_BG);
  tft.setTextSize(4);
  escribir(String((int)ceilf(restante)) + " s", 240, 122);

  tft.setTextPadding(0);

  // Barra de progreso
  const int BX = 70, BY = 196, BW = 340, BH = 14;
  float frac = (tTotalSec > 0.0f) ? tiempoSec / tTotalSec : 0.0f;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  int w = (int)(BW * frac);
  tft.fillRect(BX, BY, w, BH, BRONQUITO_CYAN);
  tft.fillRect(BX + w, BY, BW - w, BH, COLOR_FONDO);
  tft.drawRect(BX - 1, BY - 1, BW + 2, BH + 2, COLOR_TEXTO_GRIS);

  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::setHintPim(const String &texto) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextPadding(400);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
  escribir(texto, 240, 207);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

void BronquitoUI::setHintEsp(const String &texto) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextPadding(400);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_CAMPO_BG);
  escribir(texto, 240, 165);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

bool BronquitoUI::cancelarTocado() {
  if (!touch.touched()) {
    cancelArmado = true;
    return false;
  }
  if (!cancelArmado) return false;

  TS_Point p = touch.getPoint();
  int x = constrain(map(p.x, 300, 3800, 0, 480), 0, 479);
  int y = constrain(map(p.y, 300, 3800, 0, 319), 0, 319);
  return dentro(x, y, 120, 245, 240, 50);
}

bool BronquitoUI::mostrarPasoCalibracion(const String &titulo, const String &subtitulo, const String *lineas, int n,
                                         const String &textoBoton, uint16_t colorBoton) {
  hasDot = false;
  if (n > 4) n = 4;

  tft.fillScreen(COLOR_FONDO);
  tft.fillRect(0, 0, 480, 40, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir(titulo, 15, 20);

  tft.fillRoundRect(20, 52, 440, 180, 10, COLOR_TARJETA_BG);
  tft.drawRoundRect(20, 52, 440, 180, 10, BRONQUITO_CYAN);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_TARJETA_BG);
  tft.setTextSize(2);
  escribir(subtitulo, 35, 65);

  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_TARJETA_BG);
  for (int i = 0; i < n; i++) escribir(lineas[i], 35, 98 + i * 30);

  dibujarBoton(20, 248, 180, 55, "Cancelar", COLOR_TEXTO_GRIS);
  dibujarBoton(220, 248, 240, 55, textoBoton, colorBoton);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 20, 248, 180, 55))  return false;
    if (dentro(x, y, 220, 248, 240, 55)) return true;
  }
}

int BronquitoUI::revisarManiobra(const String &titulo, const String &l1, const String &l2) {
  hasDot = false;
  tft.fillScreen(COLOR_FONDO);
  tft.fillRect(0, 0, 480, 40, BRONQUITO_CYAN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_FONDO, BRONQUITO_CYAN);
  tft.setTextSize(2);
  escribir(titulo, 15, 20);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(BRONQUITO_CYAN, COLOR_FONDO);
  tft.setTextSize(3);
  escribir(l1.substring(0, 20), 240, 105);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXTO_GRIS, COLOR_FONDO);
  escribir(l2.substring(0, 36), 240, 150);
  tft.setTextColor(BRONQUITO_BROWN, COLOR_FONDO);
  escribir("¿Aceptás esta medición?", 240, 195);

  dibujarBoton(10, 248, 140, 55, "Cancelar", COLOR_TEXTO_GRIS);
  dibujarBoton(165, 248, 145, 55, "Repetir", BRONQUITO_ORANGE);
  dibujarBoton(325, 248, 145, 55, "Aceptar", COLOR_VERDE_OK);
  tft.setTextDatum(TL_DATUM);

  while (true) {
    int x, y;
    if (!leerToque(x, y)) continue;
    if (dentro(x, y, 10, 248, 140, 55))  return -1;
    if (dentro(x, y, 165, 248, 145, 55)) return 0;
    if (dentro(x, y, 325, 248, 145, 55)) return 1;
  }
}
