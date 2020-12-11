#include <Arduino.h>

class CControl
{
public:
    CControl();
    void tick();
    void set_fan_mode(uint8_t fan_mode);
    void set_fan_mode(String) {};

private:
    uint8_t m_relays { 0xFF }; // Cached state of the relay control byte
    uint32_t m_fan_counter { 0 };
    uint8_t m_fan_mode;
};

extern CControl control;
