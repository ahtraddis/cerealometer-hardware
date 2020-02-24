#include "HX711-multi.h"

// Pins to the load cell amps
#define HX711_CLK 5 // GPIO5 (also tied to on-board LED)
#define HX711_DT0 0 // GPIO0
#define HX711_DT1 4 // GPIO4
#define HX711_DT2 12 // GPIO12, MISO (Hardware SPI MISO)
#define HX711_DT3 13 // GPIO13, MOSI (Hardware SPI MOSI)
#define HX711_DT4 15 // GPIO15
#define HX711_DT5 16 // GPIO16, XPD (can be used to wake from deep sleep)

#define BOOT_MESSAGE "MIT_ML_SCALE V0.8"

#define TARE_TIMEOUT_SECONDS 4

byte DOUTS[6] = {HX711_DT0, HX711_DT1, HX711_DT2, HX711_DT3, HX711_DT4, HX711_DT5};

#define CHANNEL_COUNT sizeof(DOUTS) / sizeof(byte)

long int results[CHANNEL_COUNT];

HX711MULTI scales(CHANNEL_COUNT, DOUTS, HX711_CLK);

void setup() {
  Serial.begin(115200);
  Serial.println(BOOT_MESSAGE);
  Serial.flush();
  //pinMode(11,OUTPUT);
  tare();
}

void tare() {
  bool tareSuccessful = false;

  unsigned long tareStartTime = millis();
  while (!tareSuccessful && millis() < (tareStartTime + TARE_TIMEOUT_SECONDS * 1000)) {
    tareSuccessful = scales.tare(20, 10000);  // reject 'tare' if still ringing
  }
}

void sendRawData() {
  scales.read(results);
  for (int i = 0; i < scales.get_count(); ++i) {
    Serial.print( -results[i]);  
    Serial.print( (i != scales.get_count() -1) ? "\t" : "\n");
  }  
  delay(10);
}

void loop() {
  
  sendRawData(); // this is for sending raw data, for where everything else is done in processing

  //on serial data (any data) re-tare
  if (Serial.available() > 0) {
    while (Serial.available()) {
      Serial.read();
    }
    tare();
  }
 
}