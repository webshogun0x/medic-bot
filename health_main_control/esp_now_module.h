#ifndef ESP_NOW_MODULE_H
#define ESP_NOW_MODULE_H

#include <esp_now.h>
#include <WiFi.h>

typedef struct struct_message {
  int id;
  double a;
  double b;
  double c;
} struct_message;

extern struct_message myData;
extern struct_message boardsStruct[3];

void initESPNow();
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void requestESPNowData();
void readESPNowData();

#endif