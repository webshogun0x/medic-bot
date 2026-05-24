/*
 * RFID Standalone Unit Test
 * -------------------------
 * Target: ESP32-S3 (MediBot Main Controller)
 * Module: MFRC522 RFID Reader
 * 
 * This test uses the exact pin configuration from config.h
 */

#include <SPI.h>
#include <MFRC522.h>

// EXACT PIN CONFIGURATION FROM config.h
#define RFID_MOSI_PIN     11    // SPI MOSI
#define RFID_MISO_PIN     12    // SPI MISO
#define RFID_SCK_PIN      13    // SPI Clock
#define RFID_CS_PIN       21    // Chip Select
#define RFID_RST_PIN      47    // Reset

MFRC522 rfid(RFID_CS_PIN, RFID_RST_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial monitor to open
  
  Serial.println("\n--- MediBot RFID Unit Test ---");
  
  // 1. Configure Pin Modes
  pinMode(RFID_RST_PIN, OUTPUT);
  pinMode(RFID_CS_PIN, OUTPUT);
  digitalWrite(RFID_CS_PIN, HIGH); // Ensure CS starts HIGH
  
  // 2. Perform Hardware Reset
  Serial.println("Performing Hardware Reset...");
  digitalWrite(RFID_RST_PIN, LOW);
  delay(100);
  digitalWrite(RFID_RST_PIN, HIGH);
  delay(100);

  // 3. Initialize SPI Bus (Hardware SPI2 / FSPI)
  // Parameters: SCK, MISO, MOSI, CS (-1 used to let library handle CS manually)
  Serial.println("Initializing SPI Bus...");
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, -1);
  
  // 4. Initialize MFRC522 Module
  Serial.println("Initializing MFRC522...");
  rfid.PCD_Init();
  
  // 5. Verify Communication
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.print("MFRC522 Chip Version: 0x");
  Serial.println(version, HEX);
  
  if (version == 0x91 || version == 0x92) {
    Serial.println("SUCCESS: RFID Module detected and responding correctly.");
  } else if (version == 0x00 || version == 0xFF) {
    Serial.println("CRITICAL FAILURE: RFID Module not detected.");
    Serial.println("Check: 3.3V Power, GND, and the 4 SPI wires (11, 12, 13, 21).");
  } else {
    Serial.println("WARNING: Unknown chip version. Communication may be unstable.");
  }
  
  Serial.println("Ready! Please scan an RFID card/tag...");
}

void loop() {
  // Look for new cards
  if (rfid.PICC_IsNewCardPresent()) {
    
    // Select one of the cards
    if (rfid.PICC_ReadCardSerial()) {
      Serial.print("\nCard Detected!");
      Serial.print(" UID: ");
      
      String uidStr = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        uidStr += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uidStr += String(rfid.uid.uidByte[i], HEX);
      }
      uidStr.toUpperCase();
      Serial.println(uidStr);
      
      // Halt PICC
      rfid.PICC_HaltA();
      // Stop encryption on PCD
      rfid.PCD_StopCrypto1();
      
      Serial.println("Waiting for next card...");
    }
  }
  
  delay(100);
}
