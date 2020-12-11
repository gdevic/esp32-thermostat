#include "control.h"
#include "main.h"

// Define function on the PCF8574 gpio pins
#define PIN_FAN     (1 << 0)
#define PIN_COOL    (1 << 1)
#define PIN_HEAT    (1 << 2)
#define PIN_MASTER  (1 << 3)

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
    }
}

CControl::CControl()
{
    m_fan_mode = wdata.fan_mode;

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
            if (m_fan_mode == FAN_MODE_ON)
                relays &= ~PIN_FAN;
            if (m_fan_mode == FAN_MODE_CYC)
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

    if (relays != m_relays)
    {
        m_relays = relays;

        xI2CMessage xMessage { I2C_SET_RELAYS, relays };
        xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
    }
}

void CControl::set_fan_mode(uint8_t fan_mode)
{
    if (fan_mode > FAN_MODE_LAST)
        fan_mode = 0;
    wdata.fan_mode = fan_mode;

    // Display the new fan mode on the LCD
    xI2CMessage xMessage;
    xMessage.xMessageType = I2C_PRINT_FAN;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

    // Initiate fan change
    m_fan_counter = 5; // 5 sec to fan change
    m_fan_mode = fan_mode;
}
