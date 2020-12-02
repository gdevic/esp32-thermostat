#include "thermostat.h"

#define WITHIN(f,min,max) (((f) >= (min)) && ((f) <= (max)))

CThermostat::CThermostat()
{
    reset();
}

void CThermostat::reset()
{
    m_cool_safety = 85;
    m_cool_set = 72;
    m_heat_set = 68;
    m_heat_safety = 60;

    m_cool_region = 'X';
    m_cool_prev_region = 'X';
    m_heat_region = 'X';
    m_heat_prev_region = 'X';

    hysteresis();
}

// Adjust the "off" hysteresis values tied to fixed "on" temperature points
void CThermostat::hysteresis()
{
    m_cool_safety_off = m_cool_safety - COOLING_SAFETY_HYSTERESIS;
    m_cool_off = m_cool_set - COOLING_HYSTERESIS;
    m_heat_safety_off = m_heat_safety + HEATING_SAFETY_HYSTERESIS;
    m_heat_off = m_heat_set + HEATING_HYSTERESIS;
}

// Set the cooling/heating region codes for a given temperature
void CThermostat::compute_regions(float f)
{
    // Always sanity check that our region boundaries are correctly sorted
    if ((m_cool_off < m_cool_set) &&
        (m_cool_set < m_cool_safety_off) &&
        (m_cool_safety_off < m_cool_safety))
    {
        if (f < m_cool_off)
            m_cool_region = 'A';
        else if (f < m_cool_set)
            m_cool_region = 'B';
        else if (f < m_cool_safety_off)
            m_cool_region = 'C';
        else if (f < m_cool_safety)
            m_cool_region = 'D';
        else
            m_cool_region = 'E';
    }
    else
        m_cool_region = 'X';

    if ((m_heat_off > m_heat_set) &&
        (m_heat_set > m_heat_safety_off) &&
        (m_heat_safety_off > m_heat_safety))
    {
        if (f > m_heat_off)
            m_heat_region = 'A';
        else if (f > m_heat_set)
            m_heat_region = 'B';
        else if (f > m_heat_safety_off)
            m_heat_region = 'C';
        else if (f > m_heat_safety)
            m_heat_region = 'D';
        else
            m_heat_region = 'E';
    }
    else
        m_heat_region = 'X';
}

bool CThermostat::set_cool(uint f)
{
    if (WITHIN(f, COOLING_MIN_TEMP, COOLING_MAX_TEMP))
    {
        m_cool_set = f;
        m_cool_off = f - COOLING_HYSTERESIS;

        // TODO store these valures in [NV]
        return true;
    }
    return false;
}

bool CThermostat::set_heat(uint f)
{
    if (WITHIN(f, HEATING_MIN_TEMP, HEATING_MAX_TEMP))
    if ((f > 60) && (f < 90))
    {
        m_heat_set = f;
        m_heat_off = f + HEATING_HYSTERESIS;

        // TODO store these valures in [NV]
        return true;
    }
    return false;
}

bool CThermostat::set_cool_safety(uint f)
{
    if (WITHIN(f, COOLING_MIN_TEMP - COOLING_SAFETY_HYSTERESIS, COOLING_MAX_TEMP + COOLING_SAFETY_HYSTERESIS))
    {
        m_cool_safety = f;
        m_cool_safety_off = f - COOLING_SAFETY_HYSTERESIS;

        // TODO store these valures in [NV]
        return true;
    }
    return false;
}

bool CThermostat::set_heat_safety(uint f)
{
    if (WITHIN(f, HEATING_MIN_TEMP - HEATING_SAFETY_HYSTERESIS, HEATING_MAX_TEMP + HEATING_SAFETY_HYSTERESIS))
    if ((f > 55) && (f < 95))
    {
        m_heat_safety = f;
        m_heat_safety_off = f + HEATING_SAFETY_HYSTERESIS;

        // TODO store these valures in [NV]
        return true;
    }
    return false;
}
