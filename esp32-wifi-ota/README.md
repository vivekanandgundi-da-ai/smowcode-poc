# ESP32 WiFi OTA Updater

SmowCode sample project — HTTPS OTA firmware update with rollback.

## Features
- HTTPS OTA with server certificate validation
- Dual OTA partition (A/B scheme)
- Automatic rollback if new firmware fails to boot
- Marks firmware valid on first successful boot

## Build
```bash
idf.py set-target esp32
idf.py menuconfig   # set WiFi SSID/password and OTA URL
idf.py build
idf.py flash monitor
```
