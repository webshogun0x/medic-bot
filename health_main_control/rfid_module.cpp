#include <SPI.h>
#include <MFRC522.h>
#include "rfid_module.h"
#include "config.h"

MFRC522 rfid(RFID_CS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key key;
String tidString = "NIL";

void initRFID() {
  Serial.println("Initializing RFID (Hardware SPI2)...");
  
  // 1. Explicitly set up control pins to known states
  pinMode(RFID_RST_PIN, OUTPUT);
  pinMode(RFID_CS_PIN, OUTPUT);
  digitalWrite(RFID_CS_PIN, HIGH); // De-select
  
  // 2. Perform manual hardware reset pulse
  digitalWrite(RFID_RST_PIN, LOW);
  delay(100);
  digitalWrite(RFID_RST_PIN, HIGH);
  delay(100);
  
  // 3. Initialize default SPI bus (SPI2/FSPI) with RFID pins
  // We pass -1 for CS so the MFRC522 library can manage it manually via GPIO
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, -1);
  
  // 4. Initialize MFRC522
  rfid.PCD_Init();
  
  // 5. Check if RFID module is responding
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.print("MFRC522 Version: 0x");
  Serial.println(version, HEX);
  
  if (version == 0x00 || version == 0xFF) {
    Serial.println("WARNING: RFID module not detected! Check wiring.");
    Serial.println("Expected version: 0x91 or 0x92");
    Serial.println("Continuing without RFID...");
    return;
  }
  
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  Serial.println("RFID initialized successfully on Hardware SPI2");
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
