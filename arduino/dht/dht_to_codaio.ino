/*
 * Coda API as IoT IO logging API.
 * 
 * - ESP8266, e.g. Adafruit HUZZAH WSP8266
 * - Monochrome OLED based on SSD1306 driver, e.g. Adafruit OLED FeatherWing
 * - DHT22 series temperature / humidity sensor
 * 
 * Written by Matthew Tebbs @ Coda.io (matthewt@coda.io)
 * Released under MIT license.
 * 
 * Derived from the many excellent Arduino IDE examples by various authors.
 */

/*
 * Requires the following libraries:
 * 
 * - ArduinoJson Library: https://arduinojson.org/
 */
 
#include <ArduinoJson.h>

/*
 * Coda.io API (v1beta1)
 *
 * For an example of the doc that consumes this data see this published
 * document in the Coda gallery:-
 * 
 *     https://coda.io/@matthew-tebbs/home-iot
 * 
 * To make use of this example code you will need to fill in the API access token,
 * and update the doc, table and column ids to match your own.
 */

#define ACCESS_TOKEN    "ACCESS_TOKEN"
#define DOCID           "DOCID"
#define TABLEID         "TABLEID"
#define COLID_TEMP      "COLID_TEMP"
#define COLID_HUMIDITY  "COLID_HUMIDITY"

const String accessToken = String(ACCESS_TOKEN);
const String docId = String(DOCID);
const String tableId = String(TABLEID);
const String colIdTemperature = String(COLID_TEMP);
const String colIdHumidity = String(COLID_HUMIDITY);

/*
 * SHA-1 fingerprint of the Coda.io SSL certificate.
 */
const char* fingerPrintSHA1 = "76 F9 7D 68 87 4A BE D5 F2 67 77 A1 9D 5A 53 44 C2 B7 16 E3";

/*
 * Requires the following boards:
 * 
 * - ESP8266 Core for Arduino: https://github.com/esp8266/Arduino
 *
 * You will need to fill in your router's SSID and PASSWORD.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define SSID      "SSID"
#define PASSWORD  "PASSWORD"

HTTPClient http;

/*
 * Requires the following libraries:
 * 
 * - Adafrui GFX Graphics Core Library: https://github.com/adafruit/Adafruit-GFX-Library
 * - Adafruit SSD1306 Library: https://github.com/adafruit/Adafruit_SSD1306
 */
 
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display px width
#define SCREEN_HEIGHT 32 // OLED display px height

#define OLED_RESET_PIN -1 // reset pin (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 g_ssd1306Display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

/*
 * Requires the following libraries:
 * 
 * - Adafruit Unified Sensor Library: https://github.com/adafruit/Adafruit_Sensor
 * - Adafruit DHT Sensor Library: https://github.com/adafruit/DHT-sensor-library
 */

#include <DHT_U.h>

/*
 * DHT22
 * 
 * See guide for details on sensor wiring and usage:
 * https://learn.adafruit.com/dht/overview
 */

#define DHT_PIN 2 // digital pin connected to the DHT sensor 

DHT_Unified g_dhtSensors(DHT_PIN, DHT22);
int32_t g_msDhtSampleDelay;

void failure() {
  while (true); /* don't proceeed */
}

/*
 * Setup and loop.
 */

void setup() {
  // Start the serial connection and wait to open.
  Serial.begin(115200);
  while (!Serial);

  // Initialize the SSD1306 display.
  if (!g_ssd1306Display.begin(SSD1306_SWITCHCAPVCC, 0x3C /* for 128 x 32 */)) {
    Serial.println(F("SSD1306 allocation failed"));
    failure();
  }
  g_ssd1306Display.cp437(true);
  g_ssd1306Display.setTextColor(WHITE);

  // Initialize the DHT22.
  g_dhtSensors.begin();

  // Set delay between sensor readings based on sensor details.
  sensor_t sensor;
  g_dhtSensors.temperature().getSensor(&sensor);
  g_msDhtSampleDelay = sensor.min_delay / 1000;
  
  // Initialize the WiFi client and wait for connection.
  WiFi.mode(WIFI_STA);

  g_ssd1306Display.clearDisplay();
  g_ssd1306Display.setTextSize(1);
  g_ssd1306Display.setCursor(0, 0);
  
  g_ssd1306Display.println("WiFi connecting");
  g_ssd1306Display.display();
    
  boolean isConnected = false;
  WiFi.begin(SSID, PASSWORD);

  while (!isConnected) {
    delay(500);
    
    switch (WiFi.status()) {
      case WL_IDLE_STATUS:
      case WL_SCAN_COMPLETED:
      case WL_DISCONNECTED:
        g_ssd1306Display.print(".");
        g_ssd1306Display.display();
        break;

      case WL_CONNECTED:
        g_ssd1306Display.println();
        g_ssd1306Display.println("Connected");
        g_ssd1306Display.display();
        isConnected = true;
        break;

       default:
        g_ssd1306Display.println();
        g_ssd1306Display.println("Not connected!");
        g_ssd1306Display.display();
        failure();
        break;
    }
  }
  
  g_ssd1306Display.println(WiFi.localIP());
  g_ssd1306Display.display();

  delay(2000);

  // Set HTTP connection reuse.
  http.setReuse(true);
}

void loop() {
  // Measure temperature and humidity.
  sensors_event_t eventTemperature, eventHumidity;
  g_dhtSensors.temperature().getEvent(&eventTemperature);
  g_dhtSensors.humidity().getEvent(&eventHumidity);

  // Display.
  displayTemperatureAndHumidity(eventTemperature, eventHumidity);

  // Post to Coda.
  codaIoPostDataRow(eventTemperature, eventHumidity);
  
  // Delay between measurements.
  delay(5 * 60 * 1000); /* 5 mins */
}

int codaIoGetDataRowCount() {
  JsonObject & root = codaIoGetCall("docs/" + docId + "/tables/" + tableId);
  return root.success() ? root["rowCount"] : -1;
}

int codaIoPostDataRow(
  const sensors_event_t & eventTemperature,
  const sensors_event_t & eventHumidity
) {
  StaticJsonBuffer<256> jsonBuffer;
  
  JsonObject & root = jsonBuffer.createObject();
  JsonArray & rows = root.createNestedArray("rows");
  JsonObject & row = rows.createNestedObject();
  JsonArray & cells = row.createNestedArray("cells");
  
  JsonObject & cellTemperature = jsonBuffer.createObject();
  cellTemperature["column"] = colIdTemperature;
  cellTemperature["value"] = eventTemperature.temperature * 1.8 + 32;
  
  JsonObject & cellHumidity = jsonBuffer.createObject();
  cellHumidity["column"] = colIdHumidity;
  cellHumidity["value"] = eventHumidity.relative_humidity / 100;
  
  cells.add(cellTemperature);
  cells.add(cellHumidity);

  codaIoPostCall("docs/" + docId + "/tables/" + tableId + "/rows", root);
}

JsonObject & codaIoGetCall(String api) {
  JsonObject & retval = JsonObject::invalid();
  
  http.begin("https://coda.io/apis/v1beta1/" + api, fingerPrintSHA1);
  http.addHeader("Authorization", "Bearer " + accessToken);

  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    StaticJsonBuffer<1024> jsonBuffer;
    return jsonBuffer.parseObject(http.getStream());
  }
  else {
    Serial.printf("CodaIoAPI: Failed GET with error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  return retval;
}

boolean codaIoPostCall(String api, const JsonObject & root) {
  boolean retval = false;
  
  http.begin("https://coda.io/apis/v1beta1/" + api, fingerPrintSHA1);
  http.addHeader("Authorization", "Bearer " + accessToken);
  http.addHeader("Content-Type", "application/json");
  
  String body = String();
  root.printTo(body);
  Serial.println(body);

  int httpCode = http.POST(body);
  
  if (httpCode == HTTP_CODE_ACCEPTED) {
    retval = true;
  }
  else {
    Serial.printf("CodaIoAPI: Failed POST with error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  return retval;
}

void displayTemperatureAndHumidity(
  const sensors_event_t & eventTemperature,
  const sensors_event_t & eventHumidity
) {
  g_ssd1306Display.clearDisplay();
  g_ssd1306Display.setTextSize(2);
  g_ssd1306Display.setCursor(0, 0);

  g_ssd1306Display.printf("%.1f 'F", eventTemperature.temperature * 1.8 + 32);
  g_ssd1306Display.println();   
  
  g_ssd1306Display.printf("%.1f %%", eventHumidity.relative_humidity);
  g_ssd1306Display.println();
  
  g_ssd1306Display.display();
}
