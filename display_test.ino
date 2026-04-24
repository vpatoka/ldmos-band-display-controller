/*
 * Sparkfun Qwiic Alphanumeric Display Test
 * Use this to verify display connection before main sketch
 *
 * Arduino R4 Qwiic connection:
 * - Connect Qwiic cable between Arduino R4 and display
 * - Or wire manually: SDA->A4, SCL->A5, VCC->3.3V, GND->GND
 */

#include <Wire.h>
#include <SparkFun_Alphanumeric_Display.h>

HT16K33 display;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n=== Display Test ==="));

  Wire.begin();

  // Try to connect
  Serial.print(F("Connecting to display at 0x70... "));
  if (display.begin(0x70) == false) {
    Serial.println(F("FAILED!"));
    Serial.println(F("Check wiring:"));
    Serial.println(F("  SDA -> A4 (or Qwiic)"));
    Serial.println(F("  SCL -> A5 (or Qwiic)"));
    Serial.println(F("  VCC -> 3.3V"));
    Serial.println(F("  GND -> GND"));
    while(1) delay(100);
  }
  Serial.println(F("OK!"));

  display.setBrightness(10);
}

void loop() {
  // Test all HAM bands
  const char* testStrings[] = {
    "160M", " 80M", " 60M", " 40M", " 30M",
    " 20M", " 17M", " 15M", " 12M", " 10M", "  6M",
    "2KW ", "ART1", "K9  ", "LDMO", "----"
  };

  for (int i = 0; i < 16; i++) {
    display.print(testStrings[i]);
    Serial.print(F("Display: "));
    Serial.println(testStrings[i]);
    delay(700);
  }

  // Brightness test
  Serial.println(F("Brightness test..."));
  display.print("BRIT");
  for (int b = 0; b <= 15; b++) {
    display.setBrightness(b);
    delay(150);
  }
  display.setBrightness(10);
  delay(500);
}
