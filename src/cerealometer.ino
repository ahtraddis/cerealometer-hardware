/**
 * Cerealometer
 * 
 * Copyright (c) 2020 Eric Schwartz
 */

// Enable/disable options
#define DEBUG false
#define USE_FIREBASE

#define BOOT_MESSAGE "Cerealometer v0.1 Copyright (c) 2020 Eric Schwartz"

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

#include "HX711-multi.h"

#include "Statistic.h"
#include <Wire.h> // Include the I2C library (required)
#include <SparkFunSX1509.h>
#include <jsonlib.h>

// Constants

#define PORT_COUNT 6
#define WEIGHT_MEASURE_INTERVAL_MS 500
#define STATS_WINDOW_LENGTH 10
#define PRESENCE_THRESHOLD_KG 0.001
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
#define LED_RED_BREATHE 9
#define LED_GREEN 10
#define LED_GREEN_BLINK 11
#define LED_GREEN_BLINK_FAST 12
#define LED_GREEN_BREATHE 13
#define LED_BLUE 14
#define LED_BLUE_BLINK 15
#define LED_BLUE_BLINK_FAST 16
#define LED_BLUE_BREATHE 17
#define LED_WHITE 18
#define LED_WHITE_BLINK 19
#define LED_WHITE_BLINK_FAST 20
#define LED_YELLOW 21
#define LED_PURPLE 22
#define LED_CYAN 23

// LED control
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
byte DATA_OUTS[6] = { HX711_DT0, HX711_DT1, HX711_DT2, HX711_DT3, HX711_DT4, HX711_DT5 };
#define CHANNEL_COUNT sizeof(DATA_OUTS) / sizeof(byte)
long int scale_results[CHANNEL_COUNT];
HX711MULTI scales(CHANNEL_COUNT, DATA_OUTS, HX711_CLK);

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
#ifdef USE_FIREBASE
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
const static bool rgbSeq[][COLOR_COUNT] = {
  { 1, 0, 0 }, // red
  { 0, 1, 0 }, // green
  { 0, 0, 1 }, // blue
  { 1, 1, 1 }, // white
  { 1, 1, 0 }, // yellow
  { 1, 0, 1 }, // purple
  { 0, 1, 1 }, // cyan
};

// Hardcoded load cell offsets and calibration factors, to be written to flash.
// Tare offset corresponds to the HX711 value when load is zero.
int hx711_tare_offsets[PORT_COUNT] = { 4288, 182437, 16573, 80423, -123745, 201301 };
// Calibration factor is the linear conversion factor to scale value to kilograms.
int hx711_calibration_factors[PORT_COUNT] = { -392309, -393188, -392518, -373189, -372550, -384838 };

char buff[10];
boolean enablePrintScaleData = false;
boolean enableLogData = false;

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

#ifdef ENABLE_WEBSERVER
void handleRoot();
void handleNotFound();
#endif

// declare board reset function at address 0
void(* resetDevice) (void) = 0;

void ledBreatheRow(uint8_t colorIndex=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeUpRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeDownRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledWaveRight(boolean r=0, boolean g=0, boolean b=0, int delayMs=100);
void ledRandom(int maxCount=1, int delayMs=0, int delayStepMs=1);
void setLedState(uint8_t pos, uint8_t state=LED_OFF, boolean sync=true);
void ledCylon(int count=10, int delayMs=150);

// Global functions

int getPortData(String device_id) {
  http.begin(REST_API_BASEURL + String("/getDevice?device_id=") + device_id);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    Serial.print(F("HTTP response code "));
    Serial.println(httpCode);
    String response = http.getString();
    String data_list = jsonExtract(response, "data");
    // Parse response, set status and LED state for each slot
    // [eschwartz-TODO] Only do this if len of array == PORT_COUNT
    for (uint8_t i = 0; i < PORT_COUNT; i++) {
      String slotStr = jsonIndexList(data_list, i);
      Serial.print("slot " + String(i) + ": ");
      Serial.println(slotStr);
      String statusStr = jsonExtract(slotStr, "status");
      byte status = statusStringToInt(statusStr);
      ports[i].status = status;
      setLedState(i, status);
    }
  } else {
    Serial.print(F("HTTP response error "));
    Serial.println(httpCode);
    String response = http.getString();
    Serial.println(response);
  }
  http.end();
}

int setWeight(String device_id, int slot, float weight_kg) {
  // Open https connection to setWeight cloud function
  // [eschwartz-TODO] Re-test with https, import SHA1_FINGERPRINT from config and add as 2nd param
  http.begin(REST_API_BASEURL + String("/setWeight"));
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
    Serial.print(F("Slot "));
    Serial.print(slot);
    Serial.print(F(" setWeight FAILED: httpCode = "));
    Serial.print(httpCode);
    Serial.print(F(", error = "));
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void printCurrentValues(void) {
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
void restoreLedStates(void) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, statusToLedState(ports[i].status));
  }
}

// Run LED test sequences
void displayTest(void) {
  int delayMs = 0;
  int delayStepMs = 1;
  byte onIntensity = 255;
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

// Set LED state
// To stop blink:
// io[device].setupBlink(pin, 0, 0, 255);
// See https://forum.sparkfun.com/viewtopic.php?t=48433)
// Notes:
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
      device = led_pins[pos][R][DEV];
      pin = led_pins[pos][R][PIN];
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
    Serial.println(F("___________ Stream callback data received __________"));
    Serial.println("streamPath: " + data.streamPath());
    Serial.println("dataPath:   " + data.dataPath());
    Serial.println("dataType:   " + data.dataType());
    Serial.println("eventType:  " + data.eventType());
    Serial.print(F(  "value:      "));
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
  //Serial.println("____________________________________________________");
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    // Stream timeout occurred
    //if (DEBUG) Serial.println("Stream timeout, resuming...");
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
  output += "<p>REST_API_BASEURL: " + String(REST_API_BASEURL) + "</p>";
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

void printCommandMenu() {
  Serial.println(F("COMMANDS: [H] Help [R] Reboot [D] Display tests [S] Sync LEDs"));
  Serial.println(F("[W] Toggle scale data log [P] Toggle port data log"));
  Serial.println(F("[0] Restore LED state [1] Blink reds [2] Blink greens [3] Blink blues"));
  Serial.println(F("[4] Whites on [5] Wave red [6] Wave green [7] Wave blue"));
}

// Note: Tare not used since device needs to be able to boot with load present.
// Using hardcoded offsets and calibration factors instead.
// void tare() {
//   bool tareSuccessful = false;
//   unsigned long tareStartTime = millis();
//   while (!tareSuccessful && millis() < (tareStartTime + TARE_TIMEOUT_MS)) {
//     tareSuccessful = scales.tare(20, 10000);  // reject 'tare' if still ringing
//   }
//   Serial.print("tare() ");
//   Serial.println(tareSuccessful ? "SUCCESS" : "FAIL");
// }

void printScaleData() {
  scales.read(scale_results);
  for (int i = 0; i < scales.get_count(); ++i) {
    Serial.print(scale_results[i]);
    Serial.print("\t (");
    Serial.print(1.0 * (scale_results[i] - hx711_tare_offsets[i]) / hx711_calibration_factors[i], 4);
    Serial.print(")");
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
      regMisc |= (1<<2);
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
    io[i].writeByte(REG_MISC, (regMisc & ~(1<<2)));
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');
  Serial.println(BOOT_MESSAGE);
  Serial.flush();

  randomSeed(analogRead(0)); // seed with random noise for misc functions

  // Initialize SX1509 I/O expanders
  for (uint8_t i = 0; i < SX1509_COUNT; i++) {
    Serial.print("Initializing SX1509 device ");
    Serial.print(i);
    Serial.print(" at address 0x");
    Serial.print(SX1509_I2C_ADDRESSES[i], HEX);
    Serial.print("...");
    if (!io[i].begin(SX1509_I2C_ADDRESSES[i])) {
      Serial.println(" FAILED");
      // [eschwartz-TODO] Set flag to skip I/O stuff or light an error LED
    } else {
      Serial.println(" success");
    }
  }
  // Set output freq.: 0x0: 0Hz (low), 0xF: 2MHz? (high),
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

  Serial.print("Connecting to WiFi...");
  // [eschwartz-TODO] Add timeout and LED error state upon failure
  #ifdef USE_WIFI_MULTI
  // Add one for each network to connect to
  for (uint8_t i = 0; i < LEN(WIFI_NETWORKS); i++) {
    wifiMulti.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
  }
  // Wait for WiFi connection; connect to strongest listed above
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  #endif
  #ifndef USE_WIFI_MULTI
  WiFi.begin(WIFI_NETWORKS[0][0], WIFI_NETWORKS[0][1]);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  #endif
  
  Serial.print("\nConnected to ");
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
  
  printCommandMenu();

  // Get initial port data via REST
  Serial.println("Fetching initial port data...");
  getPortData(DEVICE_ID);

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

  scales.read(scale_results);
  if (enablePrintScaleData) printScaleData();
  if (enableLogData) printCurrentValues();

  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    float kg_change = 0;
    // Get current weight reading (signed integer), apply offset and calibratation factor
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
    if ((key == 'h') || (key == 'H') || (key == '?')) {
      printCommandMenu();
    }
    // if (key == 't' || key == 'T') {
    //   Serial.println("Resetting scales to 0...");
    //   tare();
    // }
    if (key == 'r' || key == 'R') {
      Serial.println("Rebooting device...");
      resetDisplay();
      resetDevice();
    }
    if (key == 'p' || key == 'P') {
      enableLogData = !enableLogData;
      Serial.print("Data logging is ");
      Serial.println(enableLogData ? "ON" : "OFF");
    }
    if (key == 'w' || key == 'W') {
      enablePrintScaleData = !enablePrintScaleData;
      Serial.print("Scale data logging is ");
      Serial.println(enablePrintScaleData ? "ON" : "OFF");
    }
    if (key == 's' || key == 'S') {
      Serial.println("Synchronizing all LED outputs...");
      syncLeds();
    }
    if (key == 'd' || key == 'D') {
      Serial.println("Running display tests...");
      displayTest();
      Serial.println("...done.");
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
    if (key == 'x') {
      Serial.println("Clearing display...");
      for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
        setLedState(pos, LED_OFF);
      }
    }
    
  }
}
