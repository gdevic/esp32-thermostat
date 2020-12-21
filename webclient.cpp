#include "main.h"
#include "ArduinoJson.h"
#include <WiFi.h>

// Reading a temperature from an external sensor
// This is normally one of my other WiFi sensors that publish its data via json http response
// Needs ArduinoJson library by Benoit Blanchon

static StaticJsonDocument<512> doc;

// Returns false if connection to the external server failed
static bool get_external_temp()
{
    // Use WiFiClient class to create TCP connections
    WiFiClient client;

    if (!client.connect(wdata.ext_server.c_str(), 80))
    {
        Serial.println("Unable to connect");
        return false;
    }

    // This will send the request to the server
    client.println("GET /json HTTP/1.0");
    client.println();
    unsigned long timeout = millis();
    while (client.available() == 0)
    {
        if (millis() - timeout > 5000)
        {
            Serial.println("Client timed out");
            client.stop();
            return false;
        }
    }

    // Read a line of the reply from server which should be a line of json data
    while(client.available())
    {
        String line = client.readStringUntil('\n');
        line.trim();
        const char *json = line.c_str();
        if (json[0] != '{')
            continue; // Parse only JSON lines
        DeserializationError error = deserializeJson(doc, json);
        if (error)
        {
            wdata.status |= STATUS_EXT_JSON_ERROR;
            wdata.ext_valid = false;
        }
        else
        {
            float temp_f = doc["temp_f"];
            if ((temp_f >= 60.0) && (temp_f <= 90.0))
            {
                wdata.ext_temp_f = temp_f;
                wdata.ext_temp_c = (temp_f - 32.0) * 5.0 / 9.0;
                wdata.ext_valid = true;
            }
            else
            {
                wdata.status |= STATUS_EXT_TEMP_ERROR;
                wdata.ext_valid = false;
            }
        }
    }
    return true;
}

void vTask_ext_temp(void *p)
{
    while(true)
    {
        // Read external temperature sensor only if it is enabled (ext_read_sec > 0)
        if (wdata.ext_read_sec)
        {
            int retries = 5; // Retry connecting to the external server several times before giving up
            while (retries && (get_external_temp() == false))
            {
                vTaskDelay(5 * 1000 / portTICK_PERIOD_MS); // Delay 5 seconds before retrying the request
                retries--;
            }
            if (retries == 0)
                wdata.ext_valid = false;
        }
        else
            wdata.ext_valid = false;

        // Wait for the next time we need to read the external sensor
        // Either wait for 1 sec (if the ext_read_sec is 0) or up to a minute
        vTaskDelay(constrain(wdata.ext_read_sec, 1, 60) * 1000 / portTICK_PERIOD_MS);

        wdata.task_ext = uxTaskGetStackHighWaterMark(nullptr);
    }
}
