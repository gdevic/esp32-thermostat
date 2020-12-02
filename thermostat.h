#include <Arduino.h>

// Temperature regions explained, in increasing temperatures:
//     COOLING REGIONS
// E
// ------> cool_safety
// D                        | Cooling safety hysteresis
// ------> cool_safety_off
// C
// ------> cool_set
// B                        | Cooling hysteresis
// ------> cool_off
// A
//     HEATING REGIONS
// A
// ------> heat_off
// B                        | Heating hysteresis
// ------> heat_set
// C
// ------> heat_safety_off
// D                        | Heating safety hysteresis
// ------> heat_safety
// E

#define COOLING_SAFETY_HYSTERESIS  5.0
#define COOLING_HYSTERESIS         1.5
#define HEATING_SAFETY_HYSTERESIS  5.0
#define HEATING_HYSTERESIS         1.5

#define COOLING_MIN_TEMP  60
#define COOLING_MAX_TEMP  80
#define HEATING_MIN_TEMP  60
#define HEATING_MAX_TEMP  80

class CThermostat
{
public:
    CThermostat();
    bool set_cool(uint f);
    bool set_heat(uint f);
    bool set_cool_safety(uint f);
    bool set_heat_safety(uint f);

private:
    void reset();
    void hysteresis();
    void compute_regions(float f);

private:
    // All temperatures are in degrees Fahrenheit
    float m_cool_safety;      // [NV]
    float m_cool_safety_off;  //
    float m_cool_set;         // [NV]
    float m_cool_off;         //

    float m_heat_off;         //
    float m_heat_set;         // [NV]
    float m_heat_safety_off;  //
    float m_heat_safety;      // [NV]

    char m_cool_region;
    char m_cool_prev_region;
    char m_heat_region;
    char m_heat_prev_region;
};
