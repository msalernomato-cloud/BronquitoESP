#ifndef BTPS_SENSOR_H
#define BTPS_SENSOR_H

#include <Arduino.h>
#include <math.h>
#include "DHT.h"

class BTPSSensor {
public:
    
    BTPSSensor(uint8_t pin, uint8_t type = DHT22, float presionBarometrica = 760.0);

    // Inicializa el hardware del sensor
    void begin();

    // Actualiza las lecturas de temperatura y humedad desde el sensor
    bool leer();

    // Retorna la temperatura ambiental en °C
    float getTemperatura() const;

    // Retorna la humedad relativa en %
    float getHumedad() const;

    // Retorna la presión de vapor de agua saturado (mmHg) según la ecuación de Antoine
    float getPsat() const;

    // Retorna la presión de vapor de agua ambiental real (mmHg)
    float getPH2OAmb() const;

    // Retorna el Factor BTPS calculado
    float getBTPSFactor() const;

    // Aplica el factor BTPS a un volumen no corregido (ATPS)
    float Volumen(float volumeATPS) const;

private:
    uint8_t _pin;
    uint8_t _type;
    DHT _dht;

    float _pb;           // Presión barométrica (mmHg)
    float _tAmb;         // Temperatura ambiental (°C)
    float _hr;           // Humedad relativa (%)
    float _factorBTPS;   // Factor BTPS calculado
    bool _lastReadValid;

    // Función interna para calcular Antoine y BTPS
    void updateBTPSFactor();
};

#endif // BTPS_SENSOR_H