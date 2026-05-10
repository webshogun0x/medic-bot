#include "pins_arduino.h"
#include <iterator>
#include "esp32-hal-spi.h"
#include <rfid_module.h>

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
byte nuidPICC[4];
String tidString = "NIL";

void initRFID() {
  Serial.println("Initializing RFID...");
  SPI.begin(CLK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  
  // Check if RFID module is responding
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
  
  Serial.println("RFID initialized successfully");
}

void readRFID() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  
  Serial.println("Card detected!");
  
  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("Failed to read card serial");
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
  delay(500);
}
