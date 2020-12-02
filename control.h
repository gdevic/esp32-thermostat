#include <Arduino.h>

// Define function on the PCF8574 gpio pins
#define PIN_MASTER  (1 << 0)
#define PIN_FAN     (1 << 1)
#define PIN_COOL    (1 << 2)
#define PIN_HEAT    (1 << 3)

class CControl
{
public:
    CControl();
    void set_fan(bool on);
    void set_cool(bool on);
    void set_heat(bool on);

private:
    void set_gpio_ext(bool fan, bool cool, bool heat);

private:
    uint32_t m_seconds;
};
