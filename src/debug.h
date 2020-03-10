#ifndef DEBUG_H
#define DEBUG_H

#define PRINT(str)    Serial.print(str)
#define PRINTLN(str)  Serial.println(str)

#define TMP_DEBUG_PRINT(str)            Serial.print(str)
#define TMP_DEBUG_PRINTLN(str)          Serial.println(str)

#ifdef DEBUG
    #define DEBUG_PRINT(str)            Serial.print(str)
    #define DEBUG_PRINTDP(str, dp)      Serial.print(str, dp)
    #define DEBUG_PRINTLNDP(str, dp)    Serial.println(str, dp)
    #define DEBUG_PRINTLN(str)          Serial.println(str)
    #define DEBUG_PRINTDEC(str)         Serial.print(str, DEC)
#else
 #define DEBUG_PRINT(str)
 #define DEBUG_PRINTDEC(str)
 #define DEBUG_PRINTLN(str)
 #define DEBUG_PRINTDP(str, dp)
 #define DEBUG_PRINTLNDP(str, dp)
#endif

#endif