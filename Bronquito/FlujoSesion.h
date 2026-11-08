#ifndef FLUJO_SESION_H
#define FLUJO_SESION_H

#include <Arduino.h>
#include "SystemState.h"
#include "UI.h"
#include "Storage.h"

// Perfil del paciente en la SD y pantalla de login: lo que el usuario ve antes de entrar,
// y el borrado para preparar el equipo para otro paciente.
class FlujoSesion {
public:
  FlujoSesion(BronquitoUI &ui, BronquitoStorage &storage, SystemState &estado);

  // Muestra en el login el resultado de leer la SD (y un cartel si hubo error)
  void manejarResultadoSincronizacion(StorageStatus status);
  // Vuelve a leer perfil.json y actualiza los datos del login, sin dibujar
  void refrescarPerfilEnUI();
  // Redibuja el login o el menú principal, según el estado actual
  void redibujarPantallaActual();
  // En el login, cada 2 s se fija si cambió la tarjeta y, si cambió, redibuja
  void revisarSD();
  // Borra los datos del paciente. forzar = borrar aunque haya pruebas sin subir
  void formatearDispositivo(bool forzar);

private:
  BronquitoUI &ui;
  BronquitoStorage &storage;
  SystemState &estado;

  unsigned long ultimoChequeoSD = 0;
  String ultimaClaveSD = "";
};

#endif
