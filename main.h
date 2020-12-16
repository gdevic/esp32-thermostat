#include <Arduino.h>

// The version string shown in stats. Nothing depends on it and it is used only to confirm newly flashed firmware.
#define FIRMWARE_VERSION "1.02"

// Set to 1 to use temperature model for testing (insted of the real temperature value)
#define USE_MODEL 0

struct StationData
{
    // Variables marked with [NV] are held in the non-volatile memory using Preferences
    String id;          // [NV] Station identification string, held in the non-volatile memory
    String tag;         // [NV] Station description or a tag, held in the non-volatile memory
    float temp_c;       // Current temperature in "C"
    float temp_f;       // Current temperature in "F"
    bool temp_valid;    // True if termperature reading is correct
    float ext_temp_c;   // External sensor temperature in "C"
    float ext_temp_f;   // External sensor temperature in "F"
    bool ext_valid;     // True if external sensor termperature reading is correct
    String ext_server;  // [NV] Server/path name of the external temperature sensor
    uint32_t ext_read_sec;// [NV] Period in seconds to read external temperature sensor, 0 to disable reading

    // Returns the effective temperature to be used for thermostat operation, display and json output
    // Using this method abstracts the internal sensor from the external sensor override
    inline float get_temp_c() { return ext_valid ? ext_temp_c : temp_c; }
    inline float get_temp_f() { return ext_valid ? ext_temp_f : temp_f; }
    inline bool get_temp_valid() { return ext_valid ? true : temp_valid; }

    uint8_t fan_mode;   // [NV] Fan operation mode: OFF, ON, CYC
#define FAN_MODE_OFF  0
#define FAN_MODE_ON   1
#define FAN_MODE_CYC  2
#define FAN_MODE_AUX  3 // Used to change the A/C mode in UI, otherwise equivalent to FAN_MODE_OFF
#define FAN_MODE_LAST FAN_MODE_AUX

    uint8_t ac_mode;    // [NV] A/C mode
#define AC_MODE_OFF   0
#define AC_MODE_COOL  1
#define AC_MODE_HEAT  2
#define AC_MODE_AUTO  3 // TODO
#define AC_MODE_LAST  AC_MODE_HEAT

    uint8_t cool_to;    // [NV] Temperature cooling target
    uint8_t heat_to;    // [NV] Temperature heating target

    uint32_t seconds;   // Uptime seconds counter (shown as "uptime" in web reports)
    uint32_t filter_sec;// [NV] Total A/C + fan on time in seconds
    uint32_t cool_sec;  // [NV] Total A/C cooling time in seconds
    uint32_t heat_sec;  // [NV] Total A/C heating time in seconds
    uint32_t status;    // Bitfield containing possible errors and status bits
    uint8_t relays { 0xFF }; // Effective state of the relay control byte
    bool gpio23;        // GPIO23 strap value
};

extern StationData wdata;

// Possible errors
#define STATUS_LCD_INIT_ERROR   0x0001 // LCD was not able to initialize
#define STATUS_EXT_GET_ERROR    0x0002 // Http GET error when reading external sensor
#define STATUS_EXT_JSON_ERROR   0x0004 // Error parsing external temperature json response
#define STATUS_EXT_TEMP_ERROR   0x0008 // Error reading external temperature sensor

// Specific to ESP32's FreeRTOS port, Arduino loop is running on core 1 and priority 1
#define PRO_CPU 0
#define APP_CPU 1

// Type of a message sent to the I2C task
typedef struct
{
    portBASE_TYPE xMessageType;
    uint8_t bMessage;
} xI2CMessage;

#define I2C_READ_TEMP    0
#define I2C_LCD_INIT     1
#define I2C_PRINT_FAN    2
#define I2C_PRINT_AC     3
#define I2C_PRINT_TARGET 4
#define I2C_SET_RELAYS   5

extern QueueHandle_t xI2CQueue; // The queue of messages to the I2C task

// From main.cpp
void pref_set(const char* name, uint8_t value);
void pref_set(const char* name, uint32_t value);
void pref_set(const char* name, float value);
void pref_set(const char* name, String value);

// From webserver.cpp
void webserver_set_response();
void setup_wifi();
void setup_webserver();
void wifi_check_loop();

// From webclient.cpp
void get_external_temp();
