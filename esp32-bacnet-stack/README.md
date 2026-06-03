# ESP32 BACnet Stack

SmowCode sample project — BACnet/IP protocol for ESP32.

## Supported Features
- BACnet/IP (Annex J) over WiFi
- Who-Is / I-Am device discovery
- ReadProperty / WriteProperty (stub)
- COV subscriptions (stub)

## Build
```bash
idf.py set-target esp32
idf.py menuconfig   # set WiFi SSID/password under SmowCode BACnet Config
idf.py build
idf.py flash monitor
```

## Requirements
- ESP-IDF v5.1+
- ESP32 with WiFi
