#include <Arduino.h>

// The version string shown in stats. Nothing depends on it and it is used only to confirm newly flashed firmware.
#define FIRMWARE_VERSION "1.00"

// To simply test the code, define TEST
//#define TEST

// Period, in seconds, to read sensors
#define PERIOD_5_SEC  5

struct StationData
{
    // Variables marked with [NV] are held in the non-volatile memory using Preferences
    String id;          // [NV] Station identification string, held in the non-volatile memory
    String tag;         // [NV] Station description or a tag, held in the non-volatile memory
    float temp_c;       // Current temperature in "C"
    float temp_f;       // Current temperature in "F"

    uint32_t seconds;   // Uptime seconds counter (shown as "uptime" in web reports)
    uint32_t errors;    // Bitfield containing possible errors
};

// Possible errors
#define ERROR_LCD_INIT   0x0001

// Specific to ESP32's FreeRTOS port
// Arduino loop is running on core 1 and priority 1, https://techtutorialsx.com/2017/05/09/esp32-running-code-on-a-specific-core
#define PRO_CPU 0
#define APP_CPU 1

extern StationData wdata;

// From main.cpp
void pref_set(const char* name, uint32_t value);
void pref_set(const char* name, float value);
void pref_set(const char* name, String value);

// From webserver.cpp
void webserver_set_response();
void setup_wifi();
void setup_webserver();
void wifi_check_loop();
