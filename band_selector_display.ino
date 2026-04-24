/*
 * HAM Radio LDMOS Amplifier Band Selector Display
 * For ART1K9 based 2KW Amplifier (1.8MHz - 70MHz)
 *
 * Designer: Vlad Patoka (VE3VPT)
 *
 * Function: Display-only - reads band from external detector, shows on display
 *           LPF relay control is handled externally
 *
 * Hardware:
 * - Arduino UNO R4 Minima (or WiFi)
 * - SparkFun Qwiic Alphanumeric Display (HT16K33/VK16K33)
 *   Connect to top-edge header: SDA, SCL, 3.3V, GND (near AREF)
 * - Band Detection Board (active-HIGH TTL inputs on D2-D9)
 * - Optional: NTC thermistor on A0 for temperature monitoring
 *
 * Serial Commands:
 *   D - Enter debug menu
 *   T - Show temperature
 *
 * I2C Connection (top-edge header near AREF):
 *   SDA (blue)   -> SDA pin
 *   SCL (yellow) -> SCL pin
 *   3.3V (red)   -> 3.3V
 *   GND (black)  -> GND
 */

#include <Wire.h>
#include <EEPROM.h>
#include <SparkFun_Alphanumeric_Display.h>

HT16K33 display;

// === EEPROM CONFIGURATION ===
// EEPROM layout for persistent storage
const int EEPROM_MAGIC_ADDR = 0;       // 2 bytes - magic number
const int EEPROM_HOURS_ADDR = 2;       // 4 bytes - operating hours
const int EEPROM_POWERON_ADDR = 6;     // 4 bytes - power-on count
const int EEPROM_CHECKSUM_ADDR = 10;   // 1 byte - simple checksum
const int EEPROM_BRIGHTNESS_ADDR = 11; // 1 byte - display brightness
const uint16_t EEPROM_MAGIC = 0xA1B2;  // Magic number to detect initialized EEPROM

// Display brightness (0-15)
uint8_t displayBrightness = 10;        // Default brightness

// Operating statistics
uint32_t totalOperatingHours = 0;      // Total hours from EEPROM
uint32_t powerOnCount = 0;             // Power-on count from EEPROM
unsigned long sessionActiveMs = 0;     // Milliseconds active this session
unsigned long lastActiveCheck = 0;     // Last time we checked active state
unsigned long lastHourMark = 0;        // Last time we incremented hour counter
bool wasActive = false;                // Was amplifier active last check?

// === TEMPERATURE SENSING ===
// NTC thermistor configuration (10K NTC with 10K series resistor to 3.3V)
// Connect: 3.3V -> 10K resistor -> A0 -> NTC -> GND
const int TEMP_PIN = A0;               // Analog input for NTC
const bool TEMP_ENABLED = true;        // Set false if no thermistor installed
const float THERMISTOR_NOMINAL = 10000.0;  // 10K at 25°C
const float TEMP_NOMINAL = 25.0;       // Temperature for nominal resistance
const float B_COEFFICIENT = 3950.0;    // Beta coefficient (check your NTC datasheet)
const float SERIES_RESISTOR = 10000.0; // 10K series resistor
// ADC_MAX: Maximum ADC reading at full supply voltage
//   Using 5V supply with 5V ADC reference:   1023.0
//   Using 3.3V supply with 5V ADC reference: 675.0 (UNO R4)
const float ADC_MAX = 675.0;
const float TEMP_CHANGE_THRESHOLD = 2.0;   // Report if changed by 2°C
const unsigned long TEMP_INTERVAL_MS = 300000; // Report every 5 minutes (300000ms)
const float TEMP_ALERT_THRESHOLD = 41.0;   // Show alert on display if above this

float lastReportedTemp = -999.0;       // Last temperature reported
unsigned long lastTempReport = 0;      // Last time we reported temperature
bool tempAlertActive = false;          // Temperature alert state
unsigned long lastAlertToggle = 0;     // For alternating display

// === I2C SCANNER CONFIGURATION ===
const uint8_t VALID_DISPLAY_ADDRS[] = {0x70, 0x71, 0x72, 0x73};
const int NUM_VALID_ADDRS = 4;

// === PIN CONFIGURATION ===
// Band detection TTL inputs (D2-D9 for Serial Monitor compatibility)
const int BAND_INPUT_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};
const int NUM_BAND_INPUTS = 8;

// === BAND DEFINITIONS ===
typedef struct {
  uint16_t inputCode;
  const char* displayText;
  const char* freqRange;
  uint8_t lpfChannel;
} BandMapping;

const BandMapping bandTable[] = {
  {0x00, "----", NULL,       0},   // No band selected
  {0x01, "160M", "1 :4.0",   0},   // Bit 0 (D2): 1.8-2.0 MHz
  {0x02, " 80M", "1 :4.0",   0},   // Bit 1 (D3): 3.5-4.0 MHz
  {0x03, "1-4M", "1 :4.0",   0},   // COMBINED: 160m+80m
  {0x04, " 40M", "5 :7.2",   0},   // Bit 2 (D4): 5.3-7.2 MHz
  {0x08, " 20M", "10:14",    0},   // Bit 3 (D5): 10.1-14.35 MHz
  {0x10, " 17M", "18:21",    0},   // Bit 4 (D6): 18.0-21.45 MHz
  {0x20, " 10M", "24:28",    0},   // Bit 5 (D7): 24.8-28.7 MHz
  {0x40, "  6M", "50:54",    0},   // Bit 6 (D8): 50-54 MHz
  {0x80, "  4M", "69:70",    0},   // Bit 7 (D9): 69-70.5 MHz
};
const int NUM_BANDS = sizeof(bandTable) / sizeof(bandTable[0]);

// === CALLSIGN CONFIGURATION ===
const char* CALLSIGN = "VE3VPT";
const int SCROLL_DELAY_MS = 220;

// === LPF RELAY CONTROL ===
bool enableLPFControl = false;
const int LPF_OUTPUT_PINS[] = {A0, A1, A2, A3, 10, 11};
const int NUM_LPF_CHANNELS = 6;

// === STATE VARIABLES ===
uint16_t lastBandCode = 0xFFFF;
uint8_t currentLPFChannel = 0;
bool debugMode = false;                // Debug mode toggle

// === NOISE FILTER CONFIGURATION ===
const int STABLE_COUNT_THRESHOLD = 5;
uint16_t candidateBandCode = 0xFFFF;
int stableCount = 0;
const unsigned long LOCKOUT_MS = 200;
unsigned long lastBandChangeTime = 0;

// Display I2C address (0 = auto-detect)
uint8_t DISPLAY_ADDR = 0;

// === EEPROM FUNCTIONS ===

// Calculate simple checksum for EEPROM data
uint8_t calculateChecksum() {
  uint8_t sum = 0;
  sum ^= (totalOperatingHours >> 24) & 0xFF;
  sum ^= (totalOperatingHours >> 16) & 0xFF;
  sum ^= (totalOperatingHours >> 8) & 0xFF;
  sum ^= totalOperatingHours & 0xFF;
  sum ^= (powerOnCount >> 24) & 0xFF;
  sum ^= (powerOnCount >> 16) & 0xFF;
  sum ^= (powerOnCount >> 8) & 0xFF;
  sum ^= powerOnCount & 0xFF;
  sum ^= displayBrightness;
  return sum;
}

// Initialize EEPROM with default values
void initializeEEPROM() {
  totalOperatingHours = 0;
  powerOnCount = 0;
  displayBrightness = 10;
  EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  EEPROM.put(EEPROM_HOURS_ADDR, totalOperatingHours);
  EEPROM.put(EEPROM_POWERON_ADDR, powerOnCount);
  EEPROM.put(EEPROM_BRIGHTNESS_ADDR, displayBrightness);
  EEPROM.put(EEPROM_CHECKSUM_ADDR, calculateChecksum());
}

// Load statistics from EEPROM
void loadFromEEPROM() {
  uint16_t magic;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic != EEPROM_MAGIC) {
    // First time use - initialize EEPROM
    Serial.println(F("EEPROM: First use, initializing..."));
    initializeEEPROM();
  } else {
    // Load existing values
    EEPROM.get(EEPROM_HOURS_ADDR, totalOperatingHours);
    EEPROM.get(EEPROM_POWERON_ADDR, powerOnCount);
    EEPROM.get(EEPROM_BRIGHTNESS_ADDR, displayBrightness);

    // Validate brightness (0-15)
    if (displayBrightness > 15) {
      displayBrightness = 10;
    }

    // Verify checksum
    uint8_t storedChecksum;
    EEPROM.get(EEPROM_CHECKSUM_ADDR, storedChecksum);
    if (storedChecksum != calculateChecksum()) {
      Serial.println(F("EEPROM: Checksum mismatch, resetting..."));
      initializeEEPROM();
    }
  }

  // Increment power-on count
  powerOnCount++;
  EEPROM.put(EEPROM_POWERON_ADDR, powerOnCount);
  EEPROM.put(EEPROM_CHECKSUM_ADDR, calculateChecksum());
}

// Save operating hours to EEPROM (called once per hour of operation)
void saveOperatingHours() {
  EEPROM.put(EEPROM_HOURS_ADDR, totalOperatingHours);
  EEPROM.put(EEPROM_CHECKSUM_ADDR, calculateChecksum());
}

// === TEMPERATURE FUNCTIONS ===

// Read temperature from NTC thermistor (returns °C)
float readTemperature() {
  if (!TEMP_ENABLED) return -999.0;

  // Average multiple readings for stability
  float average = 0;
  for (int i = 0; i < 5; i++) {
    average += analogRead(TEMP_PIN);
    delay(2);
  }
  average /= 5.0;

  // Convert to resistance: Vcc -> Series Resistor -> A0 -> NTC -> GND
  float resistance = SERIES_RESISTOR * average / (ADC_MAX - average);

  // Steinhart-Hart simplified (B-parameter equation)
  float steinhart = resistance / THERMISTOR_NOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                            // ln(R/Ro)
  steinhart /= B_COEFFICIENT;                            // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMP_NOMINAL + 273.15);            // + (1/To)
  steinhart = 1.0 / steinhart;                           // Invert
  steinhart -= 273.15;                                   // Convert to °C

  return steinhart;
}

// Check and report temperature changes
void checkTemperature() {
  if (!TEMP_ENABLED) return;

  float currentTemp = readTemperature();
  unsigned long now = millis();
  bool shouldReport = false;

  // Check for temperature alert
  bool wasAlert = tempAlertActive;
  tempAlertActive = (currentTemp > TEMP_ALERT_THRESHOLD);

  // Report when alert state changes
  if (tempAlertActive && !wasAlert) {
    Serial.print(F("*** TEMPERATURE ALERT: "));
    Serial.print(currentTemp, 1);
    Serial.println(F(" C ***"));
    shouldReport = false;  // Already reported
  } else if (!tempAlertActive && wasAlert) {
    Serial.println(F("Temperature back to normal"));
  }

  // Report if temperature changed significantly
  if (abs(currentTemp - lastReportedTemp) >= TEMP_CHANGE_THRESHOLD) {
    shouldReport = true;
  }

  // Or report periodically
  if (now - lastTempReport >= TEMP_INTERVAL_MS) {
    shouldReport = true;
  }

  if (shouldReport && currentTemp > -100) {  // Sanity check
    Serial.print(F("Temperature: "));
    Serial.print(currentTemp, 1);
    Serial.println(F(" C"));
    lastReportedTemp = currentTemp;
    lastTempReport = now;
  }
}

// === OPERATING TIME TRACKING ===

// Track operating time (call from loop)
void trackOperatingTime() {
  unsigned long now = millis();

  // Check if amplifier is active (any band selected)
  bool isActive = (lastBandCode != 0x00 && lastBandCode != 0xFFFF);

  // Accumulate active time
  if (isActive && wasActive) {
    sessionActiveMs += (now - lastActiveCheck);
  }

  wasActive = isActive;
  lastActiveCheck = now;

  // Check if an hour has passed
  const unsigned long HOUR_MS = 3600000UL;  // 1 hour in milliseconds
  if (sessionActiveMs >= HOUR_MS) {
    sessionActiveMs -= HOUR_MS;
    totalOperatingHours++;
    saveOperatingHours();
    Serial.print(F("Operating hours: "));
    Serial.println(totalOperatingHours);
  }
}

// === SETUP ===

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize I2C
  Wire.begin();

  // Load EEPROM data
  loadFromEEPROM();

  // Print compact startup banner
  Serial.println();
  Serial.println(F("================================"));
  Serial.println(F(" ART1K9 Band Selector - VE3VPT"));
  Serial.println(F("================================"));
  Serial.print(F("Operating hours: "));
  Serial.println(totalOperatingHours);
  Serial.print(F("Power-on count:  "));
  Serial.println(powerOnCount);

  // Show temperature if enabled
  if (TEMP_ENABLED) {
    float temp = readTemperature();
    if (temp > -100) {
      Serial.print(F("Temperature:     "));
      Serial.print(temp, 1);
      Serial.println(F(" C"));
      lastReportedTemp = temp;
      lastTempReport = millis();
    }
  }

  Serial.println();

  // Find display (minimal output unless error)
  DISPLAY_ADDR = findDisplayAddress();
  if (DISPLAY_ADDR == 0) {
    Serial.println(F("ERROR: Display not found!"));
    Serial.println(F("Check Qwiic cable. Retrying..."));
    while (DISPLAY_ADDR == 0) {
      delay(2000);
      DISPLAY_ADDR = findDisplayAddress();
    }
  }

  // Initialize display
  if (display.begin(DISPLAY_ADDR) == false) {
    Serial.println(F("ERROR: Display init failed!"));
    while (display.begin(DISPLAY_ADDR) == false) {
      delay(1000);
    }
  }

  display.setBrightness(displayBrightness);  // Use saved brightness

  // Configure band input pins
  for (int i = 0; i < NUM_BAND_INPUTS; i++) {
    pinMode(BAND_INPUT_PINS[i], INPUT);
  }

  // Configure LPF output pins if enabled
  if (enableLPFControl) {
    for (int i = 0; i < NUM_LPF_CHANNELS; i++) {
      pinMode(LPF_OUTPUT_PINS[i], OUTPUT);
      digitalWrite(LPF_OUTPUT_PINS[i], LOW);
    }
  }

  // Startup display sequence
  displayStartupSequence();

  Serial.println(F("Ready. Press 'D' for debug menu."));
  Serial.println();

  lastActiveCheck = millis();
  lastHourMark = millis();
}

// === MAIN LOOP ===

void loop() {
  // Check for serial commands
  processSerialCommands();

  // Read current band code
  uint16_t bandCode = readBandInputs();

  // Debug mode: continuous input display
  if (debugMode) {
    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 500) {
      lastDebugPrint = millis();
      Serial.print(F("Inputs: 0x"));
      Serial.print(bandCode, HEX);
      Serial.print(F(" = "));
      for (int i = NUM_BAND_INPUTS - 1; i >= 0; i--) {
        Serial.print((bandCode >> i) & 1);
      }
      Serial.println();
    }
  }

  // Noise filter
  if (bandCode == candidateBandCode) {
    if (stableCount < STABLE_COUNT_THRESHOLD) {
      stableCount++;
    }
  } else {
    candidateBandCode = bandCode;
    stableCount = 1;
  }

  // Accept band change
  if (stableCount >= STABLE_COUNT_THRESHOLD &&
      bandCode != lastBandCode &&
      (millis() - lastBandChangeTime) >= LOCKOUT_MS) {
    lastBandCode = bandCode;
    lastBandChangeTime = millis();
    processBandChange(bandCode);
  }

  // Track operating time
  trackOperatingTime();

  // Check temperature periodically
  static unsigned long lastTempCheck = 0;
  if (millis() - lastTempCheck > 10000) {  // Check every 10 seconds
    lastTempCheck = millis();
    checkTemperature();
  }

  // Temperature alert display - scroll "RELAY HOT!" then show band
  if (tempAlertActive) {
    unsigned long now = millis();
    if (now - lastAlertToggle > 5000) {  // Scroll every 5 seconds
      lastAlertToggle = now;
      scrollText("RELAY HOT!", 180);  // Scroll warning message
      processBandChange(lastBandCode);  // Restore band display after scroll
    }
  }

  delay(10);
}

// === SERIAL COMMAND PROCESSING ===

void processSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    while (Serial.available()) Serial.read();  // Flush

    switch (cmd) {
      case 'd':
      case 'D':
        showDebugMenu();
        break;

      case 't':
      case 'T':
        showTemperature();
        break;

      case 's':
      case 'S':
        showStatus();
        break;

      case 'c':
      case 'C':
        debugMode = !debugMode;
        Serial.print(F("Continuous debug: "));
        Serial.println(debugMode ? "ON" : "OFF");
        break;

      case 'i':
      case 'I':
        scanI2CBus();
        break;

      case 'b':
      case 'B':
        cycleBrightness();
        break;

      case 'r':
      case 'R':
        resetArduino();
        break;

      case '\n':
      case '\r':
        break;

      default:
        Serial.println(F("Press 'D' for debug menu"));
    }
  }
}

// Show debug menu
void showDebugMenu() {
  Serial.println();
  Serial.println(F("=== DEBUG MENU ==="));
  Serial.println(F("  S - System status"));
  Serial.println(F("  T - Temperature"));
  Serial.println(F("  C - Continuous input monitor (toggle)"));
  Serial.println(F("  I - I2C bus scan"));
  Serial.println(F("  B - Cycle brightness"));
  Serial.println(F("  R - Reset Arduino (reboot)"));
  Serial.println();
  showStatus();
}

// Show current temperature with debug info
void showTemperature() {
  if (!TEMP_ENABLED) {
    Serial.println(F("Temperature sensor not enabled"));
    return;
  }

  // Read raw ADC for debugging
  float average = 0;
  for (int i = 0; i < 5; i++) {
    average += analogRead(TEMP_PIN);
    delay(2);
  }
  average /= 5.0;

  float resistance = SERIES_RESISTOR * average / (ADC_MAX - average);
  float temp = readTemperature();

  Serial.print(F("ADC: "));
  Serial.print(average, 0);
  Serial.print(F(" | R: "));
  Serial.print(resistance, 0);
  Serial.print(F(" ohm | Temp: "));
  Serial.print(temp, 1);
  Serial.println(F(" C"));
}

// Show system status
void showStatus() {
  Serial.println(F("--- System Status ---"));
  Serial.print(F("Operating hours:  "));
  Serial.println(totalOperatingHours);
  Serial.print(F("Session active:   "));
  Serial.print(sessionActiveMs / 60000);
  Serial.println(F(" min"));
  Serial.print(F("Power-on count:   "));
  Serial.println(powerOnCount);
  Serial.print(F("Display address:  0x"));
  Serial.println(DISPLAY_ADDR, HEX);
  Serial.print(F("Brightness:       "));
  Serial.println(displayBrightness);
  Serial.print(F("Current band:     0x"));
  Serial.print(lastBandCode, HEX);
  Serial.print(F(" ("));
  Serial.print(getBandName(lastBandCode));
  Serial.println(F(")"));

  // Input pin states
  Serial.print(F("Input pins:       "));
  uint16_t code = readBandInputs();
  for (int i = NUM_BAND_INPUTS - 1; i >= 0; i--) {
    Serial.print((code >> i) & 1);
  }
  Serial.print(F(" (0x"));
  Serial.print(code, HEX);
  Serial.println(F(")"));

  if (TEMP_ENABLED) {
    Serial.print(F("Temperature:      "));
    Serial.print(readTemperature(), 1);
    Serial.println(F(" C"));
  }
  Serial.println();
}

// Reset Arduino (software reboot)
void resetArduino() {
  Serial.println(F("Resetting Arduino..."));
  Serial.flush();
  delay(100);
  NVIC_SystemReset();  // ARM CMSIS reset function
}

// Get band name from code
const char* getBandName(uint16_t code) {
  for (int i = 0; i < NUM_BANDS; i++) {
    if (bandTable[i].inputCode == code) {
      return bandTable[i].displayText;
    }
  }
  return "????";
}

// Cycle brightness and save to EEPROM
void cycleBrightness() {
  displayBrightness = (displayBrightness + 3) % 16;
  display.setBrightness(displayBrightness);

  // Save to EEPROM (only when changed via console)
  EEPROM.put(EEPROM_BRIGHTNESS_ADDR, displayBrightness);
  EEPROM.put(EEPROM_CHECKSUM_ADDR, calculateChecksum());

  Serial.print(F("Brightness: "));
  Serial.print(displayBrightness);
  Serial.println(F(" (saved)"));
}

// === BAND INPUT READING ===

uint16_t readBandInputs() {
  const int NUM_SAMPLES = 7;
  const int SAMPLE_DELAY_US = 500;
  uint16_t samples[NUM_SAMPLES];

  for (int s = 0; s < NUM_SAMPLES; s++) {
    uint16_t code = 0;
    for (int i = 0; i < NUM_BAND_INPUTS; i++) {
      bool pinState = digitalRead(BAND_INPUT_PINS[i]);
      if (pinState) {
        code |= (1 << i);
      }
    }
    samples[s] = code;
    if (s < NUM_SAMPLES - 1) {
      delayMicroseconds(SAMPLE_DELAY_US);
    }
  }

  // Majority vote
  uint16_t bestCode = samples[0];
  int bestCount = 1;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int count = 0;
    for (int j = 0; j < NUM_SAMPLES; j++) {
      if (samples[j] == samples[i]) count++;
    }
    if (count > bestCount) {
      bestCount = count;
      bestCode = samples[i];
    }
  }
  return bestCode;
}

// === BAND CHANGE PROCESSING ===

void processBandChange(uint16_t bandCode) {
  const char* bandText = "----";
  const char* freqRange = NULL;
  uint8_t lpfChannel = 0;
  bool found = false;

  for (int i = 0; i < NUM_BANDS; i++) {
    if (bandTable[i].inputCode == bandCode) {
      bandText = bandTable[i].displayText;
      freqRange = bandTable[i].freqRange;
      lpfChannel = bandTable[i].lpfChannel;
      found = true;
      break;
    }
  }

  if (!found && bandCode != 0) {
    for (int bit = 0; bit < NUM_BAND_INPUTS; bit++) {
      if (bandCode & (1 << bit)) {
        uint16_t singleBitCode = (1 << bit);
        for (int i = 0; i < NUM_BANDS; i++) {
          if (bandTable[i].inputCode == singleBitCode) {
            bandText = bandTable[i].displayText;
            freqRange = bandTable[i].freqRange;
            lpfChannel = bandTable[i].lpfChannel;
            found = true;
            break;
          }
        }
        break;
      }
    }
  }

  // Update display
  if (freqRange != NULL) {
    display.colonOff();
    display.decimalOff();
    display.print(freqRange);
  } else {
    display.colonOff();
    display.decimalOff();
    display.print(bandText);
  }

  // Update LPF if enabled
  if (enableLPFControl && lpfChannel != currentLPFChannel) {
    setLPFChannel(lpfChannel);
    currentLPFChannel = lpfChannel;
  }

  // Log band change
  Serial.print(F("Band: "));
  Serial.print(bandText);
  if (freqRange != NULL) {
    Serial.print(F(" ("));
    Serial.print(freqRange);
    Serial.print(F(" MHz)"));
  }
  Serial.print(F(" | 0x"));
  Serial.println(bandCode, HEX);
}

// === LPF CONTROL ===

void setLPFChannel(uint8_t channel) {
  for (int i = 0; i < NUM_LPF_CHANNELS; i++) {
    digitalWrite(LPF_OUTPUT_PINS[i], LOW);
  }
  delay(10);
  if (channel >= 1 && channel <= NUM_LPF_CHANNELS) {
    digitalWrite(LPF_OUTPUT_PINS[channel - 1], HIGH);
  }
}

// === DISPLAY FUNCTIONS ===

void scrollText(const char* text, int delayMs) {
  int len = strlen(text);
  char buffer[5] = "    ";
  for (int pos = 0; pos < len + 4; pos++) {
    for (int i = 0; i < 4; i++) {
      int textIndex = pos - (3 - i);
      if (textIndex >= 0 && textIndex < len) {
        buffer[i] = text[textIndex];
      } else {
        buffer[i] = ' ';
      }
    }
    display.print(buffer);
    delay(delayMs);
  }
}

void displayStartupSequence() {
  display.colonOff();
  display.decimalOff();
  scrollText(CALLSIGN, SCROLL_DELAY_MS);
  display.print("----");
}

// === I2C FUNCTIONS ===

uint8_t findDisplayAddress() {
  for (int i = 0; i < NUM_VALID_ADDRS; i++) {
    Wire.beginTransmission(VALID_DISPLAY_ADDRS[i]);
    if (Wire.endTransmission() == 0) {
      return VALID_DISPLAY_ADDRS[i];
    }
  }
  return 0;
}

void scanI2CBus() {
  Serial.println(F("Scanning I2C bus..."));
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  0x"));
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      Serial.print(F(" - "));
      Serial.println(identifyI2CDevice(addr));
      count++;
    }
  }
  if (count == 0) {
    Serial.println(F("  No devices found!"));
  } else {
    Serial.print(F("  Found: "));
    Serial.println(count);
  }
}

const char* identifyI2CDevice(byte address) {
  switch(address) {
    case 0x70: case 0x71: case 0x72: case 0x73:
      return "HT16K33 Display";
    case 0x3C: case 0x3D:
      return "OLED Display";
    case 0x27: case 0x3F:
      return "LCD Backpack";
    case 0x48: case 0x49: case 0x4A: case 0x4B:
      return "ADS1115 ADC";
    case 0x68:
      return "DS3231 RTC";
    case 0x76: case 0x77:
      return "BME280 Sensor";
    default:
      return "Unknown";
  }
}
