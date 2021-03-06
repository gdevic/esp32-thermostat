#include "main.h"
#include <Preferences.h>
#include "control.h"

StationData wdata = {};
CControl control;
Preferences pref;

//-------------------------------------- DS18B20 -------------------------------------------
// Requires library: "DallasTemperature" by Miles Burton
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 5
// Setup a oneWire instance to communicate with any OneWire device
OneWire oneWire(ONE_WIRE_BUS);
// Pass oneWire reference to DallasTemperature library
DallasTemperature sensors(&oneWire);
//------------------------------------------------------------------------------------------


//-------------------------------------- PCF8574 -------------------------------------------
#define GPIO_EXT_ADDR 0x20
//------------------------------------------------------------------------------------------


//-------------------------------------- LCD16x2 -------------------------------------------
// Requires library: "LiquidCrystal_PCF8574"
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>
LiquidCrystal_PCF8574 lcd(0x27); // Set the I2C LCD address

// Custom char generator: https://maxpromer.github.io/LCD-Character-Creator
static int customCharSelect[] = { B00000, B00000, B00000, B01101, B11010, B00000, B01101, B11010 };
static int customCharDown[]   = { B00000, B00000, B11111, B11111, B01110, B01110, B00100, B00100 };
static int customCharUp[]     = { B00000, B00000, B00100, B00100, B01110, B01110, B11111, B11111 };
static int customCharFan1[]   = { B00000, B00000, B00000, B01100, B00101, B11011, B10100, B00110 };
static int customCharFan2[]   = { B00000, B00000, B00000, B00010, B11010, B00100, B01011, B01000 };

#define CHAR_SELECT  0
#define CHAR_DOWN    1
#define CHAR_UP      2
#define CHAR_FAN1    3
#define CHAR_FAN2    4

// The maximum number of messages that can be waiting for I2C task at any one time
#define I2C_QUEUE_SIZE 5
QueueHandle_t xI2CQueue; // The queue used to send messages to the I2C task
//------------------------------------------------------------------------------------------
void setup_i2c()
{
    xI2CMessage xMessage;
    // Create the queue used by the I2C task
    xI2CQueue = xQueueCreate(I2C_QUEUE_SIZE, sizeof(xI2CMessage));

    xMessage.xMessageType = I2C_LCD_INIT;
    xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
}

static void lcd_init()
{
    Wire.begin();
    Wire.beginTransmission(0x27);
    if (Wire.endTransmission(true))
        wdata.status |= STATUS_LCD_INIT_ERROR;

    lcd.begin(16, 2);
    lcd.createChar(CHAR_SELECT, customCharSelect);
    lcd.createChar(CHAR_DOWN, customCharDown);
    lcd.createChar(CHAR_UP, customCharUp);
    lcd.createChar(CHAR_FAN1, customCharFan1);
    lcd.createChar(CHAR_FAN2, customCharFan2);
    lcd.clear();
    lcd.setBacklight(1);

    lcd.setCursor(0, 0);
    lcd.print("Init");

    lcd.setCursor(9, 1);
    lcd.write(uint8_t(CHAR_SELECT));
    lcd.setCursor(12, 1);
    lcd.write(uint8_t(CHAR_DOWN));
    lcd.setCursor(15, 1);
    lcd.write(uint8_t(CHAR_UP));
}

//-------------------------------------- BUTTONS -------------------------------------------
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#define GPIO_INPUT_IO_0 GPIO_NUM_15
#define GPIO_INPUT_IO_1 GPIO_NUM_16
#define GPIO_INPUT_IO_2 GPIO_NUM_17
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1) | (1ULL<<GPIO_INPUT_IO_2))
#define ESP_INTR_FLAG_DEFAULT 0
static xQueueHandle gpio_evt_queue = nullptr;
static bool buttons[3] {};
static uint32_t option_mode_counter {}; // Seconds to switch off from the option mode selector
#define BUTTON_INDEX_OPTION  0
#define BUTTON_INDEX_DOWN    1
#define BUTTON_INDEX_UP      2
//------------------------------------------------------------------------------------------
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t button_index = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &button_index, nullptr);

    buttons[0] = !gpio_get_level(gpio_num_t(GPIO_INPUT_IO_0));
    buttons[1] = !gpio_get_level(gpio_num_t(GPIO_INPUT_IO_1));
    buttons[2] = !gpio_get_level(gpio_num_t(GPIO_INPUT_IO_2));
}

static void vTask_gpio(void* arg)
{
    xI2CMessage xMessage;
    uint32_t button_index;

    while(true)
    {
        while(xQueueReceive(gpio_evt_queue, &button_index, portMAX_DELAY) != pdPASS);

        bool level = buttons[button_index];

        if (level) // Consider buttons only on a press transition
        {
            if (button_index == BUTTON_INDEX_OPTION)
            {
                wdata.option = wdata.option + 1;
                if (wdata.option > OPTION_LAST)
                    wdata.option = OPTION_OFF;
                option_mode_counter = OPTION_MODE_COUNTER_SEC;
            }
            else // When the options are setting up fan, up or down buttons change fan mode
            if (wdata.option == OPTION_FAN)
            {
                // To make the physical UI as simple as possible, we limit the fan options to ON/OFF only
                if (wdata.fan_mode == OPTION_OFF)
                    control.set_fan_mode(FAN_MODE_ON); // OFF goes to ON
                else
                    control.set_fan_mode(FAN_MODE_OFF); // while anything else goes to OFF
                option_mode_counter = OPTION_MODE_COUNTER_SEC;
            }
            else // When the options are setting the A/C mode, up or down buttons change it
            if (wdata.option == OPTION_AC)
            {
                control.set_ac_mode(wdata.ac_mode + 1);
                option_mode_counter = OPTION_MODE_COUNTER_SEC;
            }
            else // Otherwise, up or down buttons, as expected, change the temperature
            {
                if (buttons[1] || buttons[2]) // Temperature up or down
                {
                    int delta = (button_index == BUTTON_INDEX_UP) ? +1 : -1;
                    if (wdata.ac_mode == AC_MODE_COOL)
                        control.set_cool_to(wdata.cool_to + delta);
                    if (wdata.ac_mode == AC_MODE_HEAT)
                        control.set_heat_to(wdata.heat_to + delta);
                    if (wdata.ac_mode == AC_MODE_AUTO)
                        ; // TODO
                }
            }

            xI2CMessage xMessage;
            xMessage.xMessageType = I2C_PRINT_STATUS;
            xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
        }

        wdata.task_gpio = uxTaskGetStackHighWaterMark(nullptr);
    }
}

void setup_sw()
{
    gpio_config_t io_conf
    {
        GPIO_INPUT_PIN_SEL,
        GPIO_MODE_INPUT,
        GPIO_PULLUP_ENABLE,
        GPIO_PULLDOWN_DISABLE,
        GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    // Create a queue to handle gpio events from isr
    gpio_evt_queue = xQueueCreate(5, sizeof(uint32_t));
    // Start gpio task
    xTaskCreate(vTask_gpio, "task_gpio", 2048, nullptr, tskIDLE_PRIORITY + 4, nullptr);
    // Install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // Hook isr handlers for specific gpio pins
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) BUTTON_INDEX_OPTION);
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) BUTTON_INDEX_DOWN);
    gpio_isr_handler_add(GPIO_INPUT_IO_2, gpio_isr_handler, (void*) BUTTON_INDEX_UP);
}

// Set a preference string value pairs for various data types
void pref_set(const char* name, bool value)
{
    pref.begin("wd", false);
    pref.putBool(name, value);
    pref.end();
}

void pref_set(const char* name, uint8_t value)
{
    pref.begin("wd", false);
    pref.putUChar(name, value);
    pref.end();
}

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

static void vTask_1s_tick(void *p)
{
    // Make this task sleep and awake once a second
    const TickType_t xTimePeriod = 1 * 1000 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    xI2CMessage xMessage;
    bool changed = false; // Commit updates to filter counters only on change

    while(true)
    {
        // Wait for the next cycle first, all calculation below will be triggered after the initial period passed
        vTaskDelayUntil(&xLastWakeTime, xTimePeriod);

        // Once every 30 seconds, read the temperature sensor
        if ((wdata.seconds % 30) == 0)
        {
            xMessage.xMessageType = I2C_READ_TEMP;
            xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
        }

        // Every second, add up filter, cooling and heating periods
        changed |= control.accounting(wdata.relays);
        // Once a minute, if changed, commit those accounting values to NV
        if (changed && ((wdata.seconds % 60) == 0))
        {
            pref.begin("wd", false);
            pref.putUInt("filter_sec", wdata.filter_sec);
            pref.putUInt("cool_sec", wdata.cool_sec);
            pref.putUInt("heat_sec", wdata.heat_sec);
            pref.end();
            changed = false;
        }

        // After a few secs of inactivity in the option mode, switch back to normal operation
        if (option_mode_counter)
        {
            option_mode_counter--;
            if (option_mode_counter == 0)
            {
                wdata.option = OPTION_OFF;

                xI2CMessage xMessage;
                xMessage.xMessageType = I2C_PRINT_STATUS;
                xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
            }
        }

        // If the TIMED fan mode completed, or it was not set up in the correct sequence, turn the fan off
        if ((wdata.fan_mode == FAN_MODE_TIMED) && (wdata.fan_sec == 0))
            control.set_fan_mode(FAN_MODE_OFF);

        // Animate the fan icon
        xI2CMessage xMessage;
        xMessage.xMessageType = I2C_ANIMATE_FAN;
        xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);

        wdata.seconds++; // Increment the uptime seconds ticker
        if (wdata.timestamp) // Increment the unix timestamp only if it has been set
            wdata.timestamp++;
        wdata.task_1s = uxTaskGetStackHighWaterMark(nullptr);
    }
}

// This task owns the exclusive right to talk to the devices on the I2C bus: LCD, temp sensor and GPIO relays
static void vTask_I2C(void *p)
{
    xI2CMessage xMessage;

    while(true)
    {
        // Wait for the I2C task message to arrive
        while(xQueueReceive(xI2CQueue, &xMessage, portMAX_DELAY) != pdPASS);

        if (xMessage.xMessageType == I2C_SET_RELAYS)
        {
            Wire.beginTransmission(GPIO_EXT_ADDR);
            Wire.write(xMessage.bMessage);
            Wire.endTransmission(true);

            wdata.relays = xMessage.bMessage;
        }
        else if (xMessage.xMessageType == I2C_READ_TEMP)
        {
            // Read temperature sensor
            sensors.requestTemperatures();
            wdata.temp_c = sensors.getTempCByIndex(0);
#if USE_MODEL
            wdata.temp_c = control.model_get_temperature();
#endif
            wdata.temp_f = wdata.temp_c * 9.0 / 5.0 + 32.0;

            // Sanity check the temperature reading
            wdata.temp_valid = (wdata.temp_f >= 60.0) && (wdata.temp_f <= 90.0);

            // Update temperature on the screen, round to the nearest
            lcd.setCursor(0, 0);
            lcd.print(String(int(wdata.get_temp_f() + 0.5), DEC));
            lcd.print(wdata.get_temp_valid() ? " F" : " ?");
        }
        else if (xMessage.xMessageType == I2C_LCD_INIT)
        {
            lcd_init();
        }
        else if (xMessage.xMessageType == I2C_PRINT_STATUS)
        {
            lcd.setCursor(6, 0);
            if (wdata.option == OPTION_OFF)
            {
                if (wdata.ac_mode == AC_MODE_OFF)
                    lcd.print("          ");
                else if (wdata.ac_mode == AC_MODE_COOL)
                    lcd.print("cool to " + String(wdata.cool_to));
                else if (wdata.ac_mode == AC_MODE_HEAT)
                    lcd.print("heat to " + String(wdata.heat_to));
                else if (wdata.ac_mode == AC_MODE_AUTO)
                    lcd.print("auto " + String(wdata.cool_to) + "/" + String(wdata.heat_to));
            }
            else if (wdata.option == OPTION_FAN)
            {
                if (wdata.fan_mode == FAN_MODE_OFF)
                    lcd.print("  Fan OFF ");
                else if (wdata.fan_mode == FAN_MODE_ON)
                    lcd.print("  Fan ON  ");
                else if (wdata.fan_mode == FAN_MODE_CYC)
                    lcd.print("  Fan CYC ");
            }
            else if (wdata.option == OPTION_AC)
            {
                if (wdata.ac_mode == AC_MODE_OFF)
                    lcd.print("  A/C OFF ");
                else if (wdata.ac_mode == AC_MODE_COOL)
                    lcd.print("  A/C COOL");
                else if (wdata.ac_mode == AC_MODE_HEAT)
                    lcd.print("  A/C HEAT");
                else if (wdata.ac_mode == AC_MODE_AUTO)
                    lcd.print("  A/C AUTO");
            }
        }
        else if (xMessage.xMessageType == I2C_ANIMATE_FAN)
        {
            lcd.setCursor(0, 1);
            if (wdata.fan_mode == OPTION_OFF)
                lcd.write(' ');
            else
                lcd.write((wdata.seconds & 1) ? CHAR_FAN1 : CHAR_FAN2);
        }

        wdata.task_i2c = uxTaskGetStackHighWaterMark(nullptr);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("START");

    delay(2000); // Start with some delay to sleep over any quick power glitches

    // Read the initial values stored in the NV (not-volatile memory)
    pref.begin("wd", true);
    wdata.id = pref.getString("id", "Thermostat");
    wdata.tag = pref.getString("tag", "Smart Thermostat station");
    wdata.ext_server = pref.getString("ext_server", "192.168.1.34/json");
    wdata.ext_read_sec = pref.getUInt("ext_read_sec", 0);
    wdata.fan_mode = pref.getUChar("fan_mode", FAN_MODE_OFF);
    wdata.ac_mode = pref.getUChar("ac_mode", AC_MODE_OFF);
    wdata.cool_to = pref.getUChar("cool_to", 90);
    wdata.heat_to = pref.getUChar("heat_to", 60);
    wdata.hyst_trigger = pref.getFloat("hyst_trigger", 1.5);
    wdata.hyst_release = pref.getFloat("hyst_release", 0.5);
    wdata.filter_sec = pref.getUInt("filter_sec", 0);
    wdata.cool_sec = pref.getUInt("cool_sec", 0);
    wdata.heat_sec = pref.getUInt("heat_sec", 0);
    pref.end();

    setup_wifi();
    setup_webserver();
    setup_i2c();
    setup_sw();

    xTaskCreatePinnedToCore(
        vTask_I2C,           // Task function
        "task_i2c",          // Name of the task
        2048,                // Stack size in bytes
        nullptr,             // Parameter passed as input to the task
        tskIDLE_PRIORITY + 1,// Priority of the task
        nullptr,             // Task handle
        APP_CPU);            // Core where the task should run (user program core)

    xTaskCreatePinnedToCore(
        vTask_1s_tick,       // Task function
        "task_1s",           // Name of the task
        2048,                // Stack size in bytes
        &wdata,              // Parameter passed as input to the task
        tskIDLE_PRIORITY,    // Priority of the task
        nullptr,             // Task handle
        APP_CPU);            // Core where the task should run (user program core)

    xTaskCreatePinnedToCore(
        vTask_ext_temp,      // Task function
        "task_ext_temp",     // Name of the task
        2048,                // Stack size in bytes
        nullptr,             // Parameter passed as input to the task
        tskIDLE_PRIORITY,    // Priority of the task
        nullptr,             // Task handle
        APP_CPU);            // Core where the task should run (user program core)

    delay(1000); // Give a second for all the tasks to start

    control.set_fan_mode(wdata.fan_mode);
    control.set_ac_mode(wdata.ac_mode);
    control.set_cool_to(wdata.cool_to);
    control.set_heat_to(wdata.heat_to);
}

void loop()
{
    wifi_check_loop();
}
