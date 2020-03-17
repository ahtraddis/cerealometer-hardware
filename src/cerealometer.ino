/**
 * Cerealometer
 * 
 * Copyright (c) 2020 Eric Schwartz
 */

// Configuration options

//#define DEBUG
//#define USE_FIREBASE_CALLBACK
#define ENABLE_WEBSERVER
// These non-essential features are disabled due to limited flash memory on the
// SparkFun Thing Dev board. Uncomment to enable them if your hardware so allows!
//#define ENABLE_MDNS
#define ENABLE_SERIAL_COMMANDS
#define ENABLE_CALIBRATE
//#define ENABLE_LED_EFFECTS

// Local config file including URLs and credentials
#include "config.h"
#include "debug.h"
#ifdef ENABLE_WEBSERVER
#include "templates.h"
#endif

// FirebaseESP8266.h must be included before ESP8266Wifi.h
#include "FirebaseESP8266.h" // v2.7.8 originally tested
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
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

// Boot message for cereal[sic] console
#define BOOT_MESSAGE "Cerealometer v0.1 Copyright (c) 2020 Eric Schwartz"
// Minimum time between weight POST for a given slot
#define WEIGHT_MEASURE_INTERVAL_MS 500
// Number of scale readings to take before averaging
#define STATS_WINDOW_LENGTH 10
// Weight threshold above which object is considered present
#define PRESENCE_THRESHOLD_KG 0.001
// Expected weight to use for scale calibration
#define CALIBRATION_WEIGHT_KG 0.1
// Minimum change of averaged weight_kg required to trigger data upload
#define KG_CHANGE_THRESHOLD 0.001
// Maximum deviation for weight reading to settle
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
#define LED_GREEN_BLINK_FAST 13 // not used
#define LED_GREEN_BREATHE 14
#define LED_BLUE 15
#define LED_BLUE_BLINK 16
#define LED_BLUE_BLINK_FAST 17 // not used
#define LED_BLUE_BREATHE 18
#define LED_WHITE 19
#define LED_WHITE_BLINK 20 // not used
#define LED_WHITE_BLINK_FAST 21 // not used
#define LED_YELLOW 22 // not used
#define LED_PURPLE 23 // not used
#define LED_CYAN 24 // not used

uint8_t STATUS_TO_LED_STATE[] = {
  LED_OFF,          // 0 STATUS_UNKNOWN
  LED_INITIALIZING, // 1 STATUS_INITIALIZING
  LED_VACANT,       // 2 STATUS_VACANT
  LED_LOADED,       // 3 STATUS_LOADED
  LED_UNLOADED,     // 4 STATUS_UNLOADED
  LED_CLEARING,     // 5 STATUS_CLEARING
  LED_CALIBRATING   // 6 STATUS_CALIBRATING
};

// LED control and timing
#define COLOR_COUNT 3
#define R 0 // array index for red
#define G 1 // array index for green
#define B 2 // array index for blue
// Macros to minimize repeated variable assignment
#define RDEV led_pins[pos][R][DEV]
#define GDEV led_pins[pos][G][DEV]
#define BDEV led_pins[pos][B][DEV]
#define RPIN led_pins[pos][R][PIN]
#define GPIN led_pins[pos][G][PIN]
#define BPIN led_pins[pos][B][PIN]
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

byte DATA_OUTS[] = { HX711_DT0, HX711_DT1, HX711_DT2, HX711_DT3, HX711_DT4, HX711_DT5 };
#define PORT_COUNT sizeof(DATA_OUTS) / sizeof(byte)
long int scale_results[PORT_COUNT];
HX711MULTI scales(PORT_COUNT, DATA_OUTS, HX711_CLK);

// Misc
#define EEPROM_ADDR 0
#define EEPROM_LEN 512
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define REBOOT_DELAY_MILLIS 2000

// Macros
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define LEN(arr) ((int) (sizeof(arr) / sizeof(arr)[0]))

// Globals

#ifdef ENABLE_WEBSERVER
// Create webserver object listening for HTTP requests on port 80
ESP8266WebServer server(80);
#endif
#ifdef USE_FIREBASE_CALLBACK
FirebaseData firebaseData;
#endif

HTTPClient http;

// SX1509 I/O expander array
byte SX1509_I2C_ADDRESSES[] = { 0x3E, 0x70 };
#define SX1509_COUNT sizeof(SX1509_I2C_ADDRESSES) / sizeof(byte)
// device and pins used for external interrupt to toggle nReset lines
#define SX1509_INT_DEVICE 1
#define SX1509_INT_PIN 0
#define SX1509_INT_TRIGGER_PIN 1

SX1509 io[SX1509_COUNT];
// SX1509 device and pin mappings for the 6 RGB LEDs
// Each pair represents {device, pin} for the LED pins in RGB order
const static uint8_t led_pins[PORT_COUNT][COLOR_COUNT][2] = {
  { {0, 4}, {1, 4}, {0, 14} },
  { {0, 5}, {1, 5}, {0, 15} },
  { {0, 6}, {1, 6}, {1, 14} },
  { {0, 7}, {1, 7}, {1, 15} },
  { {0, 12}, {1, 12}, {0, 0} },
  { {0, 13}, {1, 13}, {0, 1} },
};

// RGB color sequence used for display tests
#ifdef ENABLE_LED_EFFECTS
const static boolean rgbSeq[][COLOR_COUNT] = {
  { 1, 0, 0 }, // red
  { 0, 1, 0 }, // green
  { 0, 0, 1 }, // blue
  { 1, 1, 1 }, // white
  { 1, 1, 0 }, // yellow
  { 1, 0, 1 }, // purple
  { 0, 1, 1 }, // cyan
};
#endif

char buff[6]; // for float to string formatting
// Serial command states
#ifdef ENABLE_SERIAL_COMMANDS
boolean enablePrintScaleData = false;
boolean enableLogData = false;
boolean enableLogUpdates = false;
uint8_t slotSelected = 0;
#endif
byte ledOnIntensity = 64;
byte ledOffIntensity = 0;

char* device_id = "";
char* wifi_ssid = "";
char* wifi_password = "";
char* firebase_project_id = "";
char* firebase_db_secret = "";

// Flag and timestamp to invoke reboot in loop()
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
  uint8_t led_on_intensity = 255;
} EepromData;

EepromData eepromData;

// Load cell offsets and calibration factors.
// Tare offset corresponds to the HX711 value with no load present.
long int hx711_tare_offsets[PORT_COUNT] = {0};
// Calibration factor is the linear conversion factor to scale value to kilograms.
int hx711_calibration_factors[PORT_COUNT] = {0};

// Port (aka slot) data
typedef struct {
  float last_weight_kilograms; // last value uploaded to cloud
  float current_weight_kilograms; // most recent sample
  Statistic stats;
  unsigned long lastWeightMeasurementPushMillis; // time last uploaded to cloud
  byte status;
} Port;

Port ports[PORT_COUNT];
byte currentLedStates[PORT_COUNT];

// Function prototypes
void ledWaveRight(byte ledState, int delayMs=100);
#ifdef ENABLE_LED_EFFECTS
void ledBreatheRow(uint8_t colorIndex=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeUpRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledFadeDownRow(boolean r=0, boolean g=0, boolean b=0, byte onIntensity=255, int delayMs=0, int delayStepMs=1);
void ledRandom(int maxCount=1, int delayMs=0, int delayStepMs=1);
void ledCylon(int count=10, int delayMs=150);
#endif
void setLedState(uint8_t pos, uint8_t state=LED_OFF, boolean sync=true, boolean force=false);
void setAllLedStates(uint8_t state=LED_OFF, boolean sync=true, boolean force=false);
void restoreLedStates(boolean force=false);
void getAverageScaleData(long *result, byte times=10);
#ifdef ENABLE_WEBSERVER
void handleRoot();
void handleSettings();
#ifdef ENABLE_CALIBRATE
void handleCalibrate();
#endif
void handleUtil();
void handleLoading();
#endif

// Global functions

String getBaseUrl() {
  String baseUrl = REST_API_BASEURL_PATTERN;
  baseUrl.replace("%%FIREBASE_PROJECT_ID%%", firebase_project_id);
  return baseUrl;
}
// Retrieve initial port data for device via GET
int getPortData(String device_id) {
  DEBUG_PRINT("Fetching initial port data...");
  http.begin(getBaseUrl() + String("/getDevice?device_id=") + device_id);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    DEBUG_PRINTLN(" success.");
    String response = http.getString();
    String data_list = jsonExtract(response, "data");
    // Parse response, set status and LED state for each slot
    // [eschwartz-TODO] Only do this if len of array == PORT_COUNT
    for (uint8_t i = 0; i < PORT_COUNT; i++) {
      String slotStr = jsonIndexList(data_list, i);
      DEBUG_PRINT("slot " + String(i) + ": ");
      DEBUG_PRINTLN(slotStr);
      String statusStr = jsonExtract(slotStr, "status");
      byte status = statusStringToInt(statusStr);
      ports[i].status = status;
      setLedState(i, status);
    }
  } else {
    DEBUG_PRINTLN(" FAILED.");
    DEBUG_PRINTLN("HTTP response error " + String(httpCode));
  }
  http.end();
}

int setWeight(String device_id, int slot, float weight_kg) {
  // Open https connection to setWeight cloud function
  // [eschwartz-TODO] Re-test with https, import SHA1_FINGERPRINT from config (2nd param):
  http.begin(getBaseUrl() + String("/setWeight"));
  http.addHeader("Content-Type", "application/json");
  String data = String("{\"device_id\":\"") + device_id
    + String("\",\"slot\":\"") + String(slot)
    + String("\",\"weight_kg\":") + dtostrf(weight_kg, 2, 4, buff)
    + String("}");
  // [eschwartz-TODO]: This should probably be a PUT to /ports/<device_id>/data/<slot>
  int httpCode = http.POST(data);
  #ifdef ENABLE_SERIAL_COMMANDS
  if (enableLogUpdates) {
    if (httpCode > 0) {
      DEBUG_PRINT("Slot ");
      DEBUG_PRINT(slot);
      DEBUG_PRINT(" setWeight SUCCESS, httpCode = ");
      DEBUG_PRINTLN(httpCode);
    } else {
      DEBUG_PRINT("Slot ");
      DEBUG_PRINT(slot);
      DEBUG_PRINT(" setWeight FAILED: httpCode = ");
      DEBUG_PRINT(httpCode);
      DEBUG_PRINT(", error = ");
      DEBUG_PRINTLN(http.errorToString(httpCode));
    }
  }
  #endif
  http.end();
}
#ifdef ENABLE_SERIAL_COMMANDS
void printCurrentValues() {
  DEBUG_PRINT("Weights (kg): ");
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    DEBUG_PRINTDP(ports[i].current_weight_kilograms, 4);
    DEBUG_PRINT((i != PORT_COUNT - 1) ? "\t" : "\n");
  }
}

void printLogMessage(uint8_t slot, float kg_change) {
  DEBUG_PRINT("Slot ");
  DEBUG_PRINT(slot);
  DEBUG_PRINT(" CHANGED: ");
  DEBUG_PRINT("count: ");
  DEBUG_PRINT(ports[slot].stats.count());
  DEBUG_PRINT(", avg: ");
  DEBUG_PRINTDP(ports[slot].stats.average(), 4);
  DEBUG_PRINT(", std dev: ");
  DEBUG_PRINTDP(ports[slot].stats.pop_stdev(), 4);
  DEBUG_PRINT(", last: ");
  DEBUG_PRINTDP(ports[slot].last_weight_kilograms, 4);
  DEBUG_PRINT(", curr: ");
  DEBUG_PRINTDP(ports[slot].stats.average(), 4);
  DEBUG_PRINT(", diff: ");
  DEBUG_PRINTDP(kg_change, 4);
  DEBUG_PRINT(" (> ");
  DEBUG_PRINTDP(KG_CHANGE_THRESHOLD, 4);
  DEBUG_PRINT(" kg)");
  DEBUG_PRINTLN();
}
#endif

// Restore LED states after disrupting them with tests
void restoreLedStates(boolean force) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, STATUS_TO_LED_STATE[ports[i].status], true, force);
  }
}

void resetDisplay() {
  setAllLedStates(LED_OFF, false, true);
}

// Start LED wave pattern by setting each to breathe (or other ledState) with delayMs spacing
void ledWaveRight(byte ledState, int delayMs) {
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
    setLedState(pos, ledState, false);
    delay(delayMs);
  }
}
#ifdef ENABLE_LED_EFFECTS
// Run LED test sequences
void displayTest() {
  int delayMs = 0;
  int delayStepMs = 1;

  DEBUG_PRINTLN("Running display tests (ledOnIntensity=" + String(ledOnIntensity) + ")...");
  resetDisplay();
  ledCylon(3);
  delay(500);
  ledBreatheRow(R, ledOnIntensity, delayMs, delayStepMs);
  delay(100);
  ledBreatheRow(G, ledOnIntensity, delayMs, delayStepMs);
  delay(100);
  ledBreatheRow(B, ledOnIntensity, delayMs, delayStepMs);
  delay(100);
  // Fade in/out rows with simple color combinations
  delayStepMs = 1;
  for (uint8_t i = 0; i < LEN(rgbSeq); i++) {
    ledFadeUpRow(rgbSeq[i][R], rgbSeq[i][G], rgbSeq[i][B], ledOnIntensity, delayMs, delayStepMs);
    ledFadeDownRow(rgbSeq[i][R], rgbSeq[i][G], rgbSeq[i][B], ledOnIntensity, delayMs, delayStepMs);
    delay(500);
  }
  delay(100);
  ledRandom(20);
  resetDisplay();
  delay(500);
  restoreLedStates();
  DEBUG_PRINTLN("...done.");
}

void ledBreatheRow(uint8_t colorIndex, byte onIntensity, int delayMs, int delayStepMs)
{
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
    int device = led_pins[pos][colorIndex][DEV];
    int pin = led_pins[pos][colorIndex][PIN];
    // ramp up
    for (int i = 0; i < (onIntensity + 1); i++) {
      io[device].analogWrite(pin, 255 - i); // syncing current, value is inverted
      delay(delayStepMs);
    }
    delay(delayMs);
    // ramp down
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
      if (r) io[RDEV].analogWrite(RPIN, 255 - i);
      if (g) io[GDEV].analogWrite(GPIN, 255 - i);
      if (b) io[BDEV].analogWrite(BPIN, 255 - i);
    }
    delay(delayStepMs);
  }
  delay(delayMs);
}

void ledFadeDownRow(boolean r, boolean g, boolean b, byte onIntensity, int delayMs, int delayStepMs)
{
  for (int i = onIntensity; i >= 0; i--) {
    for (uint8_t pos = 0; pos < PORT_COUNT; pos++) {
      if (r) io[RDEV].analogWrite(RPIN, 255 - i);
      if (g) io[GDEV].analogWrite(GPIN, 255 - i);
      if (b) io[BDEV].analogWrite(BPIN, 255 - i);
    }
    delay(delayStepMs);
  }
  delay(delayMs);
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
      if (r) io[RDEV].analogWrite(RPIN, 255 - i);
      if (g) io[GDEV].analogWrite(GPIN, 255 - i);
      if (b) io[BDEV].analogWrite(BPIN, 255 - i);
      delay(delayStepMs);
    }
    delay(delayMs);
    for (int i = intensity; i >= 0; i -= step) {
      if (r) io[RDEV].analogWrite(RPIN, 255 - i);
      if (g) io[GDEV].analogWrite(GPIN, 255 - i);
      if (b) io[BDEV].analogWrite(BPIN, 255 - i);
      delay(delayStepMs);
    }
    // turn off in case stepping missed the 0 value
    if (r) io[RDEV].analogWrite(RPIN, 255);
      if (g) io[GDEV].analogWrite(GPIN, 255);
      if (b) io[BDEV].analogWrite(BPIN, 255);
    delay(delayMs);
  }
}

void ledCylon(int count, int delayMs) {
  int counter = 0;
  uint8_t device, pin, endPos;
  while (counter++ < count) {
    endPos = (counter == count) ? 0 : 1;
    // to the right
    for (int pos = 0; pos < PORT_COUNT; pos++) {
      io[RDEV].analogWrite(RPIN, 0);
      delay(delayMs);
      io[RDEV].analogWrite(RPIN, 255);
    }
    // to the left
    for (int pos = (PORT_COUNT - 2); pos >= endPos; pos--) {
      io[RDEV].analogWrite(RPIN, 0);
      delay(delayMs);
      io[RDEV].analogWrite(RPIN, 255);
    }
  }
}
#endif

void setAllLedStates(uint8_t state, boolean sync, boolean force) {
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    setLedState(i, state, false, force);
  }
  if (sync) syncLeds();
}

// Stop any blinking or breathing on pins for colors in given LED position
// See https://forum.sparkfun.com/viewtopic.php?t=48433)
void checkStopBlinkBreathe(uint8_t pos) {
  byte state = currentLedStates[pos];
  if ((state == LED_RED_BLINK) || (state == LED_RED_BLINK_FAST) || (state == LED_UNLOADED)
      || (LED_CLEARING) || (state == LED_RED_BREATHE)) {
    io[RDEV].setupBlink(RPIN, 0, 0, 255); // stop red blink
  }
  if ((state == LED_GREEN_BLINK) || (state == LED_GREEN_BREATHE) || (state == LED_INITIALIZING)) {
    io[GDEV].setupBlink(GPIN, 0, 0, 255); // stop green blink
  }
  if ((state == LED_BLUE_BLINK) || (state == LED_BLUE_BREATHE)) {
    io[BDEV].setupBlink(BPIN, 0, 0, 255); // stop blue blink
  }
}

// For called RGB colorIndex in LED position, turn off other 2 colors
void isolateColor(uint8_t pos, uint8_t colorIndex) {
  if (colorIndex == B) {
    io[RDEV].analogWrite(RPIN, 255);
  }
  if (colorIndex == R || colorIndex == B) {
    io[GDEV].analogWrite(GPIN, 255);
  }
  if (colorIndex == R || colorIndex == G) {
    io[BDEV].analogWrite(BPIN, 255);
  }
}

void setLedState(uint8_t pos, uint8_t state, boolean sync, boolean force) {
  // Return if target state is already set, and force is not with us
  if ((state == currentLedStates[pos]) && !force) return;

  checkStopBlinkBreathe(pos); // if any color(s) blinking, stop

  switch (state) {
    case LED_RED:
    case LED_LOADED:
      // solid red a la Whole Foods parking lot
      isolateColor(pos, R);
      io[RDEV].analogWrite(RPIN, 255 - ledOnIntensity); 
      break;
    case LED_RED_BLINK:
    case LED_UNLOADED:
      isolateColor(pos, R);
      io[RDEV].blink(RPIN, BLINK_ON_MS, BLINK_OFF_MS);
      break;
    case LED_RED_BLINK_FAST:
    case LED_CLEARING:
      isolateColor(pos, R);
      io[RDEV].blink(RPIN, BLINK_FAST_ON_MS, BLINK_FAST_OFF_MS);
      break;
    case LED_RED_BREATHE:
      isolateColor(pos, R);
      io[RDEV].breathe(RPIN, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
      break;
    case LED_GREEN:
    case LED_VACANT:
      // solid green a la Whole Foods parking lot
      isolateColor(pos, G);
      io[GDEV].analogWrite(GPIN, 255 - ledOnIntensity);
      break;
    case LED_GREEN_BLINK:
      isolateColor(pos, G);
      io[GDEV].blink(GPIN, BLINK_ON_MS, BLINK_OFF_MS);
      break;
    case LED_GREEN_BREATHE:
    case LED_INITIALIZING:
      isolateColor(pos, G);
      io[GDEV].breathe(GPIN, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
      break;
    case LED_BLUE:
      // fyi, solid blue also appears in Whole Foods parking lot (reserved spaces)
      isolateColor(pos, B);
      io[BDEV].analogWrite(BPIN, 255 - ledOnIntensity);
      break;
    case LED_BLUE_BLINK:
      isolateColor(pos, B);
      io[BDEV].blink(BPIN, BLINK_ON_MS, BLINK_OFF_MS);
      break;    
    case LED_BLUE_BREATHE:
      isolateColor(pos, B);
      io[BDEV].breathe(BPIN, BREATHE_ON_MS, BREATHE_OFF_MS, BREATHE_RISE_MS, BREATHE_FALL_MS);
      break;
    case LED_WHITE:
    case LED_CALIBRATING:
      io[RDEV].analogWrite(RPIN, 255 - ledOnIntensity);
      io[GDEV].analogWrite(GPIN, 255 - ledOnIntensity);
      io[BDEV].analogWrite(BPIN, 255 - ledOnIntensity);
      break;
    case LED_OFF:
    default:
      io[RDEV].analogWrite(RPIN, 255);
      io[GDEV].analogWrite(GPIN, 255);
      io[BDEV].analogWrite(BPIN, 255);
      break;
  }
  currentLedStates[pos] = state;
  // Sync LED timing if new state is blinking or breathing
  if (sync && (state != LED_RED) && (state != LED_BLUE) && (state != LED_GREEN) &&
      (state != LED_WHITE) && (state != LED_LOADED) && (state != LED_OFF) &&
      (state != LED_VACANT)) {
    syncLeds();
  }
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
  DEBUG_PRINTLN("___________ Stream callback data received __________");
  DEBUG_PRINTLN("streamPath: " + data.streamPath());
  DEBUG_PRINTLN("dataPath:   " + data.dataPath());
  DEBUG_PRINTLN("dataType:   " + data.dataType());
  DEBUG_PRINTLN("eventType:  " + data.eventType());
  DEBUG_PRINT(  "value:      ");
  // if (data.dataType() == "int") {
  //   DEBUG_PRINTLN(data.intData());
  // }
  // else (data.dataType() == "float") {
  //   DEBUG_PRINTLN(String(data.floatData(), 5));
  // }
  // else if (data.dataType() == "double") {
  //   printf("%.9lf\n", data.doubleData());
  // }
  // else if (data.dataType() == "boolean") {
  //   DEBUG_PRINTLN(data.boolData() == 1 ? "true" : "false");
  // }
  // else if (data.dataType() == "array") {
  //   DEBUG_PRINTLN("got dataType() 'array'");
  // }
  // else if (data.dataType() == "json") {
  //   DEBUG_PRINTLN(data.jsonString());
  // }
  if ((data.dataType() == "string") || (data.dataType() == "null")) {
    DEBUG_PRINTLN(data.stringData());
    if (data.dataPath().startsWith("/data/")) {
      // [eschwartz-TODO] This assumes single digit slot number. Change to allow multi digit.
      uint8_t slot = data.dataPath().substring(6, 7).toInt(); // extract from '/data/n'
      if ((slot >= 0) && (slot < PORT_COUNT)) {
        // If matched "/data/n/status", update slot status otherwise set to unknown
        byte status = data.dataPath().endsWith("/status") ? statusStringToInt(data.stringData()) : STATUS_UNKNOWN;
        ports[slot].status = status;
        // Update LED state
        // [eschwartz-TODO] LED is turning off upon update from deleted port to VACANT
        setLedState(slot, STATUS_TO_LED_STATE[status]);
      }
    }
  }
  
}

void streamTimeoutCallback(boolean timeout) {
  if (timeout) {
    // Stream timeout occurred
    DEBUG_PRINTLN("Stream timeout, resuming...");
  }  
}

#ifdef ENABLE_WEBSERVER
// Check for and validate param in server POST args
String getParamVal(String param, uint8_t minLength, uint8_t maxLength) {
  String result = "";
  if (server.hasArg(param)) {
    String val = server.arg(param);
    if (val.length() >= minLength && val.length() <= maxLength) {
      result = val;
    }
  }
  return result;
}

String headerHtml() {
  String html = headerHtmlTemplate;
  html.replace("{STYLE}", styleHtmlTemplate);
  html.replace("{SCRIPT}", scriptHtmlTemplate);
  return html;
}

String settingsHtml() {
  String html = settingsHtmlTemplate;
  html.replace("{DEVICE_ID}", device_id);
  html.replace("{WIFI_SSID}", wifi_ssid);
  html.replace("{WIFI_PASSWORD}", wifi_password);
  html.replace("{FIREBASE_PROJECT_ID}", firebase_project_id);
  html.replace("{FIREBASE_DB_SECRET}", firebase_db_secret);
  html.replace("{LED_ON_INTENSITY}", String(ledOnIntensity));
  return html;
}
#ifdef ENABLE_CALIBRATE
String calibrateHtml() {
  String html = calibrateHtmlTemplate;
  String tableRowsHtml = "";

  // Convert status code int to string
  const char* STATUS_TO_STRING[] = {
    "UNKNOWN",      // 0 STATUS_UNKNOWN
    "INITIALIZING", // 1 STATUS_INITIALIZING
    "VACANT",       // 2 STATUS_VACANT
    "LOADED",       // 3 STATUS_LOADED
    "UNLOADED",     // 4 STATUS_UNLOADED
    "CLEARING",     // 5 STATUS_CLEARING
    "CALIBRATING"   // 6 STATUS_CALIBRATING
  };

  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    String rowHtml = calibrateRowHtmlTemplate;
    rowHtml.replace("{SLOT}", String(i));
    rowHtml.replace("{PORT}", String(i + 1));
    rowHtml.replace("{LAST_WEIGHT_KILOGRAMS}", String(dtostrf(ports[i].last_weight_kilograms, 2, 4, buff)));
    rowHtml.replace("{TARE_OFFSET}", String(hx711_tare_offsets[i]));
    rowHtml.replace("{CALIBRATION_FACTOR}", String(hx711_calibration_factors[i]));
    rowHtml.replace("{STATUS}", STATUS_TO_STRING[ports[i].status]);
    tableRowsHtml += rowHtml;
  }
  html.replace("{TABLE_ROWS}", tableRowsHtml);
    
  return html;
}
#endif
String loadingHtml() {
String html = loadingHtmlTemplate;
  html.replace("{STYLE}", styleHtmlTemplate);
  return html;
}

void handleRoot() {
  // consider /settings the root
  server.sendHeader("Location", "/settings");
  server.send(302);
}

void handleSettings() {
  String val;

  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + settingsHtml() + footerHtmlTemplate);
  }
  else if (server.method() == HTTP_POST) {
    // [eschwartz-TODO] Validate all inputs
    val = getParamVal("device_id", 1, 31);
    if (val) {
      strcpy(device_id, val.c_str());
    }

    val = getParamVal("firebase_project_id", 1, 63);
    if (val) {
      strcpy(firebase_project_id, val.c_str());
    }

    val = getParamVal("firebase_db_secret", 1, 63);
    if (val) {
      strcpy(firebase_db_secret, val.c_str());
    }

    val = getParamVal("wifi_ssid", 1, 31);
    if (val) {
      strcpy(wifi_ssid, val.c_str());
    }

    val = getParamVal("wifi_password", 1, 31);
    if (val) {
      strcpy(wifi_password, val.c_str());
    }

    val = getParamVal("led_on_intensity", 1, 3);
    if (val) {
      ledOnIntensity = val.toInt();
      restoreLedStates(true); // force state refresh to reinit intensity
    }

    // Redirect to loading page for reboot
    server.sendHeader("Location", "/loading");
    server.send(303);
    
    // Write to EEPROM and reboot if any setting was successfully changed
    if (val) {
      writeEeprom();
      // Schedule reboot via loop() so this handler can exit
      rebootRequired = true;
      rebootTimeMillis = millis() + REBOOT_DELAY_MILLIS;
    }
  } else {
    server.send(405, "text/html", "Method Not Allowed");
  }
}
#ifdef ENABLE_CALIBRATE
void handleCalibrate() {
  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + calibrateHtml() + footerHtmlTemplate);
  }
  else if (server.method() == HTTP_POST) {
    if (server.hasArg("id")) {
      // Calibrate button clicked
      int factor = calibrateScaleFactor(server.arg("id").toInt());
      // Send response for table update
      server.send(200, "text/html", String(factor));
    }
    if (server.hasArg("offsets")) {
      // Tare Scales button clicked 
      calibrateOffsets();
      // Redirect to self to refresh
      server.sendHeader("Location", "/calib");
      server.send(303);
    }
    if (server.hasArg("savecal")) {
      writeEeprom();
      server.sendHeader("Location", "/loading");
      server.send(303);
      // Schedule reboot via loop() so this handler can exit
      rebootRequired = true;
      rebootTimeMillis = millis() + REBOOT_DELAY_MILLIS;
    }
  }
  else {
    server.send(405, "text/html", "Method Not Allowed");
  }
}
#endif

void handleUtil() {
  if (server.method() == HTTP_GET) {
    server.send(200, "text/html", headerHtml() + utilHtmlTemplate + footerHtmlTemplate);
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
    #ifdef ENABLE_LED_EFFECTS
    if (server.hasArg("displaytest")) {
      // Run Display Tests button clicked, redirect to self
      server.sendHeader("Location", "/util");
      server.send(303);
      displayTest();
      restoreLedStates(true); // force state refresh to reinit timing
    }
    #endif
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

#ifdef ENABLE_SERIAL_COMMANDS
void printSerialCommandMenu() {
  PRINTLN("COMMANDS:");
  PRINTLN("[H] Help [R] Reboot [T] Test display");
  PRINTLN("[0] Reset display [1] Sync LED state [2] Blink red [3] Blink green [4] Blink blue");
  PRINTLN("[5] White on [6] Wave red [7] Wave green [8] Wave blue");
  PRINTLN("[O] Calib offsets [+] Select slot (+) [-] Select slot (-) [F] Calib slot " + String(slotSelected));
  PRINTLN("LOGGING: [U] Toggle Updates [S] Toggle Scale data [P] Toggle Port data");
  PRINTLN("CONFIG DATA:");
  printConfigData();
}
#endif
void calibrateOffsets() {
  long int results[PORT_COUNT] = {0};
  DEBUG_PRINTLN("Calibrating offsets: sampling averages...");
  setAllLedStates(LED_CALIBRATING);
  getAverageScaleData(results, 10);
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    hx711_tare_offsets[i] = results[i];
  }
  writeEeprom();
  restoreLedStates();
}

int calibrateScaleFactor(uint8_t slot) {
  long int results[PORT_COUNT] = {0};
  DEBUG_PRINTLN("Calibrating scale factor for Slot " + String(slot));
  setLedState(slot, LED_CALIBRATING);
  getAverageScaleData(results, 10);
  restoreLedStates();
  hx711_calibration_factors[slot] = 1.0 * (results[slot] - hx711_tare_offsets[slot]) / CALIBRATION_WEIGHT_KG;
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
  #ifdef DEBUG 
  DEBUG_PRINTLN("Slot\tCount\tAvg\tMin\tMax\tStdev");
  DEBUG_PRINTLN("----\t-----\t---\t---\t---\t-----");
  for (i = 0; i < scales.get_count(); i++) {
    DEBUG_PRINT(i);
    DEBUG_PRINT('\t');
    DEBUG_PRINT(calStats[i].count());
    DEBUG_PRINT('\t');
    DEBUG_PRINT((int)calStats[i].average());
    DEBUG_PRINT('\t');
    DEBUG_PRINTDP(calStats[i].minimum(), 0);
    DEBUG_PRINT('\t');
    DEBUG_PRINTDP(calStats[i].maximum(), 0);
    DEBUG_PRINT('\t');
    DEBUG_PRINTDP(calStats[i].pop_stdev(), 0);
    DEBUG_PRINT('\n');
  }
  #endif

	// set the offsets
	for (i = 0; i < PORT_COUNT; i++) {
		result[i] = (int)calStats[i].average();
	}
}
#ifdef ENABLE_SERIAL_COMMANDS
void printScaleData() {
  scales.read(scale_results);
  for (uint8_t i = 0; i < scales.get_count(); ++i) {
    DEBUG_PRINT(scale_results[i]);
    DEBUG_PRINT("\t");
    DEBUG_PRINTDP(1.0 * (scale_results[i] - hx711_tare_offsets[i]) / hx711_calibration_factors[i], 4);
    DEBUG_PRINT((i != scales.get_count() - 1) ? "\t" : "\n");
  }
}
#endif

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
#ifdef ENABLE_SERIAL_COMMANDS
void printConfigData() {
  PRINTLN("device_id:           '" + String(device_id) + "'");
  PRINTLN("wifi_ssid:           '" + String(wifi_ssid) + "'");
  PRINTLN("wifi_password:       '" + String(wifi_password) + "'");
  PRINTLN("firebase_project_id: '" + String(firebase_project_id) + "'");
  PRINTLN("firebase_db_secret:  '" + String(firebase_db_secret) + "'");
  PRINTLN("led_on_intensity:     " + String(ledOnIntensity));
  PRINTLN("Slot\tOffset\tCal factor");
  PRINTLN("----\t------\t----------");
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    PRINT(i);
    PRINT('\t');
    PRINT(hx711_tare_offsets[i]);
    PRINT('\t');
    PRINT(hx711_calibration_factors[i]);
    PRINTLN();
  }
}
#endif
void readEeprom() {
  DEBUG_PRINTLN("Reading EEPROM data...");
  EEPROM.begin(EEPROM_LEN);
  EEPROM.get(EEPROM_ADDR, eepromData);
  device_id = eepromData.device_id;
  wifi_ssid = eepromData.wifi_ssid;
  wifi_password = eepromData.wifi_password;
  firebase_project_id = eepromData.firebase_project_id;
  firebase_db_secret = eepromData.firebase_db_secret;
  ledOnIntensity = eepromData.led_on_intensity;
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    hx711_tare_offsets[i] = eepromData.offsets[i];
    hx711_calibration_factors[i] = eepromData.calibration_factors[i];
  }
  #ifdef ENABLE_SERIAL_COMMANDS
  printConfigData();
  #endif
}

void writeEeprom() {
  DEBUG_PRINTLN("Writing EEPROM data to addr 0x" + String(EEPROM_ADDR, HEX) + "...");
  strncpy(eepromData.device_id, device_id, 32);
  strncpy(eepromData.wifi_ssid, wifi_ssid, 64);
  strncpy(eepromData.wifi_password, wifi_password, 64);
  strncpy(eepromData.firebase_project_id, firebase_project_id, 64);
  strncpy(eepromData.firebase_db_secret, firebase_db_secret, 64);
  eepromData.led_on_intensity = ledOnIntensity;
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    eepromData.offsets[i] = hx711_tare_offsets[i];
    eepromData.calibration_factors[i] = hx711_calibration_factors[i];
  }
  #ifdef ENABLE_SERIAL_COMMANDS
  printConfigData();
  #endif
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
    DEBUG_PRINTLN("commit succeeded.");
  } else {
    DEBUG_PRINTLN("commit FAILED.");
  }
}

void setupAP() {
  PRINT("Setting up access point with SSID '" + String(WIFI_AP_SSID) + "'... ");
  Serial.println(WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD) ? "success." : "FAILED.");
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
  // Set the size of WiFi RX/TX buffers (min 512 - max 16384)
  //firebaseData.setBSSLBufferSize(512, 512);
  // Set the size of HTTP response buffers
  //firebaseData.setResponseSize(1024);
  // Set database read timeout to 1 minute (max 15 minutes)
  //Firebase.setReadTimeout(firebaseData, 1000 * 60);
  // Set write size limit and timeout: tiny (1s), small (10s), medium (30s), large (60s), or unlimited
  //Firebase.setwriteSizeLimit(firebaseData, "tiny");
  
  Firebase.setStreamCallback(firebaseData, streamCallback, streamTimeoutCallback);

  if (!Firebase.beginStream(firebaseData, "/ports/" + String(device_id))) {
    // unable to begin stream connection
    DEBUG_PRINTLN(firebaseData.errorReason());
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
  PRINTLN('\n');
  PRINTLN(BOOT_MESSAGE);
  Serial.flush();

  readEeprom();
  #ifdef ENABLE_LED_EFFECTS
  randomSeed(analogRead(0)); // seed with random noise for ledRandom()
  #endif

  // Initialize SX1509 I/O expanders
  for (uint8_t i = 0; i < SX1509_COUNT; i++) {
    DEBUG_PRINT("Initializing SX1509 device " + String(i) + " at address 0x");
    DEBUG_PRINT(String(SX1509_I2C_ADDRESSES[i], HEX));
    DEBUG_PRINT("...");
    if (!io[i].begin(SX1509_I2C_ADDRESSES[i])) {
      DEBUG_PRINTLN(" FAILED.");
      // [eschwartz-TODO] Set flag to skip I/O stuff or light an error LED
    } else {
      DEBUG_PRINTLN(" success.");
    }
  }
  // Set output frequency: 0x0: 0Hz (low), 0xF: 2MHz? (high),
  // 0x1-0xE: fOSCout = Fosc / 2 ^ (outputFreq - 1) Hz
  io[0].clock(INTERNAL_CLOCK_2MHZ, 2, OUTPUT, 0xF);
  io[1].clock(EXTERNAL_CLOCK, 2, INPUT);

  // Setup input pin to trigger external interrupt on shared nReset lines
  io[SX1509_INT_DEVICE].pinMode(SX1509_INT_PIN, INPUT_PULLUP);
  io[SX1509_INT_DEVICE].enableInterrupt(SX1509_INT_PIN, FALLING);
  // Setup an output pin (wired to SX1509_INT_PIN) to trigger it
  io[SX1509_INT_DEVICE].pinMode(SX1509_INT_TRIGGER_PIN, OUTPUT);

  // Set up all LED pins as ANALOG_OUTPUTs
  for (uint8_t pos = 0; pos < PORT_COUNT; pos++ ) {
    for (uint8_t color = 0; color < COLOR_COUNT; color++) {
      io[led_pins[pos][color][DEV]].pinMode(led_pins[pos][color][PIN], ANALOG_OUTPUT);      
    }
  }

  // Start LED wave to indicate initializing
  ledWaveRight(LED_RED_BREATHE);

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
  Serial.println("Connecting to WiFi (timeout in " + String(WIFI_CONNECT_TIMEOUT_MS) + " ms)...");
  if (wifiConnect()) {
    Serial.print("\nConnected to ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    PRINTLN("connection timed out, setting up access point...");
    setupAP();
  }
  
  #ifdef MDNS
  // Start the mDNS responder for esp8266.local
  if (MDNS.begin("esp8266")) {
    DEBUG_PRINTLN("mDNS responder started");
  } else {
    DEBUG_PRINTLN("Error setting up MDNS responder!");
  }
  #endif

  #ifdef ENABLE_WEBSERVER
  // Set up callbacks for client URIs
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  #ifdef ENABLE_CALIBRATE
  server.on("/calib", handleCalibrate);
  #endif
  server.on("/util", handleUtil);
  server.on("/loading", handleLoading);
  server.onNotFound(handleRoot);
  // Start the HTTP server
  server.begin();
  DEBUG_PRINTLN("HTTP server started");
  #endif
  
  // Get initial port data via REST call to Firebase
  if (WiFi.status() == WL_CONNECTED) {
    getPortData(device_id);
  }

  #ifdef USE_FIREBASE_CALLBACK
  setupFirebaseCallback();
  #endif
  #ifdef ENABLE_SERIAL_COMMANDS
  printSerialCommandMenu();
  #endif
}

void loop() {
  #ifdef ENABLE_WEBSERVER
  // Listen for HTTP requests from clients
  server.handleClient();
  #endif

  scales.read(scale_results);
  #ifdef ENABLE_SERIAL_COMMANDS
  if (enablePrintScaleData) {
    printScaleData();
  }
  if (enableLogData) {
    printCurrentValues();
  }
  #endif

  if (rebootRequired && (millis() >= rebootTimeMillis)) {
    reboot();
  }

  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    float kg_change = 0;
    // Get current weight reading, apply offset and calibratation factor
    float weight_kilograms = MAX(1.0 *
      (scale_results[i] - hx711_tare_offsets[i]) / hx711_calibration_factors[i], 0);
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
       * Print log message and send update to cloud when conditions are met:
       * - average reading exceeds PRESENCE_THRESHOLD_KG (i.e. object is present)
       * - average reading changed KG_CHANGE_THRESHOLD or more since last update
       * - standard deviation within STD_DEV_THRESHOLD kg
       * - at least WEIGHT_MEASURE_INTERVAL_MS has passed since last update
       */
      if (!ports[i].lastWeightMeasurementPushMillis ||
            ((kg_change >= KG_CHANGE_THRESHOLD)
            && (ports[i].stats.pop_stdev() <= STD_DEV_THRESHOLD)
            && ((millis() - ports[i].lastWeightMeasurementPushMillis) >= WEIGHT_MEASURE_INTERVAL_MS))) {
        // Log it
        #ifdef ENABLE_SERIAL_COMMANDS
        if (enableLogUpdates) {
          printLogMessage(i, kg_change);
        }
        #endif
        if (WiFi.status() == WL_CONNECTED) {
          // Send new weight_kg to Realtime Database via HTTP function
          if (setWeight(device_id, i, ports[i].stats.average())) {
            // success
            ports[i].last_weight_kilograms = ports[i].stats.average();
            ports[i].lastWeightMeasurementPushMillis = millis();
          }
        }
        // Reset LED based on current port status
        setLedState(i, STATUS_TO_LED_STATE[ports[i].status]);
      }
      ports[i].stats.clear();
    }
  }

  if (Serial.available()) {
    char key = Serial.read();
    if (key == 'r' || key == 'R') {
      PRINTLN("Rebooting device...");
      reboot();
    }
    #ifdef ENABLE_SERIAL_COMMANDS
    if ((key == 'h') || (key == 'H') || (key == '?')) {
      printSerialCommandMenu();
    }
    if (key == 'p' || key == 'P') {
      enableLogData = !enableLogData;
      PRINT("Data logging is ");
      PRINTLN(enableLogData ? "ON" : "OFF");
    }
    if (key == 's' || key == 'S') {
      enablePrintScaleData = !enablePrintScaleData;
      PRINT("Scale data logging is ");
      PRINTLN(enablePrintScaleData ? "ON" : "OFF");
      if (enablePrintScaleData) {
        for (uint8_t i = 0; i < PORT_COUNT; i++) {
          PRINT("s" + String(i) + " raw\ts" + String(i) + " kg\t");
        }
        PRINT("\n");
      }
    }
    if (key == 'u' || key == 'U') {
      enableLogUpdates = !enableLogUpdates;
      PRINT("Update logging is ");
      PRINTLN(enableLogUpdates ? "ON" : "OFF");
    }
    if ((key == 'o') || (key == 'O')) {
      calibrateOffsets();
    }
    if ((key == 'f') || (key == 'F')) {
      calibrateScaleFactor(slotSelected);
    }
    if (key == '0') {
      PRINTLN("Resetting display...");
      resetDisplay();
    }
    if (key == '1') {
      PRINTLN("Restoring LED state...");
      restoreLedStates();
    }
    if (key == '2') {
      PRINTLN("Setting all LEDs to LED_RED_BLINK state...");
      setAllLedStates(LED_RED_BLINK);
    }
    if (key == '3') {
      PRINTLN("Setting all LEDs to LED_GREEN_BLINK state...");
      setAllLedStates(LED_GREEN_BLINK);
    }
    if (key == '4') {
      PRINTLN("Setting all LEDs to LED_BLUE_BLINK state...");
      setAllLedStates(LED_BLUE_BLINK);
    }
    if (key == '5') {
      PRINTLN("Setting all LEDs to LED_WHITE state...");
      setAllLedStates(LED_WHITE);
    }
    #ifdef ENABLE_LED_EFFECTS
    if (key == '6') {
      PRINTLN("Setting red wave pattern...");
      ledWaveRight(LED_RED_BREATHE);
    }
    if (key == '7') {
      PRINTLN("Setting green wave pattern...");
      ledWaveRight(LED_GREEN_BREATHE);
    }
    if (key == '8') {
      PRINTLN("Setting blue wave pattern...");
      ledWaveRight(LED_BLUE_BREATHE);
    }
    if (key == 't' || key == 't') {
      displayTest();
    }
    #endif
    if (key == '+') {
      if (slotSelected < (PORT_COUNT - 1)) slotSelected++;
      PRINTLN("Slot " + String(slotSelected) + " is selected");
    }
    if (key == '-') {
      if (slotSelected > 0) slotSelected--;
      PRINTLN("Slot " + String(slotSelected) + " is selected");
    }
    if (key == 'x') {
      PRINTLN("Clearing display...");
      setAllLedStates(LED_OFF);
    }
    #endif
  }
  
}
