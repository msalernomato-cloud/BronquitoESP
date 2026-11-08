#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

// Pines asignados al conector J8 (MicroSD) según esquemático
#define SD_CS_DEFAULT   19
#define SD_SCK_DEFAULT  22
#define SD_MISO_DEFAULT 23
#define SD_MOSI_DEFAULT 21

enum StorageStatus {
  STORAGE_OK,
  ERR_NO_CARD,        // No hay tarjeta SD conectada o no responde
  ERR_FILE_NOT_FOUND, // No existe el archivo /perfil.json
  ERR_JSON_PARSE      // El archivo existe pero el formato JSON es inválido
};

enum FormatoResultado {
  FORMATO_OK,
  FORMATO_SIN_SD,      // No hay tarjeta
  FORMATO_PENDIENTES,  // Hay pruebas con "sincronizado": false (no se borró nada)
  FORMATO_ERROR        // Algún archivo no se pudo borrar
};

struct PerfilPaciente {
  String dni;
  String pin;
  String sexo;
  String altura;
  String peso;
  String fumador;
  String nacimiento;
  String registro;
  String enfermedad;
  String nombre;
  String apellido;
  String pinVersion;
};

// Lo que hay en la clave "frecuencia" de /CONFIGURACION.JSON
struct FrecuenciaGuardada {
  int sesiones = 0;        // 0 = no hay ninguna guardada
  bool dias[7] = {};       // 0 = lunes ... 6 = domingo
  String vigenteDesde;     // "AAAA-MM-DD"
};

class BronquitoStorage {
public:
  BronquitoStorage(uint8_t csPin = SD_CS_DEFAULT, 
                   uint8_t sckPin = SD_SCK_DEFAULT, 
                   uint8_t misoPin = SD_MISO_DEFAULT, 
                   uint8_t mosiPin = SD_MOSI_DEFAULT);

  bool begin();
  StorageStatus sync(bool verbose = true);
  bool tarjetaPresente();

  PerfilPaciente getPerfil() const { return paciente; }
  String getPin() const { return paciente.pin; }
  String getDNI() const { return paciente.dni; }
  bool isSynced() const { return synced; }
  StorageStatus getLastError() const { return lastError; }

  bool pinPendiente() const { return paciente.pin.length() == 0; }
  String getPinVersion() const { return paciente.pinVersion; }
  String getNombre() const { return paciente.nombre; }
  String getApellido() const { return paciente.apellido; }

  bool guardarPerfil(JsonVariantConst perfil);
  bool guardarPin(const String& pin, const String& version);
  bool guardarFrecuencia(JsonVariantConst frecuencia);   // en /CONFIGURACION.JSON
  FrecuenciaGuardada leerFrecuencia();
  int contarPendientes();
  FormatoResultado formatearFabrica(bool forzar = false);

private:
  uint8_t chipSelectPin;
  uint8_t sckPin;
  uint8_t misoPin;
  uint8_t mosiPin;
  bool sdMontada;
  PerfilPaciente paciente;
  bool synced;
  StorageStatus lastError;
  
  SPIClass sdSPI; // Instancia de SPI independiente para el lector SD (J8)

  void marcarError(StorageStatus e, const char *textoDNI);
  bool escribirJson(const char *ruta, JsonDocument &doc);
};

#endif