#ifndef FLUJO_CONEXION_H
#define FLUJO_CONEXION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "SystemState.h"
#include "UI.h"
#include "Storage.h"
#include "BronquitoNet.h"
#include "BronquitoRTC.h"
#include "Evaluaciones.h"
#include "FlujoSesion.h"

// Todo lo que pasa entre el equipo y la web: Wi-Fi, elegir la compu, sincronizar,
// subir las pruebas, atender los pedidos del médico, verificar el PIN y bajar la frecuencia.
class FlujoConexion {
public:
  FlujoConexion(BronquitoUI &ui, BronquitoNetwork &net, BronquitoStorage &storage, Evaluaciones &evaluaciones,
                BronquitoRTC &rtc, FlujoSesion &sesion, SystemState &estado);

  // Llamar en cada vuelta del loop: detecta si se conectó o se cayó el Wi-Fi
  void actualizarConexion();
  EstadoConexion calcularEstado();
  // Si recién se conectó el Wi-Fi y estamos en el login, sincroniza con la compu guardada
  void atenderSyncInicial();
  // Si cambió el estado de la conexión, actualiza el indicador y redibuja
  void actualizarIndicador();

  // Botón "Conectar": elegir red, elegir compu o sincronizar
  void menuConectar();
  // Pantalla con el estado de la conexión
  void mostrarEstado();
  // Pantalla de configuración: baja la frecuencia de la web (si se puede) y la muestra
  void mostrarConfiguracion();
  // Al terminar una evaluación: si hay pruebas sin subir (y Wi-Fi y compu), pregunta si mandarlas ya.
  // Si acepta, sincroniza; si no, se suben en la próxima sincronización
  void ofrecerEnvio();
  // Botón "Enviar a la web" del historial: sincroniza ya (sube las pruebas y atiende los pedidos del médico)
  void enviarDatos();

  // 1 = PIN correcto, 0 = PIN incorrecto, -1 = no se pudo verificar
  int verificarPinOnline(const String &pin);
  // Borra la red y la compu guardadas
  void olvidarRedes();

private:
  BronquitoUI &ui;
  BronquitoNetwork &net;
  BronquitoStorage &storage;
  Evaluaciones &evaluaciones;
  BronquitoRTC &rtc;
  FlujoSesion &sesion;
  SystemState &estado;

  bool compuOk = false;
  bool wifiAntes = false;
  bool syncInicialPendiente = false;
  EstadoConexion ultimoEstado = CON_NINGUNA;

  bool dispositivoVacio();
  String usuarioActual();
  int archivosPendientes();

  bool identificarCompu(const String &ip, RespuestaIdentificar &r);
  bool pedirYVerificarPin(const String &titulo, const String &subtitulo);
  void atenderSolicitud(JsonObject s);
  int procesarSolicitudes();
  int subirPendientes();
  void sincronizar();
  bool actualizarFrecuencia();
  bool flujoCompu();
  void flujoWifi();
};

#endif
