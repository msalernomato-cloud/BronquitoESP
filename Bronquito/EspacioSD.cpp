#include "EspacioSD.h"

// Una calibración ocupa ~130 KB (PIM + espirometría, .csv y .json) y hasta ~0,5 MB con varias espirometrías
// fallidas (en tarjetas grandes cada archivo ocupa como mínimo 32 KB).
static const uint64_t MB = 1024ULL * 1024ULL;
static const uint64_t UMBRAL_LIMPIEZA = 500 * MB;    // con menos que esto libre se borran pruebas viejas
static const uint64_t OBJETIVO_LIBRE  = 1024 * MB;   // se borra hasta tener esto libre (así no limpia antes de cada calibración)
static const uint64_t MINIMO_CRITICO  = 5 * MB;      // con menos que esto no se deja medir (no se podría guardar)
static const int GRUPOS_POR_PASADA    = 40;          // pruebas viejas que se revisan por vuelta (no hay RAM para todas)
static const int MAX_SINCRONIZACIONES = 3;           // cada sincronización sube hasta 100 pruebas

EspacioSD::EspacioSD(BronquitoUI &ui, TarjetaSD &tarjeta, FlujoConexion &conexion, const EstadoSistema &estado)
  : ui(ui), tarjeta(tarjeta), conexion(conexion), estado(estado) {}

// "850 MB" o "1,2 GB"
static String textoEspacio(uint64_t bytes) {
  if (bytes >= 1024ULL * MB) {
    String t = String(bytes / (1024.0f * 1024.0f * 1024.0f), 1);
    t.replace('.', ',');
    return t + " GB";
  }
  return String((unsigned long)(bytes / MB)) + " MB";
}

void EspacioSD::informarPorSerie() {
  if (!tarjeta.hayTarjeta()) {
    Serial.println("[SD] No hay tarjeta");
    return;
  }
  uint32_t t0 = millis();
  uint64_t total = tarjeta.espacioTotal();
  uint64_t libre = tarjeta.espacioLibre();
  Serial.printf("[SD] Total %s | usado %s | libre %s (calculado en %lu ms)\n", textoEspacio(total).c_str(),
                textoEspacio(total - libre).c_str(), textoEspacio(libre).c_str(), (unsigned long)(millis() - t0));
  Serial.printf("[SD] Limpieza: con menos de %s libres, borra pruebas viejas ya enviadas hasta tener %s\n",
                textoEspacio(UMBRAL_LIMPIEZA).c_str(), textoEspacio(OBJETIVO_LIBRE).c_str());
}

bool EspacioSD::asegurarEspacio() {
  if (!tarjeta.hayTarjeta()) return true;   // sin tarjeta, la calibración ya avisa cuando quiere guardar

  if (primeraRevision) {
    ui.mostrarMensaje("Revisando la microSD...");
    primeraRevision = false;
  }
  uint64_t libre = tarjeta.espacioLibre();
  Serial.printf("[SD] Espacio libre: %s\n", textoEspacio(libre).c_str());
  if (libre >= UMBRAL_LIMPIEZA) return true;

  ui.mostrarMensaje("Poco espacio en la microSD", "Quedan " + textoEspacio(libre) + ": se borran pruebas viejas",
                    BRONQUITO_ORANGE);
  delay(2500);
  ui.mostrarMensaje("Liberando espacio...", "Se borran pruebas que ya están en la web");

  int borradas = 0;
  int sincronizaciones = 0;
  bool puedeSincronizar = true;   // pasa a false si una sincronización falló (o la cancelaron)

  // Por vueltas: en cada una se toman las pruebas más viejas que quedan y se borran las que están en la web.
  // Termina cuando hay suficiente lugar o cuando en una vuelta no se pudo borrar ninguna.
  while (libre < OBJETIVO_LIBRE) {
    std::vector<GrupoPruebas> grupos = tarjeta.gruposMasViejos(GRUPOS_POR_PASADA);
    int borradasEnLaVuelta = 0;

    for (size_t i = 0; i < grupos.size() && libre < OBJETIVO_LIBRE; i++) {
      bool enLaWeb = tarjeta.grupoSincronizado(grupos[i]);

      if (!enLaWeb && puedeSincronizar && sincronizaciones < MAX_SINCRONIZACIONES) {
        // Es de las más viejas y no está en la web: primero hay que subirla
        Serial.println("[SD] " + grupos[i].id + " no está en la web: se sincroniza antes de borrar");
        sincronizaciones++;
        bool ok = conexion.conectarYSincronizar();
        if (estado == ESTADO_INICIO_SESION) return false;            // la web pidió borrar el paciente
        if (!tarjeta.hayTarjeta()) return true;         // sacaron la tarjeta: la calibración avisa al guardar
        enLaWeb = tarjeta.grupoSincronizado(grupos[i]);
        if (!ok || !enLaWeb) puedeSincronizar = ok && sincronizaciones < MAX_SINCRONIZACIONES;
        ui.mostrarMensaje("Liberando espacio...", "Se borran pruebas que ya están en la web");
      }
      if (!enLaWeb) continue;   // no está en la web: no se borra

      if (tarjeta.borrarGrupo(grupos[i])) {
        borradas++;
        borradasEnLaVuelta++;
      }
      libre = tarjeta.espacioLibre();
    }
    if (borradasEnLaVuelta == 0) break;   // no queda nada que se pueda borrar
  }

  Serial.printf("[SD] Limpieza: %d pruebas borradas | libre %s\n", borradas, textoEspacio(libre).c_str());
  if (borradas > 0) {
    ui.mostrarMensaje(borradas == 1 ? String("Se borró 1 prueba vieja")
                                    : "Se borraron " + String(borradas) + " pruebas viejas",
                      "Ya estaban en la web. Libre: " + textoEspacio(libre), COLOR_VERDE_OK);
    delay(2500);
  }

  if (libre < MINIMO_CRITICO) {
    ui.mostrarError("No hay espacio para medir", "Enviá las pruebas a la web");
    return false;
  }
  if (libre < UMBRAL_LIMPIEZA) {
    // Se puede medir igual, pero la próxima vez vuelve a intentar liberar espacio
    ui.mostrarMensaje("La microSD sigue con poco espacio", "Quedan " + textoEspacio(libre), BRONQUITO_ORANGE);
    delay(2500);
  }
  return true;
}
