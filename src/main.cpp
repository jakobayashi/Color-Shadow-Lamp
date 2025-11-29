#include <Arduino.h>
#include "LEDController.h"
#include "LTTController.h"
#include "WiFiManager.h"
#include "State.h"
#include <math.h>

const int RED_PIN = 5;
const int GREEN_PIN = 6;
const int BLUE_PIN = 7;

const int POT_RED_PIN = 4;
const int POT_GREEN_PIN = 3;
const int POT_BLUE_PIN = 0;

const int MOVING_AVERAGE_SIZE = 8; // Size of the moving average window

int pot1Values[MOVING_AVERAGE_SIZE] = {0};
int pot2Values[MOVING_AVERAGE_SIZE] = {0};
int pot3Values[MOVING_AVERAGE_SIZE] = {0};
int potIndex = 0;

int lastPot1 = 0;
int lastPot2 = 0;
int lastPot3 = 0;

//unsigned long potTimer = 0;
//const int potInterval = 50; // Check every 50ms
//bool potChanged = false;

LEDController ledController(
    RED_PIN, GREEN_PIN, BLUE_PIN,
    0, 1, 2);

LTTController lttController(ledController);
WiFiManager wifiManager(ledController);
StateHandler stateHandler(ledController);

// Simple party mode helpers
float partyHue = 0.0f; // degrees
unsigned long lastPartyMillis = 0;

int readAveragedADC(int pin, int samples = 4)
{
  int32_t sum = 0;
  for (int i = 0; i < samples; i++)
  {
    sum += analogReadMilliVolts(pin);
  }
  return sum / samples;
}

int calculateMovingAverage(int *values, int size)
{
  int sum = 0;
  for (int i = 0; i < size; i++)
  {
    sum += values[i];
  }
  return sum / size;
}

void hsvToRgb11(float hDeg, float s, float v, int &rOut, int &gOut, int &bOut)
{
  float h = fmodf(hDeg, 360.0f) / 60.0f;
  float c = v * s;
  float x = c * (1 - fabsf(fmodf(h, 2.0f) - 1));
  float m = v - c;
  float r, g, b;

  if (0 <= h && h < 1)
  {
    r = c;
    g = x;
    b = 0;
  }
  else if (1 <= h && h < 2)
  {
    r = x;
    g = c;
    b = 0;
  }
  else if (2 <= h && h < 3)
  {
    r = 0;
    g = c;
    b = x;
  }
  else if (3 <= h && h < 4)
  {
    r = 0;
    g = x;
    b = c;
  }
  else if (4 <= h && h < 5)
  {
    r = x;
    g = 0;
    b = c;
  }
  else
  {
    r = c;
    g = 0;
    b = x;
  }

  rOut = static_cast<int>((r + m) * 2047);
  gOut = static_cast<int>((g + m) * 2047);
  bOut = static_cast<int>((b + m) * 2047);
}

void setup()
{
  Serial.begin(115200);
  ledController.begin();
  stateHandler.begin();
  wifiManager.attachStateHandler(&stateHandler);
  wifiManager.begin();

  analogSetAttenuation(ADC_2_5db);
  analogSetPinAttenuation(POT_RED_PIN, ADC_2_5db);
  analogSetPinAttenuation(POT_GREEN_PIN, ADC_2_5db);
  analogSetPinAttenuation(POT_BLUE_PIN, ADC_2_5db);
}

void loop()
{
  static unsigned long lastUpdate = 0;
  const unsigned long UPDATE_INTERVAL = 20;

  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= UPDATE_INTERVAL)
  {
    lastUpdate = currentMillis;
    stateHandler.update();

    int pot1 = readAveragedADC(POT_RED_PIN);
    int pot2 = readAveragedADC(POT_GREEN_PIN);
    int pot3 = readAveragedADC(POT_BLUE_PIN);

    pot1 = map(constrain(pot1, 5, 950), 5, 950, 0, 2047); // Left pot (meant for Red)
    pot2 = map(constrain(pot2, 5, 950), 5, 950, 0, 2047); // Middle pot (meant for Green)
    pot3 = map(constrain(pot3, 5, 950), 5, 950, 0, 2047); // Right pot (meant for Blue)

    // Update moving average arrays
    pot1Values[potIndex] = pot1;
    pot2Values[potIndex] = pot2;
    pot3Values[potIndex] = pot3;
    potIndex = (potIndex + 1) % MOVING_AVERAGE_SIZE;

    // Calculate moving averages
    pot1 = calculateMovingAverage(pot1Values, MOVING_AVERAGE_SIZE);
    pot2 = calculateMovingAverage(pot2Values, MOVING_AVERAGE_SIZE);
    pot3 = calculateMovingAverage(pot3Values, MOVING_AVERAGE_SIZE);

    //Serial.print("pot1: ");
    //Serial.print(pot1);
    //Serial.print(", pot2: ");
    //Serial.print(pot2);
    //Serial.print(", pot3: ");
    //Serial.println(pot3);

    switch (stateHandler.getCurrentMode())
    {
    case OperationMode::RGB:
      // pot1 is LEFT (physically red), pot3 is RIGHT (physically blue)
      // since the LED pins are now swapped in begin(), we need to swap these too
      ledController.setPWMDirectly(pot3, pot2, pot1); // Swap values to match physical layout
      lastPartyMillis = 0;
      break;
    case OperationMode::LTT:
      lttController.updateLTT(pot1, pot2, pot3);
      lastPartyMillis = 0;
      break;
    case OperationMode::PARTY:
    {
      if (lastPartyMillis == 0)
      {
        lastPartyMillis = currentMillis;
      }
      float dt = (currentMillis - lastPartyMillis) / 1000.0f;
      lastPartyMillis = currentMillis;

      float hz = stateHandler.getPartyHz();
      partyHue += dt * hz * 360.0f;
      if (partyHue > 720.0f)
      {
        partyHue = fmodf(partyHue, 360.0f);
      }
      int r, g, b;
      hsvToRgb11(partyHue, 1.0f, 1.0f, r, g, b);
      ledController.setPWMDirectly(r, g, b);
      break;
    }
    case OperationMode::OFF:
      lastPartyMillis = 0;
      break;
    case OperationMode::WIFI:
      // LED control happens via WiFi in WIFI mode
      break;
    }
  }
  delay(2);
}
