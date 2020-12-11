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

    // Custom char generator: https://maxpromer.github.io/LCD-Character-Creator
    static int customCharAir[]    = { B00000, B00000, B00000, B01101, B11010, B00000, B01101, B11010 };
    static int customCharAirInv[] = { B11111, B11111, B11111, B10010, B00101, B11111, B10010, B00101 };
    static int customCharDown[]   = { B00000, B00000, B11111, B11111, B01110, B01110, B00100, B00100 };
    static int customCharUp[]     = { B00000, B00000, B00100, B00100, B01110, B01110, B11111, B11111 };
#define CHAR_AIR     0
#define CHAR_AIR_INV 1
#define CHAR_DOWN    2
#define CHAR_UP      3

    lcd.begin(16, 2);
    lcd.createChar(CHAR_AIR, customCharAir);
    lcd.createChar(CHAR_AIR_INV, customCharAirInv);
    lcd.createChar(CHAR_DOWN, customCharDown);
    lcd.createChar(CHAR_UP, customCharUp);

    lcd.clear();
    lcd.setBacklight(1);
    lcd.setCursor(9, 1);
    lcd.write(uint8_t(CHAR_AIR)); // FAN
    lcd.setCursor(12, 1);
    lcd.write(uint8_t(CHAR_DOWN)); // TEMP DOWN
    lcd.setCursor(15, 1);
    lcd.write(uint8_t(CHAR_UP)); // TEMP UP
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
volatile bool buttons[3] {};
#define BUTTON_INDEX_FAN  0
#define BUTTON_INDEX_DOWN 1
#define BUTTON_INDEX_UP   2
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

        if (level) // Consider buttons only on a button press edge
        {
            // Manual fan mode button
            if (button_index == BUTTON_INDEX_FAN)
            {
                control.set_fan_mode(wdata.fan_mode + 1); // control class with wrap the value back to 0
            }

            // Switch heating/cooling modes by pressing both buttons at the same time
            if (buttons[1] && buttons[2])
            {
                wdata.ac_mode++;
                if (wdata.ac_mode > AC_MODE_LAST)
                    wdata.ac_mode = 0;

                xMessage.xMessageType = I2C_PRINT_AC;
                xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
            }
            else if (buttons[1] || buttons[2]) // Temperature up or down
            {
                bool down = (button_index == BUTTON_INDEX_DOWN);
                bool up = (button_index == BUTTON_INDEX_UP);

                if (wdata.ac_mode == AC_MODE_COOL)
                {
                    if (down && (wdata.cool_to > 60.0)) wdata.cool_to = wdata.cool_to - 1.0;
                    if (up   && (wdata.cool_to < 90.0)) wdata.cool_to = wdata.cool_to + 1.0;
                }
                if (wdata.ac_mode == AC_MODE_HEAT)
                {
                    if (down && (wdata.heat_to > 60.0)) wdata.heat_to = wdata.heat_to - 1.0;
                    if (up   && (wdata.heat_to < 90.0)) wdata.heat_to = wdata.heat_to + 1.0;
                }

                xMessage.xMessageType = I2C_PRINT_TARGET;
                xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
            }
        }
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
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) BUTTON_INDEX_FAN);
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) BUTTON_INDEX_DOWN);
    gpio_isr_handler_add(GPIO_INPUT_IO_2, gpio_isr_handler, (void*) BUTTON_INDEX_UP);
}


// Set a preference string value pairs, we are using char, int, float and string variants
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

static void vTask_read_sensors(void *p)
{
    // Make this task sleep and awake once a second
    const TickType_t xFrequency = 1 * 1000 / portTICK_PERIOD_MS;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    xI2CMessage xMessage;

    while(true)
    {
        // Wait for the next cycle first, all calculation below will be triggered after the initial period passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        wdata.seconds++;

        // Once every 5 seconds, read temperature sensor
        if ((wdata.seconds % PERIOD_5_SEC) == 0)
        {
            xMessage.xMessageType = I2C_READ_TEMP;
            xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
        }
        // At the end, preset various response strings that the server should give out. This will happen once a second,
        // whether we have any new data or not.
        webserver_set_response();
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
        if (xMessage.xMessageType == I2C_READ_TEMP)
        {
            // Read temperature sensor
            sensors.requestTemperatures();
            wdata.temp_c = sensors.getTempCByIndex(0);
            wdata.temp_f = wdata.temp_c * 9.0 / 5.0 + 32.0;

            // Sanity check the temperature reading
            wdata.temp_valid = (wdata.temp_f >= 60.0) && (wdata.temp_f <= 90.0);

            // Update temperature on the screen, round to the nearest
            lcd.setCursor(0, 0);
            lcd.print(String(int(wdata.temp_f + 0.5), DEC));
            lcd.print(wdata.temp_valid ? " F" : " ?");
        }
        if (xMessage.xMessageType == I2C_LCD_INIT)
        {
            lcd_init();
        }
        if (xMessage.xMessageType == I2C_PRINT_FAN)
        {
            lcd.setCursor(0, 1);
            if (wdata.fan_mode == FAN_MODE_OFF)
                lcd.print("       ");
            if (wdata.fan_mode == FAN_MODE_ON)
                lcd.print("Fan ON ");
            if (wdata.fan_mode == FAN_MODE_CYC)
                lcd.print("Fan CYC");
        }
        if (xMessage.xMessageType == I2C_PRINT_AC)
        {
            lcd.setCursor(6, 0);
            if (wdata.ac_mode == AC_MODE_OFF)
                lcd.print("          ");
            if (wdata.ac_mode == AC_MODE_COOL)
                lcd.print(" A/C cool ");
            if (wdata.ac_mode == AC_MODE_HEAT)
                lcd.print(" A/C heat ");
            if (wdata.ac_mode == AC_MODE_AUTO)
                lcd.print(" A/C auto ");
        }
        if (xMessage.xMessageType == I2C_PRINT_TARGET)
        {
            lcd.setCursor(6, 0);
            if (wdata.ac_mode == AC_MODE_OFF)
                lcd.print("          ");
            if (wdata.ac_mode == AC_MODE_COOL)
                lcd.print("cool to " + String(wdata.cool_to));
            if (wdata.ac_mode == AC_MODE_HEAT)
                lcd.print("heat to " + String(wdata.heat_to));
        }
    }
}

void setup()
{
    delay(2000); // Start with some delay to buffer out quick power glitches

    Serial.begin(115200);

    // Read the initial values stored in the NVM
    pref.begin("wd", true);
    wdata.id = pref.getString("id", "Thermostat");
    wdata.tag = pref.getString("tag", "Smart Thermostat station");
    pref.end();

    wdata.cool_to = 90.0;
    wdata.heat_to = 60.0;

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
        vTask_read_sensors,  // Task function
        "task_sensors",      // Name of the task
        2048,                // Stack size in bytes
        &wdata,              // Parameter passed as input to the task
        tskIDLE_PRIORITY,    // Priority of the task
        nullptr,             // Task handle
        APP_CPU);            // Core where the task should run (user program core)

    delay(1000); // Give a second for all the tasks to start

    // Recover controller state
    pref.begin("wd", false);
    control.set_fan_mode(pref.getUChar("fan_mode", FAN_MODE_OFF));
    pref.end();
}

void loop()
{
    wifi_check_loop();
}
