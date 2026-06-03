# ESP32 CAN Bus Interface

SmowCode sample project — CAN 2.0A/B using ESP32 TWAI controller.

## Features
- 500 Kbps CAN bus (configurable)
- Standard (11-bit) and Extended (29-bit) frame support
- Bus-off error recovery
- OBD-II PID request example (Engine RPM)

## Wiring
```
ESP32 GPIO4  →  CAN Transceiver TXD  (SN65HVD230 or MCP2562)
ESP32 GPIO5  ←  CAN Transceiver RXD
3.3V         →  Transceiver VCC
GND          →  Transceiver GND
```

## Build
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```
