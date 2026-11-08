#ifndef FLUJO_CONEXION_H
#define FLUJO_CONEXION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Estados.h"
#include "UI.h"
#include "TarjetaSD.h"
#include "BronquitoNet.h"
#include "Reloj.h"
#include "FlujoSesion.h"

// La conexión con la web: Wi-Fi, elegir la computadora, el indicador de conexión, sincronizar y subir las pruebas.
// Al sincronizar, los pedidos que tocan al paciente (cargar uno nuevo, borrar los datos, PIN nuevo)
// se los pasa a FlujoSesion; acá solo se le responde a la web.
class FlujoConexion {
public:
  FlujoConexion(BronquitoUI &ui, BronquitoNetwork &net, TarjetaSD &tarjeta, Reloj &reloj,
                FlujoSesion &sesion, const EstadoSistema &estado);

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
  // Al terminar una evaluación: si hay pruebas sin subir (y Wi-Fi y compu), pregunta si mandarlas ya.
  // Si acepta, sincroniza; si no, se suben en la próxima sincronización
  void ofrecerEnvio();
  // Botón "Enviar a la web" del historial: sincroniza ya (sube las pruebas y atiende los pedidos del médico)
  void enviarDatos();
  // Borra todas las redes recordadas y la compu guardada
  void olvidarRedes();

private:
  BronquitoUI &ui;
  BronquitoNetwork &net;
  TarjetaSD &tarjeta;
  Reloj &reloj;
  FlujoSesion &sesion;
  const EstadoSistema &estado;   // solo se lee (para sincronizar al conectar, únicamente en el login)

  bool compuOk = false;
  bool wifiAntes = false;
  bool syncInicialPendiente = false;
  EstadoConexion ultimoEstado = CON_NINGUNA;

  bool dispositivoVacio();
  String usuarioActual();
  int archivosPendientes();

  bool identificarCompu(const String &ip, RespuestaIdentificar &r);
  void atenderSolicitud(JsonObject s);
  int procesarSolicitudes();
  int subirPendientes();
  void sincronizar();
  bool flujoCompu();
  void flujoWifi();
};

#endif
