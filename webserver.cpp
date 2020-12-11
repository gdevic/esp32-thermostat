#include "main.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "control.h"

// Async web server needs these two additional libraries:
// https://github.com/me-no-dev/ESPAsyncWebServer
// https://github.com/me-no-dev/AsyncTCP

extern "C" uint8_t temprature_sens_read(); // Very imprecise internal ESP32 temperature value in F

#include "wifi_credentials.h"
// This include file is ignored by git (in .gitignore) and should contain your specific ssid and password as defines:
// #define MY_SSID "your-ssid"
// #define MY_PASS "your-password"
static const char* ssid = MY_SSID;
static const char* password = MY_PASS;
static String webtext_root; // Web response to / (root)
static String webtext_json; // Web response to /json
static uint32_t reconnects = 0; // Count how many times WiFi had to reconnect (for stats)
static String wifi_mac; // WiFi MAC address of this station
static SemaphoreHandle_t webtext_semaphore; // Semaphore guarding the access to webtext strings as we are building them

AsyncWebServer server(80);

String get_time_str(uint32_t sec)
{
    uint32_t seconds = (sec % 60);
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t days = (sec % (86400 * 30)) / 86400;
    return String(days) + ":" + String(hours) + ":" + String(minutes) + ":" + String(seconds);
}

void webserver_set_response()
{
    // Wait 20 ms before giving up. In practice, there are no other tasks that could block this sem. for longer than that
    // This task will take the longest due to a number of string operations
    if (xSemaphoreTake(webtext_semaphore, TickType_t(20)) != pdTRUE)
        return;

    // Make this web page auto-refresh every 5 sec
    webtext_root = String("<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"5\"></head><body><pre>");

    webtext_root += "\nVER = " + String(FIRMWARE_VERSION);
    webtext_root += "\nID = " + wdata.id;
    webtext_root += "\nTAG = " + wdata.tag;
    webtext_root += "\nMAC = " + wifi_mac;
    webtext_root += "\nstatus = " + String(wdata.status);
    webtext_root += "\nuptime = " + get_time_str(wdata.seconds);
    webtext_root += "\nreconnects = " + String(reconnects);
    webtext_root += "\nRSSI = " + String(WiFi.RSSI()); // Signal strength
    webtext_root += "\nGPIO23 = " + String(wdata.gpio23);
    webtext_root += "\nINT_C = " + String((temprature_sens_read() - 32) / 1.8);
    if (wdata.temp_valid) // Display the temperature value only if it is valid
    {
        webtext_root += "\ntemp_c = " + String(wdata.temp_c);
        webtext_root += "\ntemp_f = " + String(wdata.temp_f);
    }
    webtext_root += "\ntemp_valid = " + String(wdata.temp_valid);
    webtext_root += "\nrelays = " + String(wdata.relays);
    webtext_root += "\nfan_mode = " + String(wdata.fan_mode);
    webtext_root += "\nac_mode = " + String(wdata.ac_mode);
    webtext_root += "\ncool_to = " + String(wdata.cool_to);
    webtext_root += "\nheat_to = " + String(wdata.heat_to);
    webtext_root += "\nfilter_sec = " + String(wdata.filter_sec);
    webtext_root += "\ncool_sec = " + String(wdata.cool_sec);
    webtext_root += "\nheat_sec = " + String(wdata.heat_sec);
    webtext_root += "\nfilter_hms = " + get_time_str(wdata.filter_sec);
    webtext_root += "\ncool_hms = " + get_time_str(wdata.cool_sec);
    webtext_root += "\nheat_hms = " + get_time_str(wdata.heat_sec);
    webtext_root += String("</pre></body></html>\n");

    // Format the json response
    webtext_json = "{";
    webtext_json += " \"id\":\"" + wdata.id + "\"";
    webtext_json += ", \"tag\":\"" + wdata.tag + "\"";
    webtext_json += ", \"uptime\":" + String(wdata.seconds);
    webtext_json += ", \"status\":" + String(wdata.status);
    if (wdata.temp_valid) // Add the temperature valid only if it is valid
    {
        webtext_json += ", \"temp_c\":" + String(wdata.temp_c);
        webtext_json += ", \"temp_f\":" + String(wdata.temp_f);
    }
    webtext_json += ", \"temp_valid\":" + String(wdata.temp_valid);
    webtext_json += ", \"relays\":" + String(wdata.relays);
    webtext_json += ", \"fan_mode\":" + String(wdata.fan_mode);
    webtext_json += ", \"ac_mode\":" + String(wdata.ac_mode);
    webtext_json += ", \"cool_to\":" + String(wdata.cool_to);
    webtext_json += ", \"heat_to\":" + String(wdata.heat_to);
    webtext_json += ", \"filter_sec\":" + String(wdata.filter_sec);
    webtext_json += ", \"cool_sec\":" + String(wdata.cool_sec);
    webtext_json += ", \"heat_sec\":" + String(wdata.heat_sec);
    webtext_json += " }";

    xSemaphoreGive(webtext_semaphore);
}

void handleRoot(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        request->send(200, "text/html", webtext_root);
        xSemaphoreGive(webtext_semaphore);
    }
    else
        request->send(503, "text/html", "Resource busy, please retry.");
}

void handleJson(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        request->send(200, "application/json", webtext_json);
        xSemaphoreGive(webtext_semaphore);
    }
    else
    {
        request->send(503, "application/json", "{ \"id\":\"" + wdata.id + "\" }");
    }
}

template<class T> T parse(String value, char **p_next);
template<> inline uint8_t parse<uint8_t>(String value, char **p_next) { return strtoul(value.c_str(), p_next, 0); }
template<> inline uint32_t parse<uint32_t>(String value, char **p_next) { return strtoul(value.c_str(), p_next, 0); }
template<> inline float parse<float>(String value, char **p_next) { return strtof(value.c_str(), p_next); }
template<> inline String parse<String>(String value, char **p_next)
{
    value.trim();
    value.replace("\"", "'"); // Disallow the quotation character to ensure valid JSON output when printed
    return value;
}

// Parses the GET method ?name=value argument and returns false if the key or its value are not valid
// When the key name is matched, and the value is correct, it updates the wdata reference variable and its NV value
template <class T>
static bool get_parse_value(AsyncWebServerRequest *request, String key_name, T& dest, bool save_nv)
{
    String value = request->arg(key_name);
    if (value.length())
    {
        char *p_next;
        T n = parse<T>(value, &p_next);

        // Check for validity of int and float types since we read them using strto* functions
        bool is_string = sizeof(T) == sizeof(String); // Little trick to tell String type apart without having the RTTI support
        if (is_string || ((p_next != value.c_str()) && (*p_next == 0) && (errno != ERANGE)))
        {
            dest = n; // Set the wdata.<key_name> member
            if (save_nv)
                pref_set(key_name.c_str(), n); // Set the new value into the NV variable
            request->send(200, "text/html", "OK " + String(n));
            return true;
        }
    }
    return false;
}

// Set a variable from the client side. The key/value pairs are passed using an HTTP GET method.
void handleSet(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        uint8_t u8;
        // Updating one at a time will respond with "OK" + the new value
        bool ok = false;
        ok |= get_parse_value(request, "id", wdata.id, true);
        ok |= get_parse_value(request, "tag", wdata.tag, true);
        ok |= get_parse_value(request, "filter_sec", wdata.filter_sec, true);
        ok |= get_parse_value(request, "cool_sec", wdata.cool_sec, true);
        ok |= get_parse_value(request, "heat_sec", wdata.heat_sec, true);
        ok |= get_parse_value(request, "fan_mode", u8, false);
        ok |= get_parse_value(request, "ac_mode", u8, false);
        ok |= get_parse_value(request, "cool_to", u8, false);
        ok |= get_parse_value(request, "heat_to", u8, false);
        if (!ok)
            request->send(400, "text/html", "Invalid request");
        else
        {
            if (request->arg("fan_mode").length())
                control.set_fan_mode(u8);
            else if (request->arg("ac_mode").length())
                control.set_ac_mode(u8);
            else if (request->arg("cool_to").length())
                control.set_cool_to(u8);
            else if (request->arg("heat_to").length())
                control.set_heat_to(u8);
        }

        xSemaphoreGive(webtext_semaphore);
    }
    else
        request->send(503, "text/html", "Resource busy, please retry.");
}

const char* uploadHtml = " \
<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script> \
<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'> \
 <input type='file' name='update'> \
  <input type='submit' value='Update'> \
 </form> \
<div id='prg'>Progress: 0%</div> \
<script> \
 $('form').submit(function(e) \
 { \
  e.preventDefault(); \
  var form = $('#upload_form')[0]; \
  var data = new FormData(form); \
  $.ajax({ \
   url: '/flash', \
   type: 'POST', \
   data: data, \
   contentType: false, \
   processData:false, \
   xhr: function() \
   { \
    var xhr = new window.XMLHttpRequest(); \
    xhr.upload.addEventListener('progress', function(evt) \
    { \
     if (evt.lengthComputable) \
     { \
      var per = evt.loaded / evt.total; \
      $('#prg').html('progress: ' + Math.round(per*100) + '%'); \
     } \
    }, false); \
    return xhr; \
   }, \
   success:function(d, s) \
   { \
       console.log('success!') \
   }, \
   error: function (a, b, c) { } \
  }); \
 }); \
</script> \
";

void setup_ota()
{
    server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", uploadHtml);
        response->addHeader("Connection", "close");
        request->send(response);
    });
    server.on("/flash", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        response->addHeader("Connection", "close");
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
    {
        // Serial.printf("Uploading: index=%d len=%d final=%d\n", index, len, final);
        if (index == 0)
        {
            Serial.printf("Uploading: %s\n", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) // start with max available size
                Update.printError(Serial);
        }
        if (!Update.hasError())
        {
            if (Update.write(data, len) != len) // flashing firmware to ESP
                Update.printError(Serial);
        }
        if (final)
        {
            if (Update.end(true)) // true to set the size to the current progress
            {
                Serial.println("Flash OK, rebooting...\n");
                ESP.restart();
            }
            else
                Update.printError(Serial);
        }
    });
}

void setup_wifi()
{
    // Based on the GPIO23 strap, assign the static IP address
    // At the moment, the "production" board has GPIO23 fused with the GND, reading 0
    //                the "development" board has GPIO23 open, reading 1
    gpio_config_t io_conf
    {
        1ULL << GPIO_NUM_23,
        GPIO_MODE_INPUT,
        GPIO_PULLUP_ENABLE,
        GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    wdata.gpio23 = gpio_get_level(gpio_num_t(GPIO_NUM_23));

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    IPAddress ip(192,168,1,40);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);
    if (wdata.gpio23)
        ip[3] = 41; // Assign 192.168.1.41 to the "development" board via strap
    WiFi.config(ip, gateway, subnet);
    wifi_mac = WiFi.macAddress();

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.printf("\nConnected to %s\nIP address: ", ssid);
    Serial.println(WiFi.localIP());
    reconnects++;

    if (MDNS.begin("esp32"))
        Serial.println("MDNS responder started");
}

void setup_webserver()
{
    webtext_semaphore = xSemaphoreCreateMutex();
    xSemaphoreGive(webtext_semaphore);

    webserver_set_response();
    server.on("/", handleRoot);
    server.on("/json", handleJson);
    server.on("/set", handleSet);
    setup_ota();
    server.begin();
}

void wifi_check_loop()
{
    delay(1000);

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Server disconnected! Reconnecting...");
        setup_wifi();
    }
}
