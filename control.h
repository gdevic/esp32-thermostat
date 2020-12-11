#include <Arduino.h>

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

private:
    uint8_t m_relays { 0xFF }; // Cached state of the relay control byte

    uint32_t m_fan_counter { 0 };
    uint8_t  m_fan_mode    { 0 };
    uint32_t m_ac_counter  { 0 };
    uint8_t  m_ac_mode     { 0 };
};

extern CControl control;
