/**
 * Cerealometer
 * 
 * Copyright (c) 2020 Eric Schwartz
 */

/**
 * TODO:
 * Test with incorrect device_id (all green LEDs?)
 */

// Enable/disable options
#define DEBUG false
//#define USE_FIREBASE_CALLBACK

#define BOOT_MESSAGE "Cerealometer v0.1 Copyright (c) 2020 Eric Schwartz"

// These non-essential features are disabled due to limited flash memory on the
// SparkFun Thing Dev board. Uncomment to enable them if your hardware so allows!
//#define ENABLE_MDNS
#define ENABLE_WEBSERVER
//#define USE_WIFI_MULTI

// Local config file including URLs and credentials
#include "config.h"
#include "templates.h"

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

#include "HX711-multi.h"
#include "Statistic.h"
#include <SparkFunSX1509.h>
#include <jsonlib.h>
#include <EEPROM.h>

// Constants

#define WEIGHT_MEASURE_INTERVAL_MS 500
#define STATS_WINDOW_LENGTH 10
#define PRESENCE_THRESHOLD_KG 0.001
#define CALIBRATION_WEIGHT_KG 0.1
// Minimum change of averaged weight_kg required to trigger data upload
#define KG_CHANGE_THRESHOLD 0.001
#define STD_DEV_THRESHOLD 0.03
// Slot statuses
#define STATUS_UNKNOWN 0
#define STATUS_INITIALIZING 1
#define STATUS_VACANT 2
#define STATUS_LOADED 3
#define STATUS_UNLOADED 4
#define STATUS_CLEARING 5
#define STATUS_CALIBRATING 6
// LED states
#define LED_OFF 0
#define LED_INITIALIZING 1
#define LED_VACANT 2
#define LED_LOADED 3
#define LED_UNLOADED 4
#define LED_CLEARING 5
#define LED_CALIBRATING 6
#define LED_RED 7
#define LED_RED_BLINK 8
#define LED_RED_BLINK_FAST 9
#define LED_RED_BREATHE 10
#define LED_GREEN 11
#define LED_GREEN_BLINK 12
#define LED_GREEN_BLINK_FAST 13
#define LED_GREEN_BREATHE 14
#define LED_BLUE 15
#define LED_BLUE_BLINK 16
#define LED_BLUE_BLINK_FAST 17
#define LED_BLUE_BREATHE 18
#define LED_WHITE 19
#define LED_WHITE_BLINK 20
#define LED_WHITE_BLINK_FAST 21
#define LED_YELLOW 22
#define LED_PURPLE 23
#define LED_CYAN 24

// LED control and timing
#define COLOR_COUNT 3
#define R 0
#define G 1
#define B 2
#define DEV 0 // array index for SX1509 device
#define PIN 1 // array index for SX1509 pin
#define BLINK_ON_MS 500
#define BLINK_OFF_MS 500
#define BLINK_FAST_ON_MS 150
#define BLINK_FAST_OFF_MS 150
#define BREATHE_ON_MS 1000
#define BREATHE_OFF_MS 500
#define BREATHE_RISE_MS 500
#define BREATHE_FALL_MS 250

// HX711 module pins (6 data lines, common clock)
/**
 * Note: GPIO15 (like GPIO0 and GPIO2) are also used to control boot options,
 * and needs to be held LOW when read upon boot, otherwise the ESP8266 will
 * try to boot from a non-existent SD card. Using the pin for the shared HX711
 * clock instead data works around this, presumably because the signal toggles
 * or becomes low when read upon boot. Not completely tested but seems stable.
 * See https://www.instructables.com/id/ESP8266-Using-GPIO0-GPIO2-as-inputs/
 */
#define HX711_CLK 15 // GPIO15
#define HX711_DT0 0 // GPIO0
#define HX711_DT1 4 // GPIO4
#define HX711_DT2 12 // GPIO12, MISO (Hardware SPI MISO)
#define HX711_DT3 13 // GPIO13, MOSI (Hardware SPI MOSI)
#define HX711_DT4 5 // GPIO5 (also tied to on-board LED)
#define HX711_DT5 16 // GPIO16, XPD (can be used to wake from deep sleep)

#define TARE_TIMEOUT_MS 2000 // less than ~3 secs to avoid tripping watchdog timer
byte DATA_OUTS[] = { HX711_DT0, HX711_DT1, HX711_DT2, HX711_DT3, HX711_DT4, HX711_DT5 };
#define PORT_COUNT sizeof(DATA_OUTS) / sizeof(byte)
long int scale_results[PORT_COUNT];
HX711MULTI scales(PORT_COUNT, DATA_OUTS, HX711_CLK);

// Misc
#define EEPROM_ADDR 0
#define EEPROM_LEN 512
#define WIFI_CONNECT_TIMEOUT_MS 8000

// Macros
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define LEN(arr) ((int) (sizeof(arr) / sizeof(arr)[0]))

// Globals

#ifdef USE_WIFI_MULTI
ESP8266WiFiMulti wifiMulti;
#endif
#ifdef ENABLE_WEBSERVER
// Create webserver object listening for HTTP requests on port 80
ESP8266WebServer server(80);
#endif
#ifdef USE_FIREBASE_CALLBACK
FirebaseData firebaseData;
#endif

HTTPClient http;

// SX1509 I/O expander array
byte SX1509_I2C_ADDRESSES[2] = { 0x3E, 0x70 };
#define SX1509_COUNT sizeof(SX1509_I2C_ADDRESSES) / sizeof(byte)
// device and pins used for external interrupt to toggle nReset lines
#define SX1509_INT_DEVICE 1
#define SX1509_INT_PIN 0
#define SX1509_INT_TRIGGER_PIN 1

SX1509 io[SX1509_COUNT];
// SX1509 device and pin mappings for the 6 RGB LEDs
// Each pair represents {device, pin} for the LED pins in RGB order
const int led_pins[PORT_COUNT][COLOR_COUNT][2] = {
  { {0, 4}, {1, 4}, {0, 14} },
  { {0, 5}, {1, 5}, {0, 15} },
  { {0, 6}, {1, 6}, {1, 14} },
  { {0, 7}, {1, 7}, {1, 15} },
  { {0, 12}, {1, 12}, {0, 0} },
  { {0, 13}, {1, 13}, {0, 1} },
};

// RGB color sequence used for display tests
const static boolean rgbSeq[][COLOR_COUNT] = {
  { 1, 0, 0 }, // red
  { 0, 1, 0 }, // green
  { 0, 0, 1 }, // blue
  { 1, 1, 1 }, // white
  { 1, 1, 0 }, // yellow
  { 1, 0, 1 }, // purple
  { 0, 1, 1 }, // cyan
};

char buff[10]; // for string formatting
// Serial command states
boolean enablePrintScaleData = false;
boolean enableLogData = false;
boolean enableLogUpdates = false;
uint8_t slotSelected = 0;

boolean rebootRequired = false;
unsigned long rebootTimeMillis = 0;

// Config and calibration data loaded from EEPROM (512 bytes max)
typedef struct {
  char device_id[32] = "";
  char wifi_ssid[64] = "";
  char wifi_password[64] = "";
  char firebase_project_id[64] = "";
  char firebase_db_secret[64] = "";
  long offsets[PORT_COUNT] = {0};
  int calibration_factors[PORT_COUNT] = {0};
} EepromData;

EepromData eepromData;

char* device_id = "";
char* wifi_ssid = "";
char* wifi_password = "";
char* firebase_project_id = "";
char* firebase_db_secret = "";
// Load cell offsets and calibration factors.
// Tare offset corresponds to the HX711 value with no load present.
long int hx711_tare_offsets[PORT_COUNT] = {0};
// Calibration factor is the linear conversion factor to scale value to kilograms.
int hx711_calibration_factors[PORT_COUNT] = {0};

typedef struct {
  int calibration_factor;
  float last_weight_kilograms; // last value uploaded
  float current_weight_kilograms; // most recent sample
  Statistic stats;
  byte data_pin;
  byte clock_pin;
  unsigned long lastWeightMeasurementPushMillis;
  byte status;
} Port;

Port ports[PORT_COUNT];
byte currentLedStates[PORT_COUNT];

// Function prototypes
void ledBreatheRow(uint8_t colorIndex=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeUpRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeDownRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledWaveRight(boolean r=0, boolean g=0, boolean b=0, int delayMs=100);
void ledRandom(int maxCount=1, int delayMs=0, int delayStepMs=1);
void setLedState(uint8_t pos, uint8_t state=LED_OFF, boolean sync=true);
void setAllLedStates(uint8_t state=LED_OFF, boolean sync=true);
void ledCylon(int count=10, int delayMs=150);
void getAverageScaleData(long *result, byte times=10);
#ifdef ENABLE_WEBSERVER
void handleRoot();
void handleSettings();
void handleCalibrate();
void handleUtil();
void handleLoading();
#endif

// Global functions

int getPortData(String device_id) {
  Serial.print("Fetching initial port data...");
  String url = REST_API_BASEURL_PATTERN;
  url.replace("%%FIREBASE_PROJECT_ID%%", firebase_project_id);
  http.begin(url + String("/getDevice?device_id=") + device_id);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    Serial.println(" success.");
    String response = http.getString();
    String data_list = jsonExtract(response, "data");
    // Parse response, set status and LED state for each slot
    // [eschwartz-TODO] Only do this if len of array == PORT_COUNT
    for (uint8_t i = 0; i < PORT_COUNT; i++) {
      String slotStr = jsonIndexList(data_list, i);
      if (DEBUG) {
        Serial.print("slot " + String(i) + ": ");
        Serial.println(slotStr);
      }
      String statusStr = jsonExtract(slotStr, "status");
      byte status = statusStringToInt(statusStr);
      ports[i].status = status;
      setLedState(i, status);
    }
  } else {
    Serial.println(" FAILED.");
    Serial.print("HTTP response error ");
    Serial.println(httpCode);
    String response = http.getString();
    Serial.println(response);
  }
  http.end();
}

int setWeight(String device_id, int slot, float weight_kg) {
  // Open https connection to setWeight cloud function
  String url = REST_API_BASEURL_PATTERN;
  url.replace("%%FIREBASE_PROJECT_ID%%", firebase_project_id);
  // [eschwartz-TODO] Re-test with https, import SHA1_FINGERPRINT from config (2nd param):
  http.begin(url + String("/setWeight"));
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
      Serial.print("Slot ");
      Serial.print(slot);
      Serial.print(" setWeight SUCCESS, httpCode = ");
      Serial.println(httpCode);
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

void printCurrentValues() {
  Serial.print("Weights (kg): ");
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    Serial.print(ports[i].current_weight_kilograms, 4);
    Serial.print((i != PORT_COUNT - 1) ? "\t" : "\n");
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

// Restore LED states after disrupting them with tests
void restoreLedStates() {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, statusToLedState(ports[i].status));
  }
}

// Run LED test sequences
void displayTest() {
  int delayMs = 0;
  int delayStepMs = 1;
  byte onIntensity = 255;

  Serial.println("Running display tests...");
  resetDisplay();
  ledCylon(5);
  delay(1000);
  ledBreatheRow(R, onIntensity, delayMs, delayStepMs);
  delay(100);
  ledBreatheRow(G, onIntensity, delayMs, delayStepMs);
  delay(100);
  ledBreatheRow(B, onIntensity, delayMs, delayStepMs);
  delay(100);
  // Fade in/out rows with simple color combinations
  delayStepMs = 0;
  for (uint8_t i = 0; i < LEN(rgbSeq); i++) {
    uint8_t r = rgbSeq[i][R];
    uint8_t g = rgbSeq[i][G];
    uint8_t b = rgbSeq[i][B];
    ledFadeUpRow(r, g, b, onIntensity, delayMs, delayStepMs);
    ledFadeDownRow(r, g, b, onIntensity, delayMs, delayStepMs);
    delay(500);
  }
  delay(100);
  ledRandom(50);
  resetDisplay();
  delay(1000);
  restoreLedStates();
  Serial.println("...done.");
}

void resetDisplay() {
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
    setLedState(pos, LED_OFF);
  }
}

void ledBreatheRow(uint8_t colorIndex, byte onIntensity, int delayMs, int delayStepMs)
{
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
    int device = led_pins[pos][colorIndex][DEV];
    int pin = led_pins[pos][colorIndex][PIN];
    for (int i = 0; i < (onIntensity + 1); i++) {
      io[device].analogWrite(pin, 255 - i); // syncing current, value is inverted
      delay(delayStepMs);
    }
    delay(delayMs);
    for (int i = onIntensity; i >= 0; i--) {
      io[device].analogWrite(pin, 255 - i);
      delay(delayStepMs);
    }
    delay(delayMs);
  }
}

void ledFadeUpRow(boolean r, boolean g, boolean b, byte onIntensity, int delayMs, int delayStepMs)
{
  for (int i = 0; i < (onIntensity + 1); i++) {
    for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
      if (r) io[led_pins[pos][R][DEV]].analogWrite(led_pins[pos][R][PIN], 255 - i);
      if (g) io[led_pins[pos][G][DEV]].analogWrite(led_pins[pos][G][PIN], 255 - i);
      if (b) io[led_pins[pos][B][DEV]].analogWrite(led_pins[pos][B][PIN], 255 - i);
    }
    delay(delayStepMs);
  }
  delay(delayMs);
}

void ledFadeDownRow(boolean r, boolean g, boolean b, byte onIntensity, int delayMs, int delayStepMs)
{
  for (int i = onIntensity; i >= 0; i--) {
    for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
      if (r) io[led_pins[pos][R][DEV]].analogWrite(led_pins[pos][R][PIN], 255 - i);
      if (g) io[led_pins[pos][G][DEV]].analogWrite(led_pins[pos][G][PIN], 255 - i);
      if (b) io[led_pins[pos][B][DEV]].analogWrite(led_pins[pos][B][PIN], 255 - i);
    }
    delay(delayStepMs);
  }
  delay(delayMs);
}

// Start LED wave pattern by setting all to breathe with delay in between
void ledWaveRight(boolean r, boolean g, boolean b, int delayMs) {
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
    if (r) io[led_pins[pos][R][DEV]].breathe(led_pins[pos][R][PIN], BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
    if (g) io[led_pins[pos][G][DEV]].breathe(led_pins[pos][G][PIN], BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
    if (b) io[led_pins[pos][B][DEV]].breathe(led_pins[pos][B][PIN], BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
    delay(delayMs);
  }
}

void ledRandom(int maxCount, int delayMs, int delayStepMs)
{
  int counter = 0;
  uint8_t rgbIndex, r, g, b, intensity, step, pos;
  while (counter++ < maxCount) {
    rgbIndex = random(0, LEN(rgbSeq));
    r = rgbSeq[rgbIndex][R];
    g = rgbSeq[rgbIndex][G];
    b = rgbSeq[rgbIndex][B];
    intensity = random(64, 255);
    step = 4;
    pos = random(0, PORT_COUNT);
    for (int i = 0; i < (intensity + 1); i += step) {
      if (r) io[led_pins[pos][R][DEV]].analogWrite(led_pins[pos][R][PIN], 255 - i);
      if (g) io[led_pins[pos][G][DEV]].analogWrite(led_pins[pos][G][PIN], 255 - i);
      if (b) io[led_pins[pos][B][DEV]].analogWrite(led_pins[pos][B][PIN], 255 - i);
      delay(delayStepMs);
    }
    delay(delayMs);
    for (int i = intensity; i >= 0; i -= step) {
      if (r) io[led_pins[pos][R][DEV]].analogWrite(led_pins[pos][R][PIN], 255 - i);
      if (g) io[led_pins[pos][G][DEV]].analogWrite(led_pins[pos][G][PIN], 255 - i);
      if (b) io[led_pins[pos][B][DEV]].analogWrite(led_pins[pos][B][PIN], 255 - i);
      delay(delayStepMs);
    }
    // turn off in case stepping missed the 0 value
    if (r) io[led_pins[pos][R][DEV]].analogWrite(led_pins[pos][R][PIN], 255);
    if (g) io[led_pins[pos][G][DEV]].analogWrite(led_pins[pos][G][PIN], 255);
    if (b) io[led_pins[pos][B][DEV]].analogWrite(led_pins[pos][B][PIN], 255);
    delay(delayMs);
  }
}

void ledCylon(int count, int delayMs) {
  int counter = 0;
  uint8_t device, pin, endPos;
  while (counter++ < count) {
    endPos = (counter == count) ? 0 : 1;
    for (int pos = 0; pos < PORT_COUNT; pos++) {
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].analogWrite(pin, 0);
      delay(delayMs);
      io[device].analogWrite(pin, 255);
    }
    for (int pos = (PORT_COUNT - 2); pos >= endPos; pos--) {
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].analogWrite(pin, 0);
      delay(delayMs);
      io[device].analogWrite(pin, 255);
    }
  }
}

void setAllLedStates(uint8_t state, boolean sync) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, state, sync);
  }
}

// Set LED state
// To stop blink:
// io[device].setupBlink(pin, 0, 0, 255);
// See https://forum.sparkfun.com/viewtopic.php?t=48433)
// Notes:
// State changes are a bit messy due to limitations on turning OFF blink
// and breathe via the SX1509.
// To stop a pin that is "breathing", do a digitalWrite() of HIGH to enter single-shot mode.
void setLedState(uint8_t pos, uint8_t state, boolean sync) {
  if (state == currentLedStates[pos]) return; // already set
  int device;
  int pin;
  switch (state) {
    case LED_RED:
    case LED_LOADED:
      // solid red ala Whole Foods parking lot
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
    case LED_RED_BLINK:
    case LED_UNLOADED:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
    case LED_RED_BLINK_FAST:
    case LED_CLEARING:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_FAST_ON_MS, BLINK_FAST_OFF_MS); // start blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // blue
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      break;
    case LED_RED_BREATHE:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].breathe(pin, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS); // start breathe
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      break;
    case LED_GREEN:
    case LED_VACANT:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // solid green ala Whole Foods parking lot
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
    case LED_GREEN_BLINK:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
    case LED_GREEN_BLINK_FAST:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_FAST_ON_MS, BLINK_FAST_OFF_MS); // start blink
      // blue
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      break;
    case LED_GREEN_BREATHE:
    case LED_INITIALIZING:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      io[device].breathe(pin, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS); // start breathe
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
    case LED_BLUE:
    case LED_CALIBRATING:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      break;
    case LED_BLUE_BLINK:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      break;    
    case LED_BLUE_BLINK_FAST:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      break;
    case LED_BLUE_BREATHE:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].digitalWrite(pin, LOW); // off
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].breathe(pin, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS); // start breathe
      break;
    case LED_WHITE:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      io[device].digitalWrite(pin, HIGH); // stop breathe and stay on
      break;
    case LED_WHITE_BLINK:
      // red
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      // green
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      // blue
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].blink(pin, BLINK_ON_MS, BLINK_OFF_MS); // start blink
      break;
    // case LED_YELLOW:
    //   // do something
    //   break;
    // case LED_PURPLE:
    //   // do something
    //   break;
    // case LED_CYAN:
    //   // do something
    //   break;
    case LED_OFF:
    default:
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      device = led_pins[pos][G][DEV];
      pin = led_pins[pos][G][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      device = led_pins[pos][B][DEV];
      pin = led_pins[pos][B][PIN];
      io[device].digitalWrite(pin, HIGH); // stop breathe
      io[device].setupBlink(pin, 0, 0, 255); // stop blink
      break;
  }
  currentLedStates[pos] = state;
  if (sync) syncLeds();
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
    case STATUS_CALIBRATING:
      val = LED_CALIBRATING;
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

String statusToString(byte status) {
  String str;
  switch (status) {
    case STATUS_UNKNOWN:
      str = "UNKNOWN";
      break;
    case STATUS_VACANT:
      str = "VACANT";
      break;
    case STATUS_LOADED:
      str = "LOADED";
      break;
    case STATUS_UNLOADED:
      str = "UNLOADED";
      break;
    case STATUS_CLEARING:
      str = "CLEARING";
      break;
    case STATUS_INITIALIZING:
      str = "INITIALIZING";
      break;
    default:
      str = "UNKNOWN";
      break;
  }
  return str;
}

void streamCallback(StreamData data) {
  if (DEBUG) {
    Serial.println("___________ Stream callback data received __________");
    Serial.println("streamPath: " + data.streamPath());
    Serial.println("dataPath:   " + data.dataPath());
    Serial.println("dataType:   " + data.dataType());
    Serial.println("eventType:  " + data.eventType());
    Serial.print(  "value:      ");
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
      // Matched "/data/n/status", update slot status and LED state
      // [eschwartz-TODO] This assumes single digit slot number. Change to allow multi digit.
      uint8_t slot = data.dataPath().substring(6, 7).toInt();
      if ((slot >= 0) && (slot < PORT_COUNT)) {
        byte status = statusStringToInt(data.stringData());
        ports[slot].status = status;
        setLedState(slot, statusToLedState(status));
      }
    }
  }
  else if (data.dataType() == "json") {
    // [eschwartz-TODO] Parse this and set status for each port upon startup
    // (it gets invoked when callback is set)
    Serial.println("INITIAL JSON:");
    Serial.println(data.jsonString());
    //if (DEBUG) Serial.println(data.jsonString());
  }
  else if (data.dataType() == "array") {
    if (DEBUG) Serial.println("got dataType() 'array'");
  }
  else if (data.dataType() == "null") {
    // something was deleted
    if (data.dataPath().startsWith("/data/") && !data.dataPath().endsWith("/status")) {
      // matched "/data/n" (if dataType is 'null', port was deleted)
      // [eschwartz-TODO] This assumes single digit slot number. Change to allow multi digit.
      uint8_t slot = data.dataPath().substring(6, 7).toInt();
      if ((slot >= 0) && (slot < PORT_COUNT)) {
        ports[slot].status = STATUS_UNKNOWN;
        setLedState(slot, statusToLedState(STATUS_UNKNOWN));
      }
    }
  }
}

void streamTimeoutCallback(boolean timeout) {
  if (timeout) {
    // Stream timeout occurred
    //if (DEBUG) Serial.println("Stream timeout, resuming...");
  }  
}

#ifdef ENABLE_WEBSERVER

String styleHtml() {
  String html = styleHtmlTemplate;
  return html;
}

String scriptHtml() {
  String html = scriptHtmlTemplate;
  return html;
}

String headerHtml() {
  String html = headerHtmlTemplate;
  html.replace("{STYLE}", styleHtml());
  html.replace("{SCRIPT}", scriptHtml());
  return html;
}

String footerHtml() {
  String html = footerHtmlTemplate;
  return html;
}

String settingsHtml() {
  String html = settingsHtmlTemplate;
  html.replace("{DEVICE_ID}", device_id);
  html.replace("{WIFI_SSID}", wifi_ssid);
  html.replace("{WIFI_PASSWORD}", wifi_password);
  html.replace("{FIREBASE_PROJECT_ID}", firebase_project_id);
  html.replace("{FIREBASE_DB_SECRET}", firebase_db_secret);
  return html;
}

String calibrateHtml() {
  String html = calibrateHtmlTemplate;
  String tableRowsHtml = "";
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    String rowHtml = calibrateRowHtmlTemplate;
    rowHtml.replace("{PORT}", String(i + 1));
    rowHtml.replace("{LAST_WEIGHT_KILOGRAMS}", String(dtostrf(ports[i].last_weight_kilograms, 2, 4, buff)));
    rowHtml.replace("{TARE_OFFSET}", String(hx711_tare_offsets[i]));
    rowHtml.replace("{CALIBRATION_FACTOR}", String(hx711_calibration_factors[i]));
    rowHtml.replace("{STATUS}", statusToString(ports[i].status));
    tableRowsHtml += rowHtml;
  }
  html.replace("{TABLE_ROWS}", tableRowsHtml);
    
  return html;
}

String utilHtml() {
  String html = utilHtmlTemplate;
  return html;
}

String loadingHtml() {
  String html = loadingHtmlTemplate;
  html.replace("{STYLE}", styleHtml());
  // [eschwartz-TODO] Not sure why but these URLs are breaking the raw string when inserted in the template
  html.replace("{URL1}", "https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js");
  html.replace("{URL2}", "https://loading.io/mod/spinner/spinner/index.svg");
  return html;
}

void handleRoot() {
  // consider /settings the root
  server.sendHeader("Location", "/settings");
  server.send(302);
}

void handleSettings() {
  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + settingsHtml() + footerHtml());
  }
  else if (server.method() == HTTP_POST) {
    boolean updateRequired = false;
    // [eschwartz-TODO] Validate all inputs
    if (server.hasArg("device_id")) {
      String str_device_id = server.arg("device_id");
      if (str_device_id.length() > 0 && str_device_id.length() < 32) {
        Serial.println("Updating device ID...");
        strcpy(device_id, str_device_id.c_str());
        updateRequired = true;
      }
    }
    if (server.hasArg("firebase_project_id")) {
      String str_firebase_project_id = server.arg("firebase_project_id");
      if (str_firebase_project_id.length() > 0 && str_firebase_project_id.length() < 64) {
        Serial.println("Updating Firebase project ID...");
        strcpy(firebase_project_id, str_firebase_project_id.c_str());
        updateRequired = true;
      }
    }
    if (server.hasArg("firebase_db_secret")) {
      String str_firebase_db_secret = server.arg("firebase_db_secret");
      if (str_firebase_db_secret.length() > 0 && str_firebase_db_secret.length() < 64) {
        Serial.println("Updating Firebase database secret...");
        strcpy(firebase_db_secret, str_firebase_db_secret.c_str());
        updateRequired = true;
      }
    }
    if (server.hasArg("wifi_ssid")) {
      String str_wifi_ssid = server.arg("wifi_ssid");
      if (str_wifi_ssid.length() > 0 && str_wifi_ssid.length() < 32) {
        Serial.println("Updating WiFi SSID...");
        strcpy(wifi_ssid, str_wifi_ssid.c_str());
        updateRequired = true;
      }
    }
    if (server.hasArg("wifi_password")) {
      String str_wifi_password = server.arg("wifi_password");
      if (str_wifi_password.length() > 0 && str_wifi_password.length() < 32) {
        Serial.println("Updating WiFi password...");
        strcpy(wifi_password, str_wifi_password.c_str());
        updateRequired = true;
      }
    }

    // Redirect to loading page for reboot
    server.sendHeader("Location", "/loading");
    server.send(303);
    
    if (updateRequired) {
      writeEeprom();
      // Schedule reboot via loop() so this handler can exit
      rebootRequired = true;
      rebootTimeMillis = millis() + 2000;
    }
  } else {
    server.send(405, "text/html", "Method Not Allowed");
  }
}

void handleCalibrate() {
  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + calibrateHtml() + footerHtml());
  }
  else if (server.method() == HTTP_POST) {
    if (server.hasArg("id")) {
      // Calibrate button clicked
      int factor = calibrateScaleFactor(server.arg("id").toInt());
      server.send(200, "text/html", String(factor));
    }
    if (server.hasArg("offsets")) {
      // Tare Scales button clicked 
      calibrateOffsets();
      server.sendHeader("Location", "/loading");
      server.send(303);
      // Schedule reboot via loop() so this handler can exit
      rebootRequired = true;
      rebootTimeMillis = millis() + 2000;
    }
  }
  else {
    server.send(405, "text/html", "Method Not Allowed");
  }
}

void handleUtil() {
  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + utilHtml() + footerHtml());
  }
  else if (server.method() == HTTP_POST) {
    if (server.hasArg("reboot")) {
      // Reboot button clicked, redirect to loading page
      server.sendHeader("Location", "/loading");
      server.send(303);
      // Schedule reboot via loop() so this handler can exit
      rebootRequired = true;
      rebootTimeMillis = millis() + 2000;
    }
    if (server.hasArg("displaytest")) {
      // Run Display Tests button clicked, redirect to self
      server.sendHeader("Location", "/util");
      server.send(303);
      displayTest();
    }
  }
  else {
    server.send(405, "text/html", "Method Not Allowed");
  }
}

void handleLoading() {
  server.send(200, "text/html", loadingHtml());
}
// end of #ifdef ENABLE_WEBSERVER
#endif

void printSerialCommandMenu() {
  Serial.println("COMMANDS:");
  Serial.println("[H] Help menu [R] Reboot [D] Display tests [W] Write EEPROM");
  Serial.println("[L] Toggle update log [S] Toggle scale data log [P] Toggle port data log");
  Serial.println("[0] Restore LED state [1] Blink red [2] Blink green [3] Blink blue");
  Serial.println("[4] White on [5] Wave red [6] Wave green [7] Wave blue");
  Serial.println("[O] Calib offsets [+] Select slot (+) [-] Select slot (-) [F] Calib slot " + String(slotSelected));
  Serial.println("TEMP DEBUG OUTPUT: eepromData (use W to commit changes):");
  printEepromData();
}

void calibrateOffsets() {
  long int results[PORT_COUNT] = {0};
  uint8_t i;
  Serial.println("Calibrating offsets: sampling averages...");
  setAllLedStates(LED_CALIBRATING);
  getAverageScaleData(results, 10);
  for (i = 0; i < PORT_COUNT; i++) {
    hx711_tare_offsets[i] = results[i];
    // stage data for writing
    eepromData.offsets[i] = results[i];
  }
  writeEeprom();
  restoreLedStates();
}

int calibrateScaleFactor(uint8_t slot) {
  long int results[PORT_COUNT] = {0};
  Serial.println("Calibrating scale factor for Slot " + String(slot));
  setLedState(slot, LED_CALIBRATING);
  getAverageScaleData(results, 10);
  restoreLedStates();
  hx711_calibration_factors[slot] = 1.0 * (results[slot] - hx711_tare_offsets[slot]) / CALIBRATION_WEIGHT_KG;
  // stage EEPROM data for writing
  eepromData.calibration_factors[slot] = hx711_calibration_factors[slot];
  return hx711_calibration_factors[slot];
}

void getAverageScaleData(long *result, byte times) {
  long values[PORT_COUNT];
  uint8_t count, i;
  Statistic calStats[PORT_COUNT];

  for (i = 0; i < scales.get_count(); i++) {
    calStats[i].clear();
  }
  for (count = 0; count < times; count++) {
    scales.read(values);
    for (i = 0; i < scales.get_count(); i++) {
      calStats[i].add(values[i]);
    }
  }
  Serial.println("Slot\tCount\tAvg\tMin\tMax\tStdev");
  Serial.println("----\t-----\t---\t---\t---\t-----");
  for (i = 0; i < scales.get_count(); i++) {
    Serial.print(i);
    Serial.print('\t');
    Serial.print(calStats[i].count());
    Serial.print('\t');
    Serial.print((int)calStats[i].average());
    Serial.print('\t');
    Serial.print(calStats[i].minimum(), 0);
    Serial.print('\t');
    Serial.print(calStats[i].maximum(), 0);
    Serial.print('\t');
    Serial.println(calStats[i].pop_stdev(), 0);
  }

	// set the offsets
	for (i = 0; i < PORT_COUNT; i++) {
		result[i] = (int)calStats[i].average();
	}
}

void printScaleData() {
  scales.read(scale_results);
  for (uint8_t i = 0; i < scales.get_count(); ++i) {
    Serial.print(scale_results[i]);
    Serial.print("\t");
    Serial.print(1.0 * (scale_results[i] - hx711_tare_offsets[i]) / hx711_calibration_factors[i], 4);
    Serial.print((i != scales.get_count() - 1) ? "\t" : "\n");
  }
}

/**
 * Reset SX1509 internal counters to synchronize LED operation (blinking, fading)
 * across multiple devices. This is a modification of sync() in the SX1509 library,
 * using one of the SX1509 interrupt outputs to toggle the shared nReset lines.
 * Note: Requires moving readByte() and writeByte() to public in SX1509 class.
 */
void syncLeds() {
  #define REG_MISC 0x1F // RegMisc Miscellaneous device settings register 0000 0000

  // First set nReset functionality (reset counters instead of POR) on each SX1509
  byte regMisc;
  for (uint8_t i = 0; i < SX1509_COUNT; i++) {
    regMisc = io[i].readByte(REG_MISC);
    if (!(regMisc & 0x04)) {
      regMisc |= (1 << 2);
      io[i].writeByte(REG_MISC, regMisc);
    }
  }

  // clear any pending interrupt
  unsigned int intStatus = io[SX1509_INT_DEVICE].interruptSource();  
  // Write a LOW (wired to SX1509_INT_PIN) to trigger the falling edge interrupt
  io[SX1509_INT_DEVICE].digitalWrite(SX1509_INT_TRIGGER_PIN, LOW);
  //delay(1);
  io[SX1509_INT_DEVICE].digitalWrite(SX1509_INT_TRIGGER_PIN, HIGH);

	// Return nReset to POR functionality on each SX1509
  for (uint8_t i = 0; i < SX1509_COUNT; i++) {
    regMisc = io[i].readByte(REG_MISC);
    io[i].writeByte(REG_MISC, (regMisc & ~(1 << 2)));
  }
}

void printEepromData() {
  Serial.println("device_id:           '" + String(eepromData.device_id) + "'");
  Serial.println("wifi_ssid:           '" + String(eepromData.wifi_ssid) + "'");
  Serial.println("wifi_password:       '" + String(eepromData.wifi_password) + "'");
  Serial.println("firebase_project_id: '" + String(eepromData.firebase_project_id) + "'");
  Serial.println("firebase_db_secret:  '" + String(eepromData.firebase_db_secret) + "'");
  Serial.println("Slot\tOffset\tCal factor");
  Serial.println("----\t------\t----------");
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    Serial.print(i);
    Serial.print('\t');
    Serial.print(eepromData.offsets[i]);
    Serial.print('\t');
    Serial.print(eepromData.calibration_factors[i]);
    Serial.println();
  }
}

void readEeprom() {
  Serial.println("Reading EEPROM data...");
  EEPROM.begin(EEPROM_LEN);
  EEPROM.get(EEPROM_ADDR, eepromData);
  printEepromData();
  device_id = eepromData.device_id;
  wifi_ssid = eepromData.wifi_ssid;
  wifi_password = eepromData.wifi_password;
  firebase_project_id = eepromData.firebase_project_id;
  firebase_db_secret = eepromData.firebase_db_secret;
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    hx711_tare_offsets[i] = eepromData.offsets[i];
    hx711_calibration_factors[i] = eepromData.calibration_factors[i];
  }
}

void writeEeprom() {
  Serial.print("Writing EEPROM data to addr 0x" + String(EEPROM_ADDR, HEX));
  Serial.println("...");
  strncpy(eepromData.device_id, device_id, 32);
  strncpy(eepromData.wifi_ssid, wifi_ssid, 64);
  strncpy(eepromData.wifi_password, wifi_password, 64);
  strncpy(eepromData.firebase_project_id, firebase_project_id, 64);
  strncpy(eepromData.firebase_db_secret, firebase_db_secret, 64);

  printEepromData();
  // commit 512 bytes (EEPROM_LEN) of ESP8266 flash (for "EEPROM" emulation)
  // this step actually loads the content (512 bytes) of flash into 
  // a 512-byte-array cache in RAM
  EEPROM.begin(EEPROM_LEN);
  // replace values in byte-array cache with modified data
  // no changes made to flash, all in local byte-array cache
  EEPROM.put(EEPROM_ADDR, eepromData);
  // actually write the content of byte-array cache to
  // hardware flash.  flash write occurs if and only if one or more byte
  // in byte-array cache has been changed, but if so, ALL 512 bytes are 
  // written to flash
  if (EEPROM.commit()) {
    Serial.println("commit succeeded.");
  } else {
    Serial.println("commit FAILED.");
  }
}

void setupAP() {
  Serial.print("SoftAP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Setting up access point with SSID '" + String(WIFI_AP_SSID) + "'...");
  boolean result = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  if (result) {
    Serial.println("success.");
  } else {
    Serial.println("FAILED.");
  }
}

boolean wifiConnect() {
  unsigned long connectingMillis = millis();
  // [eschwartz-TODO] Add LED error state upon failure
  WiFi.begin(wifi_ssid, wifi_password);
  while ((millis() - connectingMillis) < WIFI_CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(500);
    Serial.print('.');
  }
  // connect timed out
  return false;
}

/**
 * Subscribe to Firebase Realtime Database stream callbacks on /ports/<device_id>.
 * This is needed to be able to update in realtime to slot status changes
 * driven by the app or cloud functions.
 */
#ifdef USE_FIREBASE_CALLBACK
void setupFirebaseCallback() {
  String url = FIREBASE_HOST_PATTERN;
  url.replace("%%FIREBASE_PROJECT_ID%%", firebase_project_id);
  Firebase.begin(url, firebase_db_secret);
  Firebase.reconnectWiFi(true);
  // Set the size of WiFi RX/TX buffers
  //firebaseData.setBSSLBufferSize(1024, 1024);
  // Set the size of HTTP response buffers
  //firebaseData.setResponseSize(1024);
  // Set database read timeout to 1 minute (max 15 minutes)
  //Firebase.setReadTimeout(firebaseData, 1000 * 60);
  // Set write size limit and timeout: tiny (1s), small (10s), medium (30s), large (60s), or unlimited
  //Firebase.setwriteSizeLimit(firebaseData, "tiny");
  Firebase.setStreamCallback(firebaseData, streamCallback, streamTimeoutCallback);

  String path = "/ports/" + String(device_id);
  if (!Firebase.beginStream(firebaseData, path)) {
    // unable to begin stream connection
    Serial.println(firebaseData.errorReason());
  }
}
#endif

void reboot() {
  resetDisplay();
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');
  Serial.println(BOOT_MESSAGE);
  Serial.flush();

  readEeprom();
  randomSeed(analogRead(0)); // seed with random noise for misc functions

  // Initialize SX1509 I/O expanders
  for (uint8_t i = 0; i < SX1509_COUNT; i++) {
    Serial.print("Initializing SX1509 device " + String(i) + " at address 0x");
    Serial.print(SX1509_I2C_ADDRESSES[i], HEX);
    Serial.print("...");
    if (!io[i].begin(SX1509_I2C_ADDRESSES[i])) {
      Serial.println(" FAILED.");
      // [eschwartz-TODO] Set flag to skip I/O stuff or light an error LED
    } else {
      Serial.println(" success.");
    }
  }
  // Set output frequency: 0x0: 0Hz (low), 0xF: 2MHz? (high),
  // 0x1-0xE: fOSCout = Fosc / 2 ^ (outputFreq - 1) Hz
  byte outputFreq = 0xF;
  io[0].clock(INTERNAL_CLOCK_2MHZ, 2, OUTPUT, outputFreq);
  io[1].clock(EXTERNAL_CLOCK, 2, INPUT);

  // Setup input pin to trigger external interrupt on shared nReset lines
  io[SX1509_INT_DEVICE].pinMode(SX1509_INT_PIN, INPUT_PULLUP);
  io[SX1509_INT_DEVICE].enableInterrupt(SX1509_INT_PIN, FALLING);
  // Setup an output pin (wired to SX1509_INT_PIN) to trigger it
  io[SX1509_INT_DEVICE].pinMode(SX1509_INT_TRIGGER_PIN, OUTPUT);

  // Set up all LED pins as ANALOG_OUTPUTs
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
    for (uint8_t color = 0; color < COLOR_COUNT; color++) {
      uint8_t device = led_pins[pos][color][DEV];
      uint8_t pin = led_pins[pos][color][PIN];
      io[device].pinMode(pin, ANALOG_OUTPUT);      
    }
  }

  // Start LED wave to indicate initializing
  ledWaveRight(1, 0, 0);

  // Initialize port structs
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    ports[i].last_weight_kilograms = 0;
    ports[i].lastWeightMeasurementPushMillis = 0;
    // Set initial status until data is received from cloud
    ports[i].status = STATUS_INITIALIZING;
    ports[i].stats.clear();
  }

  Serial.print("SoftAP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Connecting to WiFi...");
  if (wifiConnect()) {
    Serial.print("\nConnected to " + WiFi.SSID());
    Serial.print("IP address: " + WiFi.localIP());
  } else {
    Serial.println("connection timed out, setting up access point...");
    setupAP();
  }
  
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
  server.on("/settings", handleSettings);
  server.on("/calib", handleCalibrate);
  server.on("/util", handleUtil);
  server.on("/loading", handleLoading);
  server.onNotFound(handleRoot);
  // Start the HTTP server
  server.begin();
  Serial.println("HTTP server started");
  #endif
  
  // Get initial port data via REST call to Firebase
  if (WiFi.status() == WL_CONNECTED) {
    getPortData(device_id);
  }

  #ifdef USE_FIREBASE_CALLBACK
  setupFirebaseCallback();
  #endif

  printSerialCommandMenu();
}

void loop() {
  #ifdef ENABLE_WEBSERVER
  // Listen for HTTP requests from clients
  server.handleClient();
  #endif

  scales.read(scale_results);
  if (enablePrintScaleData) printScaleData();
  if (enableLogData) printCurrentValues();

  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    float kg_change = 0;
    // Get current weight reading, apply offset and calibratation factor
    float weight_kilograms = MAX(1.0 * (scale_results[i] - hx711_tare_offsets[i]) / hx711_calibration_factors[i], 0);
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
        if (enableLogUpdates) printLogMessage(i, kg_change);
        if (WiFi.status() == WL_CONNECTED) {
          // Send new weight_kg to Realtime Database via HTTP function
          if (setWeight(device_id, i, ports[i].stats.average())) {
            // success
            ports[i].last_weight_kilograms = ports[i].stats.average();
            ports[i].lastWeightMeasurementPushMillis = millis();
          }
        } else {
          Serial.println("No WiFi connection, skipping update");
        }
        // Reset LED based on current port status
        setLedState(i, statusToLedState(ports[i].status));
      }
      ports[i].stats.clear();
    }
  }

  if (Serial.available()) {
    char key = Serial.read();
    if ((key == 'h') || (key == 'H') || (key == '?')) {
      printSerialCommandMenu();
    }
    if (key == 'r' || key == 'R') {
      Serial.println("Rebooting device...");
      reboot();
    }
    if (key == 'p' || key == 'P') {
      enableLogData = !enableLogData;
      Serial.print("Data logging is ");
      Serial.println(enableLogData ? "ON" : "OFF");
    }
    if (key == 's' || key == 'S') {
      enablePrintScaleData = !enablePrintScaleData;
      Serial.print("Scale data logging is ");
      Serial.println(enablePrintScaleData ? "ON" : "OFF");
      if (enablePrintScaleData) {
        for (uint8_t i = 0; i < PORT_COUNT; i++) {
          Serial.print("s" + String(i) + " raw\ts" + String(i) + " kg\t");
        }
        Serial.print("\n");
      }
    }
    if (key == 'l' || key == 'L') {
      enableLogUpdates = !enableLogUpdates;
      Serial.print("Update logging is ");
      Serial.println(enableLogUpdates ? "ON" : "OFF");
    }
    if (key == 'w' || key == 'W') {
      writeEeprom();
    }
    if (key == 'd' || key == 'D') {
      displayTest();
    }
    if ((key == 'o') || (key == 'O')) {
      calibrateOffsets();
    }
    if ((key == 'f') || (key == 'F')) {
      calibrateScaleFactor(slotSelected);
    }
    if (key == '0') {
      Serial.println("Restoring LED state...");
      restoreLedStates();
    }
    if (key == '1') {
      Serial.println("Setting all LEDs to LED_RED_BLINK state...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_RED_BLINK);
      }
    }
    if (key == '2') {
      Serial.println("Setting all LEDs to LED_GREEN_BLINK state...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_GREEN_BLINK);
      }
    }
    if (key == '3') {
      Serial.println("Setting all LEDs to LED_BLUE_BLINK state...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_BLUE_BLINK);
      }
    }
    if (key == '4') {
      Serial.println("Setting all LEDs to LED_WHITE state...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_WHITE);
      }
    }
    if (key == '5') {
      Serial.println("Setting red wave pattern...");
      ledWaveRight(1, 0, 0);
    }
    if (key == '6') {
      Serial.println("Setting green wave pattern...");
      ledWaveRight(0, 1, 0);
    }
    if (key == '7') {
      Serial.println("Setting blue wave pattern...");
      ledWaveRight(0, 0, 1);
    }
    if (key == '+') {
      if (slotSelected < (PORT_COUNT - 1)) slotSelected++;
      Serial.println("Slot " + String(slotSelected) + " is selected");
    }
    if (key == '-') {
      if (slotSelected > 0) slotSelected--;
      Serial.println("Slot " + String(slotSelected) + " is selected");
    }
    if (key == 'x') {
      Serial.println("Clearing display...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_OFF);
      }
    }
  }

  if (rebootRequired && (millis() >= rebootTimeMillis)) {
    //writeEeprom();
    reboot();
  }
}
