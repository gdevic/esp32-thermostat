#include "main.h"
#include <Preferences.h>

#include "thermostat.h"
#include "control.h"

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


//-------------------------------------- LCD16x2 -------------------------------------------
// Requires library: "LiquidCrystal_PCF8574"
#include <LiquidCrystal_PCF8574.h>
#include <Wire.h>
LiquidCrystal_PCF8574 lcd(0x27); // Set the I2C LCD address

// The maximum number of messages that can be waiting for I2C task at any one time
#define I2C_QUEUE_SIZE 5

// Type of the message sent to the I2C task
typedef struct
{
    portBASE_TYPE xMessageType;
    char *pcMessage;
} xI2CMessage;

#define I2C_READ_TEMP  0
#define I2C_LCD_INIT   1
#define I2C_PRINT_SW   2

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
    static int customCharAir[]  = { B00000, B00000, B00000, B01101, B11010, B00000, B01101, B11010 };
    static int customCharDown[] = { B00000, B00000, B11111, B11111, B01110, B01110, B00100, B00100 };
    static int customCharUp[]   = { B00000, B00000, B00100, B00100, B01110, B01110, B11111, B11111 };

    lcd.begin(16, 2);
    lcd.createChar(0, customCharAir);
    lcd.createChar(1, customCharDown);
    lcd.createChar(2, customCharUp);
    lcd.clear();
    lcd.setBacklight(1);
    lcd.setCursor(9, 1);
    lcd.write(uint8_t(0)); // FAN
    lcd.setCursor(12, 1);
    lcd.write(uint8_t(1)); // TEMP DOWN
    lcd.setCursor(15, 1);
    lcd.write(uint8_t(2)); // TEMP UP
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
volatile int buttons[3] {};
//------------------------------------------------------------------------------------------
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, nullptr);
}

static void vTask_gpio(void* arg)
{
    xI2CMessage xMessage;
    uint32_t io_num;

    while(true)
    {
        while(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY) != pdPASS);

        bool level = !gpio_get_level(gpio_num_t(io_num));
        if (level) // Consider buttons only on a button press edge
        {
            if (io_num == GPIO_INPUT_IO_0) buttons[0] += level;
            if (io_num == GPIO_INPUT_IO_1) buttons[1] += level;
            if (io_num == GPIO_INPUT_IO_2) buttons[2] += level;

            xMessage.xMessageType = I2C_PRINT_SW;
            xQueueSend(xI2CQueue, &xMessage, portMAX_DELAY);
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
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Start gpio task
    xTaskCreate(vTask_gpio, "task_gpio", 2048, nullptr, 10, nullptr);
    // Install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // Hook isr handlers for specific gpio pins
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) GPIO_INPUT_IO_1);
    gpio_isr_handler_add(GPIO_INPUT_IO_2, gpio_isr_handler, (void*) GPIO_INPUT_IO_2);
}


StationData wdata = {};
CControl control;
CThermostat th;

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

        if (xMessage.xMessageType == I2C_READ_TEMP)
        {
            // Read temperature sensor
            sensors.requestTemperatures();
            wdata.temp_c = sensors.getTempCByIndex(0);
            wdata.temp_f = wdata.temp_c * 9.0 / 5.0 + 32.0;

            // Update temperature on the screen, round to the nearest
            lcd.setCursor(0, 0);
            lcd.print(String(int(wdata.temp_f + 0.5), DEC) + " F");
        }
        if (xMessage.xMessageType == I2C_LCD_INIT)
        {
            lcd_init();
        }
        if (xMessage.xMessageType == I2C_PRINT_SW)
        {
            lcd.setCursor(0, 1);
            lcd.print(String(buttons[0], DEC) + "," + String(buttons[1], DEC) + "," + String(buttons[2], DEC));
        }
    }
}

void setup()
{
    // Start with some delay to buffer out quick power glitches
    //delay(2000); // TBD

    Serial.begin(115200);

    // Read the initial values stored in the NVM
    pref.begin("wd", true);
    wdata.id = pref.getString("id", "Thermostat");
    wdata.tag = pref.getString("tag", "Smart Thermostat station");
    pref.end();

    setup_wifi();
    setup_webserver();
    setup_i2c();
    setup_sw();

    xTaskCreatePinnedToCore(
        vTask_I2C,          // Task function
        "task_i2c",         // Name of the task
        2048,               // Stack size in bytes
        nullptr,            // Parameter passed as input to the task
        tskIDLE_PRIORITY,   // Priority of the task
        nullptr,            // Task handle
        APP_CPU);           // Core where the task should run (user program core)

    xTaskCreatePinnedToCore(
        vTask_read_sensors, // Task function
        "task_sensors",     // String with name of the task
        2048,               // Stack size in bytes
        &wdata,             // Parameter passed as input to the task
        1,                  // Priority of the task
        nullptr,            // Task handle
        APP_CPU);           // Core where the task should run (user program core)
}

void loop()
{
    wifi_check_loop();
}
