#include "fingerprint_module.h"
#include "config.h"

HardwareSerial mySerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
uint8_t id;
int fingerprint_id = 0;
String fidString = "NIL";

void initFingerprint() {
  mySerial.begin(FINGERPRINT_BAUD, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  finger.begin(FINGERPRINT_BAUD);
  
  Serial.println("Initializing fingerprint sensor...");
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found!");
  } else {
    Serial.println("Fingerprint sensor not found or password incorrect!");
    Serial.println("Continuing without fingerprint sensor...");
    return;  // Return instead of infinite loop
  }
  
  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);
}

uint8_t readNumber() {
  uint8_t num = 0;
  while (num == 0) {
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

uint8_t getFingerprintEnroll() {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK: Serial.println("Image taken"); break;
      case FINGERPRINT_NOFINGER: Serial.print("."); break;
      case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); break;
      case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); break;
      default: Serial.println("Unknown error"); break;
    }
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK: Serial.println("Image converted"); break;
    case FINGERPRINT_IMAGEMESS: Serial.println("Image too messy"); return p;
    case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return p;
    case FINGERPRINT_FEATUREFAIL: Serial.println("Could not find fingerprint features"); return p;
    case FINGERPRINT_INVALIDIMAGE: Serial.println("Could not find fingerprint features"); return p;
    default: Serial.println("Unknown error"); return p;
  }

  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK: Serial.println("Image taken"); break;
      case FINGERPRINT_NOFINGER: Serial.print("."); break;
      case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); break;
      case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); break;
      default: Serial.println("Unknown error"); break;
    }
  }

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK: Serial.println("Image converted"); break;
    case FINGERPRINT_IMAGEMESS: Serial.println("Image too messy"); return p;
    case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return p;
    case FINGERPRINT_FEATUREFAIL: Serial.println("Could not find fingerprint features"); return p;
    case FINGERPRINT_INVALIDIMAGE: Serial.println("Could not find fingerprint features"); return p;
    default: Serial.println("Unknown error"); return p;
  }

  Serial.print("Creating model for #"); Serial.println(id);
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error"); return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match"); return p;
  } else {
    Serial.println("Unknown error"); return p;
  }

  Serial.print("ID "); Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error"); return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location"); return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash"); return p;
  } else {
    Serial.println("Unknown error"); return p;
  }
  return true;
}

void enrollFingerprint() {
  Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  id = readNumber();
  if (id == 0) return;
  Serial.print("Enrolling ID #"); Serial.println(id);
  if (!getFingerprintEnroll());
}

uint8_t getFingerprintID() {
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK: Serial.println("Image taken"); break;
    case FINGERPRINT_NOFINGER: Serial.println("No finger detected"); return p;
    case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return p;
    case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error"); return p;
    default: Serial.println("Unknown error"); return p;
  }

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK: Serial.println("Image converted"); break;
    case FINGERPRINT_IMAGEMESS: Serial.println("Image too messy"); return p;
    case FINGERPRINT_PACKETRECIEVEERR: Serial.println("Communication error"); return p;
    case FINGERPRINT_FEATUREFAIL: Serial.println("Could not find fingerprint features"); return p;
    case FINGERPRINT_INVALIDIMAGE: Serial.println("Could not find fingerprint features"); return p;
    default: Serial.println("Unknown error"); return p;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Found a print match!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error"); return p;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Did not find a match"); return p;
  } else {
    Serial.println("Unknown error"); return p;
  }

  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  fingerprint_id = finger.fingerID;
  Serial.print(" with confidence of "); Serial.println(finger.confidence);
  fidString = String(finger.fingerID);
  return finger.fingerID;
}

int getFingerprintIDez() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -1;

  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);
  fidString = String(finger.fingerID);
  return finger.fingerID;
}

void readFingerprint() {
  getFingerprintID();
}