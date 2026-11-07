#include "BTPSSensor.h"

BTPSSensor::BTPSSensor(uint8_t pin, uint8_t type, float presionBarometrica)
    : _pin(pin), _type(type), _dht(pin, type), _pb(presionBarometrica),
      _tAmb(20.0), _hr(50.0), _factorBTPS(1.0), _lastReadValid(false) {}

void BTPSSensor::begin() {
    _dht.begin();
}
// Dos funciones principales que hacen todos los cálculos: 
// lee los valores y decide si son válidos. Si lo son llama a la función que hace los cálculos. 
bool BTPSSensor::leer() {
    float temp = _dht.readTemperature();
    float hum = _dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
        _lastReadValid = false;
        return false;
    }

    _tAmb = temp;
    _hr = hum;
    _lastReadValid = true;

    updateBTPSFactor();
    return true;
}
//se hace automático cada vez que hay valores válidos en read, es el que calcula el valor BTPS. 
void BTPSSensor::updateBTPSFactor() {
    // 1. Presión de vapor de agua saturado a Tamb (mmHg) usando Antoine
    float exponente = 5.053988 - (1647.6 / (_tAmb + 226.0));
    float pSat = 760.0 * pow(10.0, exponente);

    // 2. Presión real de vapor de agua ambiental
    float pH2O_Amb = pSat * (_hr / 100.0);

    // 3. Cálculo del Factor BTPS
    float termTemp = (273.15 + 37.0) / (273.15 + _tAmb);
    float termPresion = (_pb - pH2O_Amb) / (_pb - 47.0);

    _factorBTPS = termTemp * termPresion;
}

// Todas estas funciones nos sirven para obtener los datos ya procesados sin volver a llamar al sensor. 
float BTPSSensor::getTemperatura() const {
    return _tAmb;
}

float BTPSSensor::getHumedad() const {
    return _hr;
}

float BTPSSensor::getPsat() const {
    float exponente = 5.053988 - (1647.6 / (_tAmb + 226.0));
    return 760.0 * pow(10.0, exponente);
}

float BTPSSensor::getPH2OAmb() const {
    return getPsat() * (_hr / 100.0);
}

float BTPSSensor::getBTPSFactor() const {
    return _factorBTPS;
}

float BTPSSensor::Volumen(float volumeATPS) const {
    return volumeATPS * _factorBTPS;
}