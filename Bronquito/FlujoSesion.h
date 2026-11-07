#ifndef FLUJO_SESION_H
#define FLUJO_SESION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Estados.h"
#include "UI.h"
#include "TarjetaSD.h"
#include "BronquitoNet.h"

// =============================================================================
// SESIÓN DEL PACIENTE
// -----------------------------------------------------------------------------
// Todo lo que tiene que ver con QUIÉN usa el equipo:
//   - la pantalla de inicio de sesión (qué paciente muestra, aviso si falta la tarjeta)
//   - el PIN: validarlo (contra la tarjeta o, si falta, contra la web) y pedir uno nuevo
//   - cargar un paciente nuevo y borrar los datos del actual
//
// Los cambios de estado los hace el .ino; estas funciones dicen si se puede pasar
// (ingresar(), validarPin()) y muestran los mensajes. La única excepción es borrarDatos(),
// que siempre deja el equipo en el inicio de sesión.
// =============================================================================
class FlujoSesion {
public:
  FlujoSesion(BronquitoUI &ui, TarjetaSD &tarjeta, BronquitoNetwork &net, EstadoSistema &estado);

  // ---- Inicio de sesión ----
  void mostrarResultadoTarjeta(EstadoTarjeta resultado);   // al arrancar: login + cartel si hubo error
  void revisarSD();                    // en el login, cada 2 s: si cambió la tarjeta, redibuja
  void refrescarPerfilEnUI();          // vuelve a leer perfil.json y actualiza el login (sin dibujar)
  void redibujarPantallaActual();      // login o menú principal, según el estado

  // ---- Botones (los llama el .ino) ----
  bool ingresar();     // "Ingresar": true si hay paciente (puede ir al PIN); si no, avisa
  bool validarPin();   // "OK" del teclado: true si el PIN es correcto

  // ---- Paciente (los llama FlujoConexion cuando la web lo pide, o el monitor serie) ----
  // Devuelven "" si salió bien, o el motivo para informarle a la web
  String cargarPaciente(JsonObjectConst perfil);   // pide confirmación, guarda y pide el PIN
  String prepararNuevoPaciente();                  // pide confirmación y borra los datos
  ResultadoFormato borrarDatos(bool forzar);       // borra sin preguntar (forzar = aunque haya pendientes)
  // Pide un PIN y lo verifica con la web hasta que sea correcto. false si el usuario sale o no hay conexión
  bool pedirPinNuevo(const String &titulo, const String &subtitulo);

private:
  BronquitoUI &ui;
  TarjetaSD &tarjeta;
  BronquitoNetwork &net;
  EstadoSistema &estado;

  unsigned long ultimoChequeoSD = 0;
  String ultimaClaveSD = "";

  // 1 = PIN correcto, 0 = incorrecto, -1 = no se pudo verificar (sin conexión, sin paciente en la web...)
  int verificarPinEnLinea(const String &pin);
};

#endif
