# ESP32 MQTT Client

SmowCode sample project — MQTT 3.1.1 client for ESP32.

## Features
- Auto-reconnect on WiFi/broker loss
- QoS 0/1/2 publish and subscribe
- Publishes JSON sensor telemetry every 5 seconds
- AWS IoT Core compatible (set TLS cert paths in menuconfig)

## Build
```bash
idf.py set-target esp32
idf.py menuconfig   # set broker URI, WiFi credentials
idf.py build
idf.py flash monitor
```
