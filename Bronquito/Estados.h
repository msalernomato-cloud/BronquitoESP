#ifndef ESTADOS_H
#define ESTADOS_H

// =============================================================================
// MÁQUINA DE ESTADOS DE LA INTERFAZ
// -----------------------------------------------------------------------------
// ESTADO = en qué pantalla está el equipo (y, por lo tanto, qué botones hay).
// EVENTO = qué tocó el usuario en esa pantalla.
//
// En cada vuelta del loop():
//   1. ui.detectarEvento(estadoActual) mira si tocaron la pantalla y, según el
//      estado actual y la zona tocada, devuelve un evento (o EVENTO_NINGUNO).
//   2. Un switch (evento) en el .ino decide qué hacer: cambiar a otro estado y
//      dibujar su pantalla, o ejecutar un flujo (calibración, historial...).
// El mismo lugar de la pantalla puede dar eventos distintos según el estado.
// =============================================================================

enum EstadoSistema {
  ESTADO_INICIO_SESION,      // pantalla de login (pollito, "Ingresar", "Conectar")
  ESTADO_PIN,                // teclado del PIN
  ESTADO_MENU_PRINCIPAL,     // Entrenamiento · Calibración · Configuración · Historial
  ESTADO_MENU_CALIBRACION    // "Calibrar" (PIM + espirometría) y "Volver"
};

enum EventoPantalla {
  EVENTO_NINGUNO,            // no tocaron nada (o tocaron fuera de los botones)

  // Inicio de sesión
  EVENTO_CONECTAR,           // botón "Conectar" (Wi-Fi / computadora)
  EVENTO_VER_CONEXION,       // indicador de conexión (círculo de color)
  EVENTO_INGRESAR,           // botón "Ingresar": va al teclado del PIN

  // PIN
  EVENTO_PIN_INGRESADO,      // tocaron "OK" con los 4 dígitos
  EVENTO_IR_AL_INICIO,       // "<" en el PIN, o "Cerrar sesión" en el menú

  // Menú principal
  EVENTO_ENTRENAMIENTO,
  EVENTO_CALIBRACION,
  EVENTO_CONFIGURACION,
  EVENTO_HISTORIAL,

  // Menú de calibración
  EVENTO_CALIBRAR,           // arranca la calibración guiada
  EVENTO_VOLVER              // vuelve al menú principal
};

#endif
