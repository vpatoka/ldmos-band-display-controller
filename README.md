# ART1K9 Band Selector Display

### Automatic Band Detection & Display for HAM Radio LDMOS Amplifiers

---

**Author:** Vlad Patoka (VE3VPT)
**License:** MIT
**Platform:** Arduino UNO R4 Minima/WiFi

---

## About This Project

This project provides automatic band detection and display for a 2KW HAM Radio LDMOS amplifier based on the ART1K9 module, covering the full HF range from 1.8 MHz to 70 MHz.

### A Personal Note

> *This project has been a long journey. I started it years ago, and life took me through many difficult times along the way. Through all the challenges, this little project waited patiently on my workbench.*
>
> *I am grateful to finally complete it on the memorial day of my father, who passed away. He was the one who first introduced me to electronics and radio. Every solder joint, every line of code carries a piece of his memory.*
>
> *Dad, this one is for you. 73.*
>
> *— Vlad Patoka (VE3VPT)*

---

## Features

- **Automatic Band Detection** — Reads 8 TTL inputs from band detector board
- **4-Character LED Display** — Shows LPF frequency range (e.g., `18:21` for 17m band)
- **Operating Hours Counter** — Tracks active time when band selected (EEPROM)
- **Power-On Counter** — Tracks number of power cycles (EEPROM)
- **Temperature Monitoring** — Optional NTC thermistor with overheat alert
- **Overheat Warning** — "RELAY HOT!" scrolls on display when temp > 41°C
- **Noise Filtering** — Three-layer filter for stable band detection
- **Serial Debug Menu** — Comprehensive diagnostics via serial port

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| Arduino UNO R4 Minima | Main controller (WiFi version also supported) |
| SparkFun Qwiic Alphanumeric Display | HT16K33-based 4-character LED display |
| Band Detector Board | Provides 8 TTL signals for band identification |
| 10K NTC Thermistor | Optional temperature sensor |
| 10K Resistor | For NTC voltage divider |

## Pin Configuration

| Function | Pins |
|----------|------|
| Band Inputs | D2-D9 (8 TTL lines) |
| I2C Display | SDA, SCL (top header) |
| Temperature | A0 (optional NTC) |

## Band Display Mapping

| Band | Input | Display |
|------|-------|---------|
| 160m | D2 | `1 :4.0` |
| 80m | D3 | `1 :4.0` |
| 40m | D4 | `5 :7.2` |
| 20m | D5 | `10:14` |
| 17m | D6 | `18:21` |
| 10m | D7 | `24:28` |
| 6m | D8 | `50:54` |
| 4m | D9 | `69:70` |

## Quick Start

1. Install the **SparkFun Alphanumeric Display** library via Arduino Library Manager
2. Connect the Qwiic display to SDA/SCL/3.3V/GND
3. Connect band detector TTL outputs to D2-D9
4. Upload `band_selector_display.ino`
5. Open Serial Monitor at 115200 baud

## Serial Commands

Press `D` to open the debug menu:

| Key | Function |
|-----|----------|
| `D` | Debug menu |
| `S` | System status |
| `T` | Temperature |
| `C` | Continuous input monitor |
| `I` | I2C bus scan |
| `B` | Cycle brightness (saved to EEPROM) |
| `R` | Reset Arduino |

## Startup Display

```
================================
 ART1K9 Band Selector - VE3VPT
================================
Operating hours: 42
Power-on count:  15
Temperature:     32.5 C

Ready. Press 'D' for debug menu.
```

## EEPROM Storage

Data persisted across power cycles:
- **Operating hours** — Counts only when band is active (not "----"), increments each 60 min
- **Power-on count** — Increments each boot
- **Brightness** — Saved when changed via 'B' command

## Installation

```bash
git clone https://github.com/ve3vpt/art1k9-band-selector.git
cd art1k9-band-selector
# Open band_selector_display.ino in Arduino IDE
```

## Configuration

Edit constants at the top of `band_selector_display.ino`:

```cpp
const char* CALLSIGN = "VE3VPT";       // Your callsign
const bool TEMP_ENABLED = true;         // Enable/disable temperature
const float ADC_MAX = 675.0;            // 675 for 3.3V, 1023 for 5V
```

## License

MIT License - See [LICENSE](LICENSE) file.

---

## Author

**Vlad Patoka (VE3VPT)**

- QRZ: [https://www.qrz.com/db/VE3VPT](https://www.qrz.com/db/VE3VPT)

---

*73 de VE3VPT*
