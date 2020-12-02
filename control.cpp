#include "control.h"
#include <Wire.h>

// PCF8574
#define GPIO_EXT_ADDR 0x20

CControl::CControl()
{
    // Open all relays, turning off fan/cool/heat
    Wire.beginTransmission(GPIO_EXT_ADDR);
    Wire.write(0xFF);
    Wire.endTransmission();
}

void CControl::set_fan(bool on)
{
}

void CControl::set_cool(bool on)
{
}

void CControl::set_heat(bool on)
{
}

void CControl::set_gpio_ext(bool fan, bool cool, bool heat)
{
#if 0
    uint8_t data = 0xFF;
    if (r1) data &= 0xFE;
    if (r2) data &= 0xFD;
    if (r3) data &= 0xFB;
    if (r4) data &= 0xF7;
    Wire.beginTransmission(GPIO_EXT_ADDR);
    Wire.write(data);
    Wire.endTransmission();
#endif
}
