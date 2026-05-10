#ifndef RFID_MODULE_H
#define RFID_MODULE_H

#include <SPI.h>
#include <MFRC522.h>
#include "config.h"

extern MFRC522 rfid;
extern MFRC522::MIFARE_Key key;
extern byte nuidPICC[4];
extern String tidString;

void initRFID();
void readRFID();

#endif