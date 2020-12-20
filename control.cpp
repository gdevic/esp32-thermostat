#include "control.h"
#include "main.h"

static void vTask_control(void *p)
{
    // Make this task sleep and awake once a second
    const TickType_t xFrequency = 1 * 1000 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while(true)
    {
        // Once a second call the control class' tick method
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        static_cast<CControl *>(p)->tick();

        wdata.task_control = uxTaskGetStackHighWaterMark(nullptr);
    }
}

CControl::CControl()
{
    xTaskCreatePinnedToCore(
        vTask_control,       // Task function
        "task_control",      // Name of the task
        2048,                // Stack size in bytes
        this,                // Parameter passed as input to the task
        tskIDLE_PRIORITY + 2,// Priority of the task
        nullptr,             // Task handle
        APP_CPU);            // Core where the task should run (user program core)
}

void CControl::tick()
{
    uint8_t relays = m_relays;

    if (m_fan_counter)
    {
        m_fan_counter--;
        if (m_fan_counter == 0)
        {
            if (m_fan_mode == FAN_MODE_OFF)
                relays |= PIN_FAN;
            else if (m_fan_mode == FAN_MODE_ON)
                relays &= ~PIN_FAN;
            else if (m_fan_mode == FAN_MODE_CYC)
            {
                // Turn the fan on for 15 min each hour
                if (relays & PIN_FAN) // Fan was off
                {
                    relays &= ~PIN_FAN; // Turn it on
                    m_fan_counter = 15 * 60; // and keep it on for 15 min
                }
                else // Fan was on
                {
                    relays |= PIN_FAN; // Turn it off
                    m_fan_counter = 45 * 60; // and keep it off for 45 min
                }
            }
        }
    }

    if (m_ac_counter && wdata.temp_valid) // Control A/C only when the temperature readings are valid
    {
        m_ac_counter--;
        if (m_ac_counter == 0)
        {
            if (m_ac_mode == AC_MODE_OFF)
                relays |= (PIN_COOL | PIN_HEAT);
            else if (m_ac_mode == AC_MODE_COOL)
            {
                bool is_cooling = ((relays & PIN_COOL) == 0);

                if (is_cooling && (wdata.get_temp_f() < (float(wdata.cool_to) - wdata.hyst_release)))
                    relays |= PIN_COOL;

                if (!is_cooling && (wdata.get_temp_f() > (float(wdata.cool_to) + wdata.hyst_trigger)))
                    relays &= ~PIN_COOL;
            }
            else if (m_ac_mode == AC_MODE_HEAT)
            {
                bool is_heating = ((relays & PIN_HEAT) == 0);

                if (is_heating && (wdata.get_temp_f() > (float(wdata.heat_to) + wdata.hyst_release)))
                    relays |= PIN_HEAT;

                if (!is_heating && (wdata.get_temp_f() <  (float(wdata.heat_to) - wdata.hyst_trigger)))
                    relays &= ~PIN_HEAT;
            }

            // Re-evaluate temperature every 30 sec
            m_ac_counter = 30;
        }
    }

    // Make sure both heating and cooling are not on at the same time
    if ((relays & (PIN_COOL | PIN_HEAT)) == 0)
        relays |= (PIN_COOL | PIN_HEAT); // Turn off both cooling and heating in that case

    if (relays != m_relays)
    {
        m_relays = relays;

        // Make sure the fan is on for either heating or cooling
        if ((relays & (PIN_COOL | PIN_HEAT)) != (PIN_COOL | PIN_HEAT))
            relays &= ~PIN_FAN; // Turn on fan

        // Turn on master relay when any other is active, or turn it off otherwise
        if ((relays & (PIN_FAN | PIN_COOL | PIN_HEAT)) == (PIN_FAN | PIN_COOL | PIN_HEAT))
            relays |= PIN_MASTER; // Turn off master if the other three relays are also off
        else
            relays &= ~PIN_MASTER; // Turn on master otherwise

        xI2CMessage xMessage { I2C_SET_RELAYS, relays };
        xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
    }
}

void CControl::set_fan_mode(uint8_t mode)
{
    if (mode > FAN_MODE_LAST)
        mode = FAN_MODE_OFF;

    // Set the new value into an NV variable
    pref_set("fan_mode", mode);

    // Display the current fan mode on the LCD
    xI2CMessage xMessage;
    xMessage.xMessageType = I2C_PRINT_FAN;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

    // Initiate fan change
    m_fan_counter = 5; // 5 sec to fan change
    m_fan_mode = mode;
    wdata.fan_mode = mode;
}

void CControl::set_ac_mode(uint8_t mode)
{
    if (mode > AC_MODE_LAST)
        mode = AC_MODE_OFF;

    // Set the new value into an NV variable
    pref_set("ac_mode", mode);

    // Display the current A/C mode on the LCD
    xI2CMessage xMessage;
    xMessage.xMessageType = I2C_PRINT_AC;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

    // Initiate A/C change
    m_ac_counter = 30; // 30 sec to A/C mode change
    m_ac_mode = mode;
    wdata.ac_mode = mode;
}

void CControl::set_cool_to(uint8_t temp)
{
    temp = constrain(temp, 60, 90);

    // Set the new value into an NV variable
    pref_set("cool_to", temp);

    // Display the current A/C target on the LCD
    xI2CMessage xMessage;
    xMessage.xMessageType = I2C_PRINT_TARGET;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

    // Initiate A/C change
    m_ac_counter = 30; // 30 sec to A/C mode change
    wdata.cool_to = temp;
}

void CControl::set_heat_to(uint8_t temp)
{
    temp = constrain(temp, 60, 90);

    // Set the new value into an NV variable
    pref_set("heat_to", temp);

    // Display the current A/C target on the LCD
    xI2CMessage xMessage;
    xMessage.xMessageType = I2C_PRINT_TARGET;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

    // Initiate A/C change
    m_ac_counter = 30; // 30 sec to A/C mode change
    wdata.heat_to = temp;
}

// Based on the effective relay configuration, add fan and A/C usage
// Returns true if any of the counters have incremented
bool CControl::accounting(uint8_t relays)
{
    bool changed = false;
    if (~relays & PIN_MASTER) // Do accounting only if the master relay is on
    {
        if (~relays & PIN_FAN)
            wdata.filter_sec++, changed = true;
        if (~relays & PIN_COOL)
            wdata.cool_sec++, changed = true;
        if (~relays & PIN_HEAT)
            wdata.heat_sec++, changed = true;
    }
    return changed;
}

// Implements a simple temperature model to test the thermostat
// Called every 5 sec when USE_MODEL is 1
float CControl::model_get_temperature()
{
    // Room is naturally getting cooler or hotter
    m_tcurrent += m_tdambience;

    if ((~wdata.relays & PIN_MASTER) && (~wdata.relays & PIN_HEAT))
    {
        m_tcurrent += m_tdheating;
    }
    if ((~wdata.relays & PIN_MASTER) && (~wdata.relays & PIN_COOL))
    {
        m_tcurrent -= m_tdcooling;
    }
    return (m_tcurrent - 32.0) * (5.0/9.0); // Return temperature in C
}
