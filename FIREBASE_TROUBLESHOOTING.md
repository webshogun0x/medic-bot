# Firebase Connection Troubleshooting Guide

## Issues Fixed in Code:

### 1. SSL/TLS Connection Problems
- **Problem**: ESP32 failing to establish SSL connections
- **Solution**: Added proper time synchronization using NTP before Firebase initialization
- **Code Changes**: Added `configTime()` and time sync wait loop

### 2. Authentication Issues
- **Problem**: "INVALID_EMAIL" error with anonymous authentication
- **Solution**: Improved error handling and fallback mechanisms
- **Code Changes**: Better error messages and offline mode support

### 3. Connection Stability
- **Problem**: Connection lost errors
- **Solution**: Added WiFi monitoring and auto-reconnection
- **Code Changes**: `checkWiFiConnection()` function in main loop

## Firebase Setup Requirements:

### 1. Database Rules
Apply these rules to your Firebase Realtime Database:
```json
{
  "rules": {
    ".read": true,
    ".write": true,
    "USERS": {
      ".indexOn": ["name", "medical_id"]
    },
    "READINGS": {
      ".indexOn": ["timestamp"]
    }
  }
}
```

### 2. Network Configuration
- Ensure your WiFi network allows HTTPS traffic on port 443
- Check if your network has firewall restrictions
- Verify DNS resolution works (try ping google.com)

### 3. ESP32 Configuration
- Use ESP32 Arduino Core version 2.0.0 or later
- Install Firebase ESP Client library version 4.3.0 or later
- Ensure sufficient heap memory (>100KB free)

## Testing Steps:

### 1. Basic Connectivity Test
```cpp
// Add this to setup() for testing
Serial.println("Testing basic connectivity...");
WiFiClient client;
if (client.connect("www.google.com", 80)) {
  Serial.println("Basic internet connectivity: OK");
  client.stop();
} else {
  Serial.println("Basic internet connectivity: FAILED");
}
```

### 2. SSL Test
```cpp
// Add this to test SSL connectivity
WiFiClientSecure secureClient;
secureClient.setInsecure(); // For testing only
if (secureClient.connect("firebase.google.com", 443)) {
  Serial.println("SSL connectivity: OK");
  secureClient.stop();
} else {
  Serial.println("SSL connectivity: FAILED");
}
```

### 3. Time Sync Verification
```cpp
// Check if time is properly synchronized
time_t now = time(nullptr);
if (now > 1600000000) { // After year 2020
  Serial.println("Time sync: OK - " + String(ctime(&now)));
} else {
  Serial.println("Time sync: FAILED - " + String(now));
}
```

## Common Error Messages and Solutions:

### "ERROR.mConnectSSL: Failed to initialize the SSL layer"
- **Cause**: Time not synchronized or SSL certificate issues
- **Solution**: Ensure NTP time sync completes before Firebase init

### "Token error: code: 400, message: INVALID_EMAIL"
- **Cause**: Authentication configuration issue
- **Solution**: Use anonymous authentication or proper email/password

### "Token error: code: -4, message: connection lost"
- **Cause**: Network instability or firewall blocking
- **Solution**: Check network stability and firewall settings

## Monitoring Commands:

Add these debug prints to monitor Firebase status:
```cpp
Serial.println("Firebase ready: " + String(Firebase.ready()));
Serial.println("WiFi status: " + String(WiFi.status()));
Serial.println("Free heap: " + String(ESP.getFreeHeap()));
Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
```

## Alternative Solutions:

### 1. Local Storage Fallback
If Firebase continues to fail, implement local SPIFFS/LittleFS storage:
```cpp
#include "SPIFFS.h"
// Store data locally when Firebase is unavailable
// Sync when connection is restored
```

### 2. HTTP REST API
Use simple HTTP requests instead of Firebase library:
```cpp
#include <HTTPClient.h>
// Make direct REST API calls to Firebase
// More control over SSL and error handling
```

### 3. MQTT Alternative
Consider using MQTT broker for real-time data:
```cpp
#include <PubSubClient.h>
// Use MQTT for sensor data transmission
// More reliable for IoT applications
```