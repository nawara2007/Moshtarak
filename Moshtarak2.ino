#include <EEPROM.h>
#include <RCSwitch.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Pin assignments
#define LED1_PIN 3
#define LED2_PIN 4
#define LED3_PIN 5
#define LED4_PIN 6
#define ONE_WIRE_BUS 7 // DS18B20 data pin (Arduino Nano D7)

RCSwitch mySwitch = RCSwitch();
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastTempRequest = 0;   // Time of last temperature request
const unsigned long tempInterval = 30000; // ms between readings
bool tempRequested = false; // Tracks if we requested but not read yet

const byte buttonPins[4] = {9, 10, 11, 12};
bool outputState[4]; // Current output states

bool buttonStates[4] = {false}; // Current button states
bool lastButtonStates[4] = {false}; // Previous button states
unsigned long lastDebounceTime[4] = {0}; // Debounce timing
const unsigned long debounceDelay = 50; // Debounce delay

bool rfButtonStates[4] = {false, false, false, false}; // Track RF button states
unsigned long lastRFButtonPress[4] = {0}; // Last time each RF button was pressed
const unsigned long rfDebounceDelay = 300; // Delay to prevent toggling while held

void setup() {
  Serial.begin(9600); // UART for ESP8266 communication
  mySwitch.enableReceive(0);  // Receiver on interrupt 0 => that is pin #2
  sensors.begin();

  for (byte i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    outputState[i] = EEPROM.read(i);
    setLED(i + 1, outputState[i]);
  }

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(LED4_PIN, OUTPUT);

  // Initial temperature reading
  readTemperature();
}

void loop() {
  unsigned long currentMillis = millis();

  // Request temperature if interval passed and no request pending
  if (!tempRequested && (currentMillis - lastTempRequest >= tempInterval)) {
    sensors.requestTemperatures(); 
    lastTempRequest = currentMillis;
    tempRequested = true;
  }

  // Read temperature after delay
  if (tempRequested && (currentMillis - lastTempRequest >= 750)) {
    readTemperature();
    tempRequested = false;
  }

  // Handle incoming commands from ESP8266
  handleSerialCommands();

  // Get input from RF remote
  handleRFCommands();

  // Handle button inputs
  handleButtonInputs();
}

void readTemperature() {
  float tempC = sensors.getTempCByIndex(0);
  Serial.print("TEMP:"); // Send to ESP
  Serial.println(tempC, 2); 
}

void handleSerialCommands() {
  while (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    for (int i = 1; i <= 4; i++) {
      if (cmd == "LED" + String(i) + "_ON") setLED(i, false);
      else if (cmd == "LED" + String(i) + "_OFF") setLED(i, true);
    }
  }
}

void handleRFCommands() {
  if (mySwitch.available()) {
    int receivedValue = mySwitch.getReceivedValue();

    int ledIndex = -1;
    if (receivedValue == 12) ledIndex = 0;
    else if (receivedValue == 22) ledIndex = 1;
    else if (receivedValue == 32) ledIndex = 2;
    else if (receivedValue == 42) ledIndex = 3;

    if (ledIndex >= 0) {
      unsigned long currentMillis = millis();
      
      // Check if the button was pressed and not toggled recently
      if (!rfButtonStates[ledIndex] && (currentMillis - lastRFButtonPress[ledIndex] >= rfDebounceDelay)) {
        setLED(ledIndex + 1, !outputState[ledIndex]);
        rfButtonStates[ledIndex] = true; // Mark as pressed
        lastRFButtonPress[ledIndex] = currentMillis; // Update last press time
      }
    }

    mySwitch.resetAvailable();
  } else {
    // Reset the state of the RF buttons when not pressed
    for (int i = 0; i < 4; i++) {
      if (rfButtonStates[i] && (millis() - lastRFButtonPress[i] >= rfDebounceDelay)) {
        rfButtonStates[i] = false; // Reset state after debounce period
      }
    }
  }
}

void handleButtonInputs() {
  for (int i = 0; i < 4; i++) {
    bool reading = digitalRead(buttonPins[i]) == LOW; // LOW means pressed
    if (reading != lastButtonStates[i]) {
      lastDebounceTime[i] = millis(); // Reset debounce timer
    }

    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (reading != buttonStates[i]) {
        buttonStates[i] = reading; // Update button state
        if (buttonStates[i]) { // If button pressed
          setLED(i + 1, !outputState[i]);
        }
      }
    }
    lastButtonStates[i] = reading; // Save the reading as last state
  }
}

void setLED(uint8_t led, bool state) {
  digitalWrite(LED1_PIN + led - 1, state ? HIGH : LOW);
  outputState[led - 1] = state;
  EEPROM.update(led - 1, state);
  sendState(led, state);
}

void sendState(uint8_t led, bool state) {
  Serial.print("LED");
  Serial.print(led);
  Serial.println(state ? "_OFF" : "_ON");
}