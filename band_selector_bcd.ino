/*
 * HAM Radio LDMOS Amplifier Band Selector Display
 * BCD ENCODING VERSION
 *
 * For ART1K9 based 2KW Amplifier (1.8MHz - 50MHz)
 * Uses 4-bit BCD encoding from band detector
 *
 * Hardware:
 * - Arduino R4 (WiFi or Minima)
 * - Sparkfun Qwiic Alphanumeric Display (HT16K33/VK16K33)
 * - Band Detection Board with BCD output (4 TTL lines)
 */

#include <Wire.h>
#include <SparkFun_Alphanumeric_Display.h>

HT16K33 display;

// === PIN CONFIGURATION ===
// BCD input pins (D0-D3 from band detector)
const int BCD_PIN_D0 = 2;   // LSB
const int BCD_PIN_D1 = 3;
const int BCD_PIN_D2 = 4;
const int BCD_PIN_D3 = 5;   // MSB

// Optional: Band valid signal (some detectors have this)
const int BAND_VALID_PIN = 6;
bool useBandValidPin = false;

// === BAND TABLE ===
// BCD code (0-15) maps to band display and LPF channel
typedef struct {
  const char* displayText;  // 4 chars for display
  uint8_t lpfChannel;       // LPF channel (1-6)
  float freqMHz;           // Center frequency for reference
} BandInfo;

// Index = BCD code (0-15)
const BandInfo bandLookup[16] = {
  // BCD  Display  LPF   Freq
  {"STBY", 0,  0.0},     // 0 - Standby
  {"160M", 1,  1.9},     // 1 - 160m (1.8-2.0 MHz)
  {" 80M", 1,  3.6},     // 2 - 80m (3.5-4.0 MHz)
  {" 60M", 2,  5.3},     // 3 - 60m (5.3 MHz) - uses 40m LPF
  {" 40M", 2,  7.1},     // 4 - 40m (7.0-7.3 MHz)
  {" 30M", 3, 10.1},     // 5 - 30m (10.1 MHz) - uses 20m LPF
  {" 20M", 3, 14.1},     // 6 - 20m (14.0-14.35 MHz)
  {" 17M", 4, 18.1},     // 7 - 17m (18.068-18.168 MHz)
  {" 15M", 4, 21.1},     // 8 - 15m (21.0-21.45 MHz)
  {" 12M", 5, 24.9},     // 9 - 12m (24.89-24.99 MHz) - uses 10m LPF
  {" 10M", 5, 28.5},     // 10 - 10m (28.0-29.7 MHz)
  {"  6M", 6, 50.1},     // 11 - 6m (50.0-54.0 MHz)
  {"----", 0,  0.0},     // 12 - Reserved
  {"----", 0,  0.0},     // 13 - Reserved
  {"----", 0,  0.0},     // 14 - Reserved
  {"ERR ", 0,  0.0},     // 15 - Error/Invalid
};

// === LPF RELAY OUTPUT PINS ===
const int LPF_PIN_CH1 = A0;   // 160m/80m
const int LPF_PIN_CH2 = A1;   // 40m
const int LPF_PIN_CH3 = A2;   // 20m
const int LPF_PIN_CH4 = A3;   // 17m/15m
const int LPF_PIN_CH5 = A4;   // 12m/10m
const int LPF_PIN_CH6 = A5;   // 6m

const int LPF_PINS[] = {LPF_PIN_CH1, LPF_PIN_CH2, LPF_PIN_CH3,
                        LPF_PIN_CH4, LPF_PIN_CH5, LPF_PIN_CH6};
const int NUM_LPF = 6;

bool enableLPFOutput = true;

// === STATE ===
uint8_t lastBCD = 0xFF;
uint8_t currentLPF = 0;
unsigned long lastChangeTime = 0;
const unsigned long DEBOUNCE_MS = 30;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== ART1K9 Band Selector (BCD) ==="));

  // I2C and display
  Wire.begin();
  if (!display.begin(0x70)) {
    Serial.println(F("Display not found!"));
    while(1) delay(100);
  }
  display.setBrightness(10);
  Serial.println(F("Display OK"));

  // BCD input pins (with pullup - assume active LOW)
  pinMode(BCD_PIN_D0, INPUT_PULLUP);
  pinMode(BCD_PIN_D1, INPUT_PULLUP);
  pinMode(BCD_PIN_D2, INPUT_PULLUP);
  pinMode(BCD_PIN_D3, INPUT_PULLUP);

  if (useBandValidPin) {
    pinMode(BAND_VALID_PIN, INPUT_PULLUP);
  }

  // LPF outputs
  if (enableLPFOutput) {
    for (int i = 0; i < NUM_LPF; i++) {
      pinMode(LPF_PINS[i], OUTPUT);
      digitalWrite(LPF_PINS[i], LOW);
    }
    Serial.println(F("LPF outputs configured"));
  }

  // Startup
  display.print("ART1");
  delay(600);
  display.print("K9  ");
  delay(600);
  display.print("STBY");

  Serial.println(F("Ready\n"));
}

void loop() {
  // Check band valid signal if used
  if (useBandValidPin && digitalRead(BAND_VALID_PIN) == HIGH) {
    // Band not valid - skip reading
    delay(10);
    return;
  }

  // Read BCD value
  uint8_t bcd = readBCD();

  // Process if changed (with debounce)
  if (bcd != lastBCD) {
    if (millis() - lastChangeTime > DEBOUNCE_MS) {
      lastChangeTime = millis();
      lastBCD = bcd;
      updateBand(bcd);
    }
  }

  delay(10);
}

// Read 4-bit BCD from input pins
uint8_t readBCD() {
  uint8_t val = 0;

  // Assuming active LOW (invert readings)
  if (!digitalRead(BCD_PIN_D0)) val |= 0x01;
  if (!digitalRead(BCD_PIN_D1)) val |= 0x02;
  if (!digitalRead(BCD_PIN_D2)) val |= 0x04;
  if (!digitalRead(BCD_PIN_D3)) val |= 0x08;

  // For active HIGH signals, remove the '!' negation above

  return val;
}

// Update display and LPF based on BCD code
void updateBand(uint8_t bcd) {
  // Bounds check
  if (bcd > 15) bcd = 15;

  const BandInfo* band = &bandLookup[bcd];

  // Update display
  display.print(band->displayText);

  // Update LPF if needed
  if (enableLPFOutput && band->lpfChannel != currentLPF) {
    setLPF(band->lpfChannel);
    currentLPF = band->lpfChannel;
  }

  // Debug output
  Serial.print(F("BCD: "));
  Serial.print(bcd);
  Serial.print(F(" -> "));
  Serial.print(band->displayText);
  Serial.print(F(" | LPF CH"));
  Serial.print(band->lpfChannel);
  if (band->freqMHz > 0) {
    Serial.print(F(" | ~"));
    Serial.print(band->freqMHz, 1);
    Serial.print(F(" MHz"));
  }
  Serial.println();
}

// Set LPF relay channel (1-6, 0=all off)
void setLPF(uint8_t channel) {
  // All off first (break before make)
  for (int i = 0; i < NUM_LPF; i++) {
    digitalWrite(LPF_PINS[i], LOW);
  }
  delay(5);  // Relay settling

  // Activate channel
  if (channel >= 1 && channel <= NUM_LPF) {
    digitalWrite(LPF_PINS[channel - 1], HIGH);
  }
}
