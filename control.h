#include <Arduino.h>

// Define function on the PCF8574 gpio pins
#define PIN_FAN     (1 << 0)
#define PIN_COOL    (1 << 1)
#define PIN_HEAT    (1 << 2)
#define PIN_MASTER  (1 << 3)

class CControl
{
public:
    CControl();
    void tick();
    void set_fan_mode(uint8_t mode);
    void set_ac_mode(uint8_t mode);
    void set_cool_to(uint8_t temp);
    void set_heat_to(uint8_t temp);
    bool accounting(uint8_t relays);
    float model_get_temperature();

private:
    uint8_t m_relays { 0xFF }; // Cached state of the relay control byte

    uint32_t m_fan_counter { 0 };
    uint8_t  m_fan_mode    { 0 };
    uint32_t m_ac_counter  { 0 };
    uint8_t  m_ac_mode     { 0 };
private:
    // Implements a simple temperature model to test the thermostat
    float m_tcurrent { 78.0 }; // Starting temperature
    const float m_tdambience { +0.1 }; // Ambience change of temperature
    const float m_tdheating  { 0.3 }; // Heating efficiency
    const float m_tdcooling  { 0.3 }; // Cooling efficiency
};

extern CControl control;
