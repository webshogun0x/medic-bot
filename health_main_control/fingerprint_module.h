#ifndef FINGERPRINT_MODULE_H
#define FINGERPRINT_MODULE_H

#include <Adafruit_Fingerprint.h>

extern HardwareSerial mySerial;
extern Adafruit_Fingerprint finger;
extern uint8_t id;
extern int fingerprint_id;
extern String fidString;

void initFingerprint();
uint8_t readNumber();
uint8_t getFingerprintEnroll();
void enrollFingerprint();
uint8_t getFingerprintID();
int getFingerprintIDez();
void readFingerprint();

#endif