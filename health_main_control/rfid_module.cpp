#include <SPI.h>
#include <MFRC522.h>
#include "rfid_module.h"
#include "config.h"

// Use SPI2 (FSPI) for RFID
SPIClass spiRFID(FSPI);
MFRC522 rfid(RFID_CS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key key;
String tidString = "NIL";

void initRFID() {
  Serial.println("Initializing RFID (SPI2)...");
  
  // Initialize SPI2 with RFID pins
  spiRFID.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_CS_PIN);
  
  // Initialize MFRC522 with custom SPI bus
  rfid.PCD_Init(&spiRFID);
  
  // Check if RFID module is responding
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.print("MFRC522 Version: 0x");
  Serial.println(version, HEX);
  
  if (version == 0x00 || version == 0xFF) {
    Serial.println("WARNING: RFID module not detected! Check wiring.");
    Serial.println("Continuing without RFID...");
    return;
  }
  
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  Serial.println("RFID initialized successfully on SPI2");
}

void readRFID() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  tidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    tidString += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    tidString += String(rfid.uid.uidByte[i], HEX);
  }
  tidString.toUpperCase();
  tidString.trim();
  Serial.print("RFID UID: ");
  Serial.println(tidString);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
