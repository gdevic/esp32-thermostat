#include "main.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

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

String get_uptime_str(uint32_t sec)
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
    webtext_root += "\nuptime = " + get_uptime_str(wdata.seconds);
    webtext_root += "\nreconnects = " + String(reconnects);
    webtext_root += "\nRSSI = " + String(WiFi.RSSI()); // Signal strength
    webtext_root += "\nINT_C = " + String((temprature_sens_read() - 32) / 1.8);
    webtext_root += "\ntemp_c = " + String(wdata.temp_c);
    webtext_root += "\ntemp_f = " + String(wdata.temp_f);
    webtext_root += "\ntemp_valid = " + String(wdata.temp_valid);

    webtext_root += String("</pre></body></html>\n");

    // Format the json response
    webtext_json = "{";
    webtext_json += " \"id\":\"" + wdata.id + "\"";
    webtext_json += ", \"tag\":\"" + wdata.tag + "\"";
    webtext_json += ", \"uptime\":" + String(wdata.seconds);
    webtext_json += ", \"status\":" + String(wdata.status);
    webtext_json += ", \"temp_c\":" + String(wdata.temp_c);
    webtext_json += ", \"temp_f\":" + String(wdata.temp_f);
    webtext_json += ", \"temp_valid\":" + String(wdata.temp_valid);
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

// Set a variable from the client side. The key/value pairs are passed using an HTTP GET method.
void handleSet(AsyncWebServerRequest *request)
{
    if (xSemaphoreTake(webtext_semaphore, TickType_t(100)) == pdTRUE)
    {
        // TODO

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
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    IPAddress ip(192,168,1,40);
    IPAddress gateway(192,168,1,1);
    IPAddress subnet(255,255,255,0);
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
