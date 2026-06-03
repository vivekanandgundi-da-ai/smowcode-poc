# ESP32 BLE Sensor Hub

SmowCode sample project — BLE GATT server for IoT sensor data.

## Features
- BLE GATT server with Environmental Sensing Service (UUID 0x181A)
- Temperature, Humidity, Accelerometer characteristics with Notify
- Auto-restarts advertising on client disconnect

## Build
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Testing
Use nRF Connect (iOS/Android) to scan for "SmowCode-SensorHub", connect, and enable notifications.
