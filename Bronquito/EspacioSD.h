#ifndef ESPACIO_SD_H
#define ESPACIO_SD_H

#include <Arduino.h>
#include "Estados.h"
#include "UI.h"
#include "TarjetaSD.h"
#include "FlujoConexion.h"

// Espacio en la microSD. Antes de medir, si queda poco, borra las pruebas más viejas (calibraciones y
// entrenamientos, cada una con todos sus archivos). NUNCA borra una que no esté en la web: si una de las
// viejas no está sincronizada, primero pide Wi-Fi (y la computadora) y sincroniza.
// Los umbrales están arriba de EspacioSD.cpp.
class EspacioSD {
public:
  EspacioSD(BronquitoUI &ui, TarjetaSD &tarjeta, FlujoConexion &conexion, const EstadoSistema &estado);

  // true = se puede medir. false = no queda lugar para guardar la prueba, o la sincronización dejó el equipo
  // en el login (la web pidió borrar el paciente): en ese caso el .ino redibuja el login
  bool asegurarEspacio();
  // Comando SD del monitor serie: total, usado y libre (y cuánto tardó en calcularlo)
  void informarPorSerie();

private:
  BronquitoUI &ui;
  TarjetaSD &tarjeta;
  FlujoConexion &conexion;
  const EstadoSistema &estado;   // solo se lee (para saber si la sincronización dejó el equipo en el login)
  bool primeraRevision = true;   // la primera medición del espacio puede tardar en tarjetas grandes
};

#endif
