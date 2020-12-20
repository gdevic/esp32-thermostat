#include "main.h"
#include "ArduinoJson.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Reading a temperature from an external sensor
// This is normally one of my other WiFi sensors that publish its data via json http response
// Needs ArduinoJson library by Benoit Blanchon

static StaticJsonDocument<512> doc;

void get_external_temp()
{
    HTTPClient http;

    http.begin("http://" + wdata.ext_server);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
        const char *json = http.getString().c_str();
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
    else
    {
        wdata.status |= STATUS_EXT_GET_ERROR;
        wdata.ext_valid = false;
    }

    http.end();
}
