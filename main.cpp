#include "main.h"
#include <Preferences.h>


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
volatile static bool lcd_update = true;
//------------------------------------------------------------------------------------------
void setup_lcd()
{
    Wire.begin();
    Wire.beginTransmission(0x27);
    int error = Wire.endTransmission();
    if (error == 0)
    {
        lcd.begin(16, 2); // Initialize the lcd
        lcd.setBacklight(1);
    }
    else
        Serial.println("LCD init error: " + String(error, DEC));
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
    uint32_t io_num;
    while(true)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            bool level = gpio_get_level(gpio_num_t(io_num));
            if (io_num == GPIO_INPUT_IO_0) buttons[0] += level;
            if (io_num == GPIO_INPUT_IO_1) buttons[1] += level;
            if (io_num == GPIO_INPUT_IO_2) buttons[2] += level;

            lcd_update = level;
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


//-------------------------------------- PCF8574 -------------------------------------------
#define GPIO_EXT_ADDR 0x20
//------------------------------------------------------------------------------------------
void set_gpio_ext(bool r1, bool r2, bool r3, bool r4)
{
    uint8_t data = 0xFF;
    if (r1) data &= 0xFE;
    if (r2) data &= 0xFD;
    if (r3) data &= 0xFB;
    if (r4) data &= 0xF7;
    Wire.beginTransmission(GPIO_EXT_ADDR);
    Wire.write(data);
    Wire.endTransmission();
}


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

    int r = 0; // TEST

    while(true)
    {
        // Wait for the next cycle first, all calculation below will be triggered after the initial period passed
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        wdata.seconds++;

        // Once every 5 seconds, read all sensors and recalculate relevant data
        if ((wdata.seconds % PERIOD_5_SEC) == 0)
        {
            // Fill up data fields with the sensors' (and computed) data

            // Read temperature sensor
            sensors.requestTemperatures();
            wdata.temp_c = sensors.getTempCByIndex(0);
            wdata.temp_f = wdata.temp_c * 9.0 / 5.0 + 32.0;

            lcd_update = true;

            r++;
            set_gpio_ext(r&1, r&2, r&4, r&8);

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
    setup_lcd();
    setup_sw();
    set_gpio_ext(1, 1, 1, 1);

    // Arduino loop is running on core 1 and priority 1
    // https://techtutorialsx.com/2017/05/09/esp32-running-code-on-a-specific-core
    xTaskCreatePinnedToCore(
        vTask_read_sensors, // Task function
        "task_sensors",     // String with name of the task
        2048,               // Stack size in bytes
        &wdata,             // Parameter passed as input to the task
        1,                  // Priority of the task
        nullptr,            // Task handle
        1);                 // Core where the task should run (user program core)
}

void loop()
{
    wifi_check_loop();
    if (lcd_update)
    {
        lcd.home();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("SW: " + String(buttons[0], DEC) + "," + String(buttons[1], DEC) + "," + String(buttons[2], DEC));
        lcd.setCursor(0, 1);
        lcd.print("T = " + String(int(wdata.temp_f), DEC) + " F");

        lcd_update = false;
    }
}
