/**
 * Cerealometer
 * 
 * Copyright (c) 2020 Eric Schwartz
 */

#include "LedControl.h"

// Constants

#define PORT_COUNT 6
#define DISPLAY_TEST_CHANGE_INTERVAL_MS 3000

// LED display
#define DISPLAY_UPDATE_INTERVAL_MS 100
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

// LedControl 4 params are: pins DATA IN, CLK, LOAD (/CS), and the number of cascaded devices
LedControl lc = LedControl(16, 13, 12, 1);

unsigned long lastDisplayUpdateMillis;
unsigned long lastDisplayTestChangeMillis;
unsigned long displayCounter = 0;
int displayTestIndex = 0;
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

byte matrix[MATRIX_COLORS][MATRIX_COLUMNS];

// Global functions

// Run LED test sequences
void displayTest(void) {
  uint8_t spaceDelayMs = 75;
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    ledFlashRight(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
  }
  spaceDelayMs = 25;
  for (uint8_t i = 0; i < PORT_COUNT; i++) {
    ledFadeUpRow(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
    ledFadeDownRow(rgbSeq[i][0], rgbSeq[i][1], rgbSeq[i][2], spaceDelayMs);
  }
  lc.setIntensity(0, MAX_INTENSITY);
  ledRandom(25, 200);
  ledFadeAll(25);
  delay(500); // pause before restoring normal state
  lc.setIntensity(0, DEFAULT_INTENSITY);
  zeroMatrix();
  //restoreLedStates();
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
}

void loop(void) {
	uint8_t testState;
  
  if ((millis() - lastDisplayUpdateMillis) >= DISPLAY_UPDATE_INTERVAL_MS) {
    displayCounter++; 
    updateDisplay();
    lastDisplayUpdateMillis = millis();
  }

	// Cycle thru LED states, change every DISPLAY_TEST_CHANGE_INTERVAL_MS
	if ((millis() - lastDisplayTestChangeMillis) >= DISPLAY_TEST_CHANGE_INTERVAL_MS) {
		if (displayTestIndex++ > 12) displayTestIndex = 0;
		lastDisplayTestChangeMillis = millis();

		switch (displayTestIndex) {
			case 0:
				testState = LED_RED;
				Serial.println("LED_RED");
				break;
			case 1:
				testState = LED_RED_BLINK;
				Serial.println("LED_RED_BLINK");
				break;
			case 2:
				testState = LED_RED_BLINK_FAST;
				Serial.println("LED_RED_BLINK_FAST");
				break;
			case 3:
				testState = LED_GREEN;
				Serial.println("LED_GREEN");
				break;
			case 4:
				testState = LED_GREEN_BLINK;
				Serial.println("LED_GREEN_BLINK");
				break;
			case 5:
				testState = LED_GREEN_BLINK_FAST;
				Serial.println("LED_GREEN_BLINK_FAST");
				break;
			case 6:
				testState = LED_BLUE;
				Serial.println("LED_BLUE");
				break;
			case 7:
				testState = LED_BLUE_BLINK;
				Serial.println("LED_BLUE_BLINK");
				break;
			case 8:
				testState = LED_BLUE_BLINK_FAST;
				Serial.println("LED_BLUE_BLINK_FAST");
				break;
			case 9:
				testState = LED_WHITE;
				Serial.println("LED_WHITE");
				break;
			case 10:
				testState = LED_YELLOW;
				Serial.println("LED_YELLOW");
				break;
			case 11:
				testState = LED_PURPLE;
				Serial.println("LED_PURPLE");
				break;
			case 12:
				testState = LED_CYAN;
				Serial.println("LED_CYAN");
				break;
			default:
				testState = LED_RED;
				Serial.println("LED_RED");
				break;
		}
		for (uint8_t i = 0; i < PORT_COUNT; i++ ) {
			setLedState(i, testState);
		}
	}

	// int ledStates[] = [
	// 	LED_RED, //LED_LOADED
	// 	LED_RED_BLINK, //LED_UNLOADED
	// 	LED_RED_BLINK_FAST, //LED_CLEARING
	// 	LED_GREEN, //LED_VACANT
	// 	LED_GREEN_BLINK,
	// 	LED_GREEN_BLINK_FAST,
	// 	LED_BLUE,
	// 	LED_BLUE_BLINK,
	// 	LED_BLUE_BLINK_FAST,
	// 	LED_WHITE, //LED_INITIALIZING
	// 	LED_YELLOW,
	// 	LED_PURPLE,
	// 	LED_CYAN,
	// ]

	

  if (Serial.available()) {
    char key = Serial.read();
    if (key == 'd' || key == 'D') {
      Serial.println("Running display tests...");
      displayTest();
      Serial.println("...done.");
    }
  }
}
