# Filament Dryer: Sensor and Modes Module

This repository contains a standalone part of a filament dryer control system for 3D printers.  
Specifically, it focuses on sensor integration, RTC support, and temperature mode logic.  
The project is written in **C using ESP-IDF** for the ESP32 platform.

This repository demonstrates only the part I implemented individually.

---

##  Key Features

- Temperature readings from **SHT30** sensor (I2C)
- Real-time clock functionality via **DS1307** (I2C)
- Mode selection logic for common filament types (PLA, ABS, PETG, TPU)
- Temperature constraints based on the selected material

---

## Components Used

- ESP32 (DevKit board)
- SHT30 — digital temperature & humidity sensor
- DS1307 — real-time clock (RTC) module

---
