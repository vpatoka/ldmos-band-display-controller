/*
 * I2C Scanner Utility
 * Use this to find I2C devices and verify display address
 *
 * Upload to Arduino, open Serial Monitor at 115200 baud
 * Expected output: "Found device at 0x70" for Qwiic display
 */

#include <Wire.h>

void setup() {
  Wire.begin();
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n============================="));
  Serial.println(F("      I2C Bus Scanner"));
  Serial.println(F("=============================\n"));
}

void loop() {
  byte deviceCount = 0;

  Serial.println(F("Scanning I2C bus..."));
  Serial.println();

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("  Device found at address 0x"));
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);

      // Identify common devices
      Serial.print(F("  -> "));
      switch(address) {
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
          Serial.print(F("HT16K33 Alphanumeric Display"));
          break;
        case 0x3C:
        case 0x3D:
          Serial.print(F("OLED Display (SSD1306)"));
          break;
        case 0x27:
        case 0x3F:
          Serial.print(F("LCD I2C Backpack (PCF8574)"));
          break;
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
          Serial.print(F("ADS1115 ADC"));
          break;
        case 0x68:
          Serial.print(F("DS3231 RTC or MPU6050"));
          break;
        default:
          Serial.print(F("Unknown device"));
      }
      Serial.println();

      deviceCount++;
    }
    else if (error == 4) {
      Serial.print(F("  Error at address 0x"));
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  Serial.println();
  if (deviceCount == 0) {
    Serial.println(F("No I2C devices found!"));
    Serial.println(F("Check wiring:"));
    Serial.println(F("  - SDA to A4"));
    Serial.println(F("  - SCL to A5"));
    Serial.println(F("  - VCC to 3.3V"));
    Serial.println(F("  - GND to GND"));
  }
  else {
    Serial.print(F("Found "));
    Serial.print(deviceCount);
    Serial.println(F(" device(s)"));
  }

  Serial.println(F("\n--- Scan complete. Next scan in 5 seconds ---\n"));
  delay(5000);
}
