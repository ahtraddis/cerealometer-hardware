/**
 * Cerealometer
 * 
 * Copyright (c) 2020 Eric Schwartz
 */

// Enable/disable options
#define DEBUG false
#define USE_FIREBASE

// These non-essential features are disabled due to limited flash memory on the
// SparkFun Thing Dev board. Uncomment to enable them if your hardware so allows!
//#define ENABLE_MDNS
//#define ENABLE_WEBSERVER
//#define USE_WIFI_MULTI

// Local config file including URLs and credentials
#include "config.h"

// FirebaseESP8266.h must be included before ESP8266Wifi.h
#include "FirebaseESP8266.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
// [MEMORY] Note: ESP8266WiFiMulti.h lib and code uses +2036 bytes
#ifdef USE_WIFI_MULTI
#include <ESP8266WiFiMulti.h>
#endif
// [MEMORY] Note: ESP8266mDNS.h lib and code uses +21808 bytes
#ifdef ENABLE_MDNS
#include <ESP8266mDNS.h>
#endif
#ifdef ENABLE_WEBSERVER
#include <ESP8266WebServer.h>
#endif

#include <HX711.h>
#include "Statistic.h"
#include "LedControl.h"

// Constants

#define PORT_COUNT 3
#define POUNDS_PER_KILOGRAM 2.20462262185
#define WEIGHT_MEASURE_INTERVAL_MS 500
#define STATS_WINDOW_LENGTH 10
#define PRESENCE_THRESHOLD_KG 0.001
// Minimum change of averaged weight_kg required to trigger data upload
#define KG_CHANGE_THRESHOLD 0.001
#define STD_DEV_THRESHOLD 0.03
// LED display
#define DISPLAY_UPDATE_INTERVAL_MS 50
#define DEFAULT_INTENSITY 8
#define MAX_INTENSITY 15
#define MATRIX_COLUMNS 6
#define MATRIX_COLORS 3
#define R 0
#define G 1
#define B 2
#define R_SHIFT_1 7
#define G_SHIFT_1 6
#define B_SHIFT_1 5
#define R_SHIFT_2 4
#define G_SHIFT_2 3
#define B_SHIFT_2 2
// Bitmasks for byte values in display matrix array
#define VAL_BITMASK   B00000001 // value (on/off) is the LSB
#define FX_BITMASK    B00001110 // effect opcode bitmap
#define DELAY_BITMASK B11110000 // delay or sequence value
// Display effect opcodes
#define FX_NONE 0
#define FX_BLINK 1
#define FX_BLINK_FAST 2
// Divisors for opcode timing
#define BLINK_DIVISOR 4
#define FAST_BLINK_DIVISOR 1
// Slot statuses
#define STATUS_UNKNOWN 0
#define STATUS_INITIALIZING 1
#define STATUS_VACANT 2
#define STATUS_LOADED 3
#define STATUS_UNLOADED 4
#define STATUS_CLEARING 5
// LED states
#define LED_OFF 0
#define LED_INITIALIZING 1
#define LED_VACANT 2
#define LED_LOADED 3
#define LED_UNLOADED 4
#define LED_CLEARING 5
#define LED_RED 6
#define LED_RED_BLINK 7
#define LED_RED_BLINK_FAST 8
#define LED_GREEN 9
#define LED_GREEN_BLINK 10
#define LED_GREEN_BLINK_FAST 11
#define LED_BLUE 12
#define LED_BLUE_BLINK 13
#define LED_BLUE_BLINK_FAST 14
#define LED_WHITE 15
#define LED_YELLOW 16
#define LED_PURPLE 17
#define LED_CYAN 18

// Macros
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// Globals
#ifdef USE_WIFI_MULTI
ESP8266WiFiMulti wifiMulti;
#endif
#ifdef ENABLE_WEBSERVER
// Create webserver object listening for HTTP requests on port 80
ESP8266WebServer server(80);
#endif
#ifdef USE_FIREBASE
FirebaseData firebaseData;
#endif

HTTPClient http;

// LedControl 4 params are: pins DATA IN, CLK, LOAD (/CS), and the number of cascaded devices
LedControl lc = LedControl(16, 13, 12, 1);
unsigned long lastDisplayUpdateMillis;
unsigned long displayCounter = 0;
// RGB color sequence used for display tests
const static bool rgbSeq[7][3] = {
  {1, 0, 0}, // red
  {0, 1, 0}, // green
  {0, 0, 1}, // blue
  {1, 1, 1}, // white
  {1, 1, 0}, // yellow
  {1, 0, 1}, // purple
  {0, 1, 1}, // cyan
};

// SparkFun Thing Dev board has enough I/O for 3 HX711 boards (reserving others for MAX7219).
// This defines the pairs of clock and data pins wired up.
byte hx711_clock_pins[PORT_COUNT] = { 0, 2, 15 };
byte hx711_data_pins[PORT_COUNT] = { 4, 14, 5 };
int hx711_calibration_factors[PORT_COUNT] = { -178000, -178000, -178000 };

char buff[10];

typedef struct {
  int calibration_factor;
  float last_weight_kilograms; // last value uploaded
  float current_weight_kilograms; // most recent sample
  HX711 scale;
  Statistic stats;
  byte data_pin;
  byte clock_pin;
  unsigned long lastWeightMeasurementPushMillis;
  byte status;
} Port;

Port ports[PORT_COUNT];

byte matrix[MATRIX_COLORS][MATRIX_COLUMNS];
#ifdef ENABLE_WEBSERVER
void handleRoot();
void handleNotFound();
#endif

// declare board reset function at address 0
void(* resetDevice) (void) = 0;

// Global functions

int setWeight(String device_id, int slot, float weight_kg) {
  // Open https connection to setWeight cloud function with site's certificate
  // [eschwartz-TODO] Re-test with https.
  //http.begin(REST_API_ENDPOINT, SHA1_FINGERPRINT);
  http.begin(REST_API_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  String data = String("{\"device_id\": \"") + device_id
    + String("\", \"slot\": \"") + String(slot)
    + String("\", \"weight_kg\": ") + dtostrf(weight_kg, 2, 4, buff)
    + String("}");
  // [eschwartz-TODO]: This should probably be a PUT to /ports/<device_id>/data/<slot>
  int httpCode = http.POST(data);
  String payload = http.getString();

  if (httpCode > 0) {
    if (DEBUG) {
      String payload = http.getString(); // response payload 
      Serial.print(F("Slot "));
      Serial.print(slot);
      Serial.print(F(" setWeight SUCCESS, httpCode = "));
      Serial.println(httpCode);
      //Serial.print("response: ");
      //Serial.println(payload);
    }
  } else {
    Serial.print("Slot ");
    Serial.print(slot);
    Serial.print(" setWeight FAILED: httpCode = ");
    Serial.print(httpCode);
    Serial.print(", error = ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void resetTares(void) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    ports[i].scale.tare();
  }
}

void printCurrentValues(void) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    Serial.print("Slot ");
    Serial.print(i);
    Serial.print(" (current_weight_kilograms): ");
    Serial.println(ports[i].current_weight_kilograms, 4);
  }
}

void printLogMessage(uint8_t slot, float kg_change) {
  Serial.print("Slot ");
  Serial.print(slot);
  Serial.print(" CHANGED: ");
  Serial.print("count: ");
  Serial.print(ports[slot].stats.count());
  Serial.print(", avg: ");
  Serial.print(ports[slot].stats.average(), 4);
  Serial.print(", std dev: ");
  Serial.print(ports[slot].stats.pop_stdev(), 4);
  Serial.print(", last: ");
  Serial.print(ports[slot].last_weight_kilograms, 4);
  Serial.print(", curr: ");
  Serial.print(ports[slot].stats.average(), 4);
  Serial.print(", diff: ");
  Serial.print(kg_change, 4);
  Serial.print(" (> ");
  Serial.print(KG_CHANGE_THRESHOLD, 4);
  Serial.print(" kg)");
  Serial.println();
}

// Refresh LED states after disrupting them with displayTest()
void restoreLedStates(void) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, statusToLedState(ports[i].status));
  }
}

// Run LED test sequences
void displayTest(void) {
  uint8_t spaceDelayMs = 75;
  for (uint8_t i = 0; i < 7; i++) {
    ledFlashRight(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
  }
  spaceDelayMs = 25;
  for (uint8_t i = 0; i < 7; i++) {
    ledFadeUpRow(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
    ledFadeDownRow(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
  }
  lc.setIntensity(0, MAX_INTENSITY);
  ledRandom(25, 200);
  ledFadeAll(25);
  delay(500); // pause before restoring normal state
  lc.setIntensity(0, DEFAULT_INTENSITY);
  zeroMatrix();
  restoreLedStates();
}

void ledFadeAll(int delayMs) {
  for (uint8_t i = 15; i > 0; i--) {
    lc.setIntensity(0, i);
    delay(delayMs);
  }
  lc.shutdown(0, true);
  delay(delayMs);
  zeroMatrix();
  updateDisplay();
  lc.shutdown(0, false);
}

void ledFadeUpRow(boolean r, boolean g, boolean b, int delayMs) {
  // power off while writing data
  lc.shutdown(0, true);
  for (uint8_t i = 0; i < MATRIX_COLUMNS; i++) {
    matrix[R][i] = r;
    matrix[G][i] = g;
    matrix[B][i] = b;
  }
  updateDisplay();
  lc.setIntensity(0, 0);
  lc.shutdown(0, false);
  for (uint8_t i = 0; i < 16; i++) {
    lc.setIntensity(0, i);
    delay(delayMs);
  }
}

void ledFadeDownRow(boolean r, boolean g, boolean b, int delayMs) {
  for (uint8_t i = 0; i < MATRIX_COLUMNS; i++) {
    matrix[R][i] = r;
    matrix[G][i] = g;
    matrix[B][i] = b;
  }
  updateDisplay();
  for (uint8_t i = 15; i > 0; i--) {
    lc.setIntensity(0, i);
    delay(delayMs);
  }
  lc.shutdown(0, true);
  delay(delayMs);
  zeroMatrix();
  updateDisplay();
  lc.shutdown(0, false);
}

void ledRandom(int delayMs, int maxCount) {
  int counter = 0;
  while (counter++ < maxCount) {
    uint8_t column = random(0, MATRIX_COLUMNS);
    uint8_t rgbIndex = random(0, 7);
    matrix[R][column] = rgbSeq[rgbIndex][0];
    matrix[G][column] = rgbSeq[rgbIndex][1];
    matrix[B][column] = rgbSeq[rgbIndex][2];
    updateDisplay();
    delay(delayMs);
  }
}

void ledFlashRight(boolean r, boolean g, boolean b, int delayMs) {
  for (uint8_t i = 0; i < MATRIX_COLUMNS; i++) {
    matrix[R][i] = r;
    matrix[G][i] = g;
    matrix[B][i] = b;
    updateDisplay();
    delay(delayMs);
    matrix[R][i] = 0;
    matrix[G][i] = 0;
    matrix[B][i] = 0;
  }
}

void ledFlashLeft(boolean r, boolean g, boolean b, int delayMs) {
  for (uint8_t i = (MATRIX_COLUMNS - 1); i > 0; i--) {
    matrix[R][i] = r;
    matrix[G][i] = g;
    matrix[B][i] = b;
    updateDisplay();
    delay(delayMs);
    matrix[R][i] = 0;
    matrix[G][i] = 0;
    matrix[B][i] = 0;
  }
}

// Set LED state and timing values in display matrix
void setLedState(uint8_t pos, uint8_t state) {
  uint8_t rVal = 0, gVal = 0, bVal = 0,
    rOpcode = FX_NONE, gOpcode = FX_NONE, bOpcode = FX_NONE,
    rDivisor = 0, gDivisor = 0, bDivisor = 0;
  // For blink effect, align initial on/off state with even values of displayCounter
  // so all LEDs blinking at the same rate are in phase with each other
  switch (state) {
    case LED_RED:
    case LED_LOADED:
      rVal = 1; // solid red ala Whole Foods parking lot
      break;
    case LED_RED_BLINK:
    case LED_UNLOADED:
      rVal = displayCounter % 2;
      rOpcode = FX_BLINK;
      rDivisor = BLINK_DIVISOR;
      break;
    case LED_RED_BLINK_FAST:
    case LED_CLEARING:
      rVal = displayCounter % 2;
      rOpcode = FX_BLINK_FAST;
      rDivisor = FAST_BLINK_DIVISOR;
      break;
    case LED_GREEN:
    case LED_VACANT:
      gVal = 1; // solid green ala Whole Foods parking lot
      break;
    case LED_GREEN_BLINK:
      gVal = displayCounter % 2;
      gOpcode = FX_BLINK;
      gDivisor = BLINK_DIVISOR;
      break;
    case LED_GREEN_BLINK_FAST:
      gVal = displayCounter % 2;
      gOpcode = FX_BLINK_FAST;
      gDivisor = FAST_BLINK_DIVISOR;
      break;
    case LED_BLUE:
      rVal = 0; // solid blue
      gVal = 0;
      bVal = 1;
      break;
    case LED_BLUE_BLINK:
      bVal = displayCounter % 2;
      bOpcode = FX_BLINK;
      bDivisor = BLINK_DIVISOR;
      break;
    case LED_BLUE_BLINK_FAST:
      bVal = displayCounter % 2;
      bOpcode = FX_BLINK_FAST;
      bDivisor = FAST_BLINK_DIVISOR;
      break;
    case LED_WHITE:
    case LED_INITIALIZING:
      rVal = 1; // solid white
      gVal = 1;
      bVal = 1;
      break;
    case LED_YELLOW:
      rVal = 1; // solid yellow
      gVal = 1;
      bVal = 0;
      break;
    case LED_PURPLE:
      rVal = 1; // solid purple
      gVal = 0;
      bVal = 1;
      break;
    case LED_CYAN:
      rVal = 0; // solid cyan
      gVal = 1;
      bVal = 1;
      break;
    case LED_OFF:
    default:
      rVal = 0;
      gVal = 0;
      bVal = 0;
      break;
  }
  matrix[R][pos] = rVal | (rOpcode << 1) | (rDivisor << 4);
  matrix[G][pos] = gVal | (gOpcode << 1) | (gDivisor << 4);
  matrix[B][pos] = bVal | (bOpcode << 1) | (bDivisor << 4);
  updateDisplay();
}

void updateDisplay() {
  // Loop through display matrix data, apply FX if opcode and divisor are present
  for (uint8_t color = 0; color < MATRIX_COLORS; color++) {
    for (uint8_t column = 0; column < MATRIX_COLUMNS; column++) {
      byte val = matrix[color][column] & VAL_BITMASK;
      byte opcode = (matrix[color][column] & FX_BITMASK) >> 1;
      byte divisor = (matrix[color][column] & DELAY_BITMASK) >> 4;

      if ((opcode == FX_BLINK) || (opcode == FX_BLINK_FAST)) {
        if ((displayCounter % divisor) == 0) {
          val = 1 - val; // toggle value
        }
      }
      matrix[color][column] = val | (opcode << 1) | (divisor << 4);
    }
  }
  // Transform logical array (6 LEDs x 1 row) to hardware I/O (6 color bits x 3 rows)
  byte row0 =
    ((matrix[R][0] & VAL_BITMASK) << R_SHIFT_1) |
    ((matrix[G][0] & VAL_BITMASK) << G_SHIFT_1) |
    ((matrix[B][0] & VAL_BITMASK) << B_SHIFT_1) |
    ((matrix[R][1] & VAL_BITMASK) << R_SHIFT_2) |
    ((matrix[G][1] & VAL_BITMASK) << G_SHIFT_2) |
    ((matrix[B][1] & VAL_BITMASK) << B_SHIFT_2);
  byte row1 =
    ((matrix[R][2] & VAL_BITMASK) << R_SHIFT_1) |
    ((matrix[G][2] & VAL_BITMASK) << G_SHIFT_1) |
    ((matrix[B][2] & VAL_BITMASK) << B_SHIFT_1) |
    ((matrix[R][3] & VAL_BITMASK) << R_SHIFT_2) |
    ((matrix[G][3] & VAL_BITMASK) << G_SHIFT_2) |
    ((matrix[B][3] & VAL_BITMASK) << B_SHIFT_2);
  byte row2 =
    ((matrix[R][4] & VAL_BITMASK) << R_SHIFT_1) |
    ((matrix[G][4] & VAL_BITMASK) << G_SHIFT_1) |
    ((matrix[B][4] & VAL_BITMASK) << B_SHIFT_1) |
    ((matrix[R][5] & VAL_BITMASK) << R_SHIFT_2) |
    ((matrix[G][5] & VAL_BITMASK) << G_SHIFT_2) |
    ((matrix[B][5] & VAL_BITMASK) << B_SHIFT_2);
  // Write to display
  lc.setRow(0, 0, row0);
  lc.setRow(0, 1, row1);
  lc.setRow(0, 2, row2);
}

// Write zeros to logical display matrix
void zeroMatrix(void) {
  for (uint8_t color = 0; color < MATRIX_COLORS; color++) {
    for (uint8_t column = 0; column < MATRIX_COLUMNS; column++) {
      matrix[color][column] = 0;
      matrix[color][column] = 0;
      matrix[color][column] = 0;
    }
  }
}

byte statusToLedState(byte status) {
  byte val;
  switch (status) {
    case STATUS_UNKNOWN:
      val = LED_OFF;
      break;
    case STATUS_VACANT:
      val = LED_VACANT;
      break;
    case STATUS_LOADED:
      val = LED_LOADED;
      break;
    case STATUS_UNLOADED:
      val = LED_UNLOADED;
      break;
    case STATUS_CLEARING:
      val = LED_CLEARING;
      break;
    case STATUS_INITIALIZING:
      val = LED_INITIALIZING;
      break;
    default:
      val = STATUS_UNKNOWN;
      break;
  }
  return val;
}

byte statusStringToInt(String status) {
  byte val;
  if (status == "UNKNOWN") {
    val = STATUS_UNKNOWN;
  } else if (status == "VACANT") {
    val = STATUS_VACANT;
  } else if (status == "LOADED") {
    val = STATUS_LOADED;
  } else if (status == "UNLOADED") {
    val = STATUS_UNLOADED;
  } else if (status == "CLEARING") {
    val = STATUS_CLEARING;
  } else if (status == "INITIALIZING") {
    val = STATUS_INITIALIZING;
  } else {
    val = STATUS_UNKNOWN;
  }
  return val;
}

void streamCallback(StreamData data) {
  if (DEBUG) {
    Serial.println("streamPath: " + data.streamPath());
    Serial.println("dataPath: " + data.dataPath());
    Serial.println("dataType: " + data.dataType());
    Serial.println("eventType: " + data.eventType());
    Serial.print("value: ");
  }
  if (data.dataType() == "int") {
    if (DEBUG) Serial.println(data.intData());
  }
  else if (data.dataType() == "float") {
    if (DEBUG) Serial.println(data.floatData(), 5);
  }
  else if (data.dataType() == "double") {
    if (DEBUG) printf("%.9lf\n", data.doubleData());
  }
  else if (data.dataType() == "boolean") {
    if (DEBUG) Serial.println(data.boolData() == 1 ? "true" : "false");
  }
  else if (data.dataType() == "string") {
    if (DEBUG) Serial.println(data.stringData());
    if (data.dataPath().startsWith("/data/") && data.dataPath().endsWith("/status")) {
      // matched "/data/n/status"
      // update slot status and LED state
      // [eschwartz-TODO] This assumes single digit slot number. Change to allow multi digit.
      uint8_t slot = data.dataPath().substring(6, 7).toInt();
      if ((slot >= 0) && (slot <= PORT_COUNT)) {
        byte status = statusStringToInt(data.stringData());
        ports[slot].status = status;
        setLedState(slot, statusToLedState(status));
      }
    }
  }
  else if (data.dataType() == "json") {
    // [eschwartz-TODO] Parse this and set status for each port upon startup
    // (it gets invoked when callback is set)
    if (DEBUG) Serial.println(data.jsonString());
  }
  else if (data.dataType() == "array") {
    if (DEBUG) Serial.println("got dataType() 'array'");
  }
  else if (data.dataType() == "null") {
    // something was deleted
    if (data.dataPath().startsWith("/data/") && !data.dataPath().endsWith("/status")) {
      // matched "/data/n" (if dataType is 'null', port was deleted)
      uint8_t slot = data.dataPath().substring(6, 7).toInt();
      if ((slot >= 0) && (slot <= PORT_COUNT)) {
        ports[slot].status = STATUS_UNKNOWN;
        setLedState(slot, statusToLedState(STATUS_UNKNOWN));
      }
    }
  }
}

void streamTimeoutCallback(bool timeout)
{
  if (timeout) {
    // Stream timeout occurred
    if (DEBUG) Serial.println("Stream timeout, resuming...");
  }  
}

#ifdef ENABLE_WEBSERVER
void handleRoot() {
  displayHtml();
}

// Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
void handleNotFound() {
 String message = "404: Not found\n\n";
 message += "URI: ";
 message += server.uri();
 message += "\nMethod: ";
 message += (server.method() == HTTP_GET) ? "GET" : "POST";
 message += "\nArguments: ";
 message += server.args();
 message += "\n";
 for (uint8_t i = 0; i < server.args(); i++) {
   message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
 }
 server.send(404, "text/plain", message);
}

String getDataSummaryHtml(void) {
  String output = "<table><tr><th>Slot</th><th>Weight (kg)</th><th>Calibration factor</th><th>Status</th></tr>";
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    output += "<tr><th>" + String(i) + "</th>";
    output += "<td>" + String(dtostrf(ports[i].last_weight_kilograms, 2, 4, buff)) + "</td>";
    output += "<td>" + String(ports[i].calibration_factor) + "</td>";
    output += "<td>" + String(ports[i].status) + "</td>";
    output += "</tr>";
  }
  output += "</table>";
  return output;
}

void displayHtml() {
  String output = "";
  // Display the HTML web page
  // Send HTTP status 200 (Ok) and send some text to the browser/client
  output += "<!DOCTYPE html><html>";
  output += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  output += "<link rel=\"icon\" href=\"data:,\">";
  output += "<style>";
  output += "html { font-family: Helvetica; display: inline-block; margin: 0px auto;}";
  output += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;";
  output += "text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}";
  output += "</style></head>";
  
  // [eschwartz-TODO] Add controls:
  // Pause/Resume sending
  // Reboot
  // Date/time

  // Web Page Heading
  output += "<body><h1>Cerealometer</h1>";
  output += "<p><a href=\"/\">Home</a></p>";
  output += "<h2>Configuration</h2>";
  output += "<p>Connected to: " + String(WiFi.SSID()) + "</p>";
  output += "<p>DEVICE_ID: " + String(DEVICE_ID) + "</p>";
  output += "<p>PORT_COUNT: " + String(PORT_COUNT) + "</p>";
  // output += "<p>IP address: ";
  // output +=  String(WiFi.localIP());
  // output += "</p>";
  output += "<p>REST_API_ENDPOINT: " + String(REST_API_ENDPOINT) + "</p>";
  output += "<p>WEIGHT_MEASURE_INTERVAL_MS: " + String(WEIGHT_MEASURE_INTERVAL_MS) + "</p>";
  output += "<p>STATS_WINDOW_LENGTH: " + String(STATS_WINDOW_LENGTH) + "</p>";
  output += "<p>PRESENCE_THRESHOLD_KG: " + String(PRESENCE_THRESHOLD_KG, 4) + "</p>";
  output += "<p>KG_CHANGE_THRESHOLD: " + String(KG_CHANGE_THRESHOLD, 4) + "</p>";
  output += "<p>STD_DEV_THRESHOLD: " + String(STD_DEV_THRESHOLD, 4) + "</p>";
  output += "<h2>Data</h2>";
  output += getDataSummaryHtml();
  output += "</body></html>";
  
  server.send(200, "text/html", output);
}
#endif

void setup(void) {
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');

  // Switch display from power savings mode to normal operation
  lc.shutdown(0, false);
  lc.setIntensity(0, DEFAULT_INTENSITY);
  lc.clearDisplay(0);
  zeroMatrix();  
  displayTest();

  // Initialize port structs
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    ports[i].clock_pin = hx711_clock_pins[i];
    ports[i].data_pin = hx711_data_pins[i];
    ports[i].calibration_factor = hx711_calibration_factors[i];
    ports[i].last_weight_kilograms = 0;
    ports[i].lastWeightMeasurementPushMillis = 0;
    ports[i].status = STATUS_INITIALIZING;
    setLedState(i, statusToLedState(STATUS_INITIALIZING));

    Serial.print("Setting up HX711 scale ");
    Serial.print(i);
    Serial.print(" on I/O pins ");
    Serial.print(ports[i].clock_pin);
    Serial.print(" (clock) and ");
    Serial.print(ports[i].data_pin);
    Serial.println(" (data)");
    ports[i].scale.begin(ports[i].data_pin, ports[i].clock_pin);
    ports[i].scale.set_scale();
    ports[i].scale.tare();
    ports[i].stats.clear();
    ports[i].status = STATUS_UNKNOWN; // initial status until data received from cloud
    setLedState(i, statusToLedState(STATUS_UNKNOWN));
  }

  Serial.print("Connecting to WiFi...");
  #ifdef USE_WIFI_MULTI
  // Add one for each network to connect to
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  //wifiMulti.addAP(WIFI_SSID2, WIFI_PASSWORD2);
  // Wait for WiFi connection; connect to strongest listed above
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  #endif
  #ifndef USE_WIFI_MULTI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  #endif
  
  Serial.println('\n');
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  #ifdef MDNS
  // Start the mDNS responder for esp8266.local
  if (MDNS.begin("esp8266")) {
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  #endif

  #ifdef ENABLE_WEBSERVER
  // Set up callbacks for client URIs
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.on("/status", HTTP_GET, handleStatus);
  // Start the HTTP server
  server.begin();
  Serial.println("HTTP server started");
  #endif
  
  Serial.println(F("Commands: [R|r] Reset board, [D|d] Display test, [T|t] Reset tares, [P|p] Print current values"));

  #ifdef USE_FIREBASE
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);
  // Set the size of WiFi RX/TX buffers
  //firebaseData.setBSSLBufferSize(1024, 1024);
  // Set the size of HTTP response buffers
  //firebaseData.setResponseSize(1024);
  // Set database read timeout to 1 minute (max 15 minutes)
  //Firebase.setReadTimeout(firebaseData, 1000 * 60);
  // Set write size limit and timeout: tiny (1s), small (10s), medium (30s), large (60s), or unlimited
  //Firebase.setwriteSizeLimit(firebaseData, "tiny");

  String path = "/ports/" + String(DEVICE_ID);
  Firebase.setStreamCallback(firebaseData, streamCallback, streamTimeoutCallback);

  if (!Firebase.beginStream(firebaseData, path)) {
    // unable to begin stream connection
    Serial.println(firebaseData.errorReason());
  }
  #endif
}

void loop(void) {
  #ifdef ENABLE_WEBSERVER
  // Listen for HTTP requests from clients
  server.handleClient();
  #endif
  if ((millis() - lastDisplayUpdateMillis) >= DISPLAY_UPDATE_INTERVAL_MS) {
    displayCounter++; 
    updateDisplay();
    lastDisplayUpdateMillis = millis();
  }

  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    float kg_change = 0;
    // Calibrate scale
    ports[i].scale.set_scale(ports[i].calibration_factor);
    // Get current weight reading and convert to kg (absolute value)
    float weight_pounds = ports[i].scale.get_units();
    float weight_kilograms = MAX(weight_pounds / POUNDS_PER_KILOGRAM, 0);
    // Add current reading to stats arrays
    ports[i].stats.add(weight_kilograms);
    // Save current reading
    ports[i].current_weight_kilograms = weight_kilograms;
    
    // Every STATS_WINDOW_LENGTH readings, check event trigger conditions
    if (ports[i].stats.count() == STATS_WINDOW_LENGTH) {
      if ((ports[i].stats.average() >= PRESENCE_THRESHOLD_KG) ||
          (ports[i].last_weight_kilograms >= PRESENCE_THRESHOLD_KG)) {
        kg_change = fabs(ports[i].stats.average() - ports[i].last_weight_kilograms);
      }
      /**
       * Print log message and send update when conditions are met:
       * - average reading exceeds PRESENCE_THRESHOLD_KG (i.e. object is present)
       * - average reading changed KG_CHANGE_THRESHOLD or more since last update
       * - standard deviation within 0.03 kg
       * - at least WEIGHT_MEASURE_INTERVAL_MS has passed since last update
       */
      if (!ports[i].lastWeightMeasurementPushMillis ||
            ((kg_change >= KG_CHANGE_THRESHOLD)
            && (ports[i].stats.pop_stdev() <= STD_DEV_THRESHOLD)
            && ((millis() - ports[i].lastWeightMeasurementPushMillis) >= WEIGHT_MEASURE_INTERVAL_MS))) {
        // Log it
        printLogMessage(i, kg_change);
        // Send new weight_kg to Realtime Database via HTTP function
        if (setWeight(DEVICE_ID, i, ports[i].stats.average())) {
          // success
          ports[i].last_weight_kilograms = ports[i].stats.average();
          ports[i].lastWeightMeasurementPushMillis = millis();
        }
        // Reset LED based on current port status
        setLedState(i, statusToLedState(ports[i].status));
      }
      ports[i].stats.clear();
    }
  }

  if (Serial.available()) {
    char key = Serial.read();
    if (key == 't' || key == 'T') {
      Serial.println("Resetting scales to 0...");
      resetTares();
    }
    if (key == 'r' || key == 'R') {
      Serial.println("Resetting device...");
      resetDevice();
    }
    if (key == 'p' || key == 'P') {
      Serial.println("Printing current values...");
      printCurrentValues();
    }
    if (key == 'd' || key == 'D') {
      Serial.println("Running display tests...");
      displayTest();
      Serial.println("...done.");
    }
  }
}
