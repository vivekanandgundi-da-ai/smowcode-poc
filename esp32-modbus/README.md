# ESP32 Modbus RTU Master

SmowCode sample project — Modbus RTU master over RS-485.

## Features
- Modbus RTU over RS-485 (UART2)
- FC03: Read Holding Registers
- FC06: Write Single Register
- CRC16 validation
- RS-485 direction control (DE/RE pin)

## Wiring
```
GPIO16 (TX)  →  MAX485 DI
GPIO17 (RX)  ←  MAX485 RO
GPIO18       →  MAX485 DE + RE (direction control)
```

## Build
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```
