# Humidity Monitor

A humidity and temperature monitoring system using an ESP32 and AHT20 sensor.

## Firmware

Built with ESP-IDF v6.0.1

### Hardware Setup

```
- ESP32 GPIO 21 - AHT20 SDA
- ESP32 GPIO 22 - AHT20 SCL
- ESP32 3.3V    - AHT20 PWR (VIN/3V3)
- ESP32 GND     - AHT20 GND
```

### Build and Flash

```
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p PORT -b 115200 flash
idf.py -p PORT monitor
```
