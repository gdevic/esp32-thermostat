#include "main.h"
#include <Preferences.h>

WeatherData wdata = {};

static Preferences pref;

// Set a preference string value pairs, we are using int, float and string variants
void pref_set(const char* name, uint32_t value)
{
    pref.begin("wd", false);
    pref.putUInt(name, value);
    pref.end();
}

void pref_set(const char* name, float value)
{
    pref.begin("wd", false);
    pref.putFloat(name, value);
    pref.end();
}

void pref_set(const char* name, String value)
{
    pref.begin("wd", false);
    pref.putString(name, value);
    pref.end();
}

static void vTask_read_sensors(void *p)
{
    // Make this task sleep and awake once a second
    const TickType_t xFrequency = 1 * 1000 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        // Wait for the next cycle first, all calculation below will be triggered after the initial period passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        wdata.seconds++;

        // Once every 5 seconds, read all sensors and recalculate relevant data
        if ((wdata.seconds % PERIOD_5_SEC) == 0)
        {
            // Fill up data fields with the sensors' (and computed) data

            // Read temperature sensor
            // TODO

#ifdef TEST
            Serial.print(wdata.seconds);
            Serial.print(": ");
            Serial.print(wdata.temp_c);
            Serial.print(" C ");
            Serial.print(wdata.temp_f);
            Serial.print(" F ");
            Serial.println("");
#endif // TEST
        }
        // At the end, preset various response strings that the server should give out. This will happen once a second,
        // whether we have any new data or not.
        webserver_set_response();
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100);

    // Read the initial values stored in the NVM
    pref.begin("wd", true);
    wdata.id = pref.getString("id", "Thermostat");
    wdata.tag = pref.getString("tag", "Smart Thermostat station");
    pref.end();

    setup_wifi();
    setup_webserver();

    // Arduino loop is running on core 1 and priority 1
    // https://techtutorialsx.com/2017/05/09/esp32-running-code-on-a-specific-core
    xTaskCreatePinnedToCore(
        vTask_read_sensors, // Task function
        "task_sensors",     // String with name of the task
        2048,               // Stack size in bytes
        &wdata,             // Parameter passed as input to the task
        1,                  // Priority of the task
        NULL,               // Task handle
        1);                 // Core where the task should run (user program core)
}

void loop()
{
    wifi_check_loop();
}
