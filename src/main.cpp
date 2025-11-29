#include <Arduino.h>
#include "LEDController.h"
#include "WiFiManager.h"
#include "State.h"
#include <math.h>

const int RED_PIN = 5;
const int GREEN_PIN = 6;
const int BLUE_PIN = 7;

//unsigned long potTimer = 0;
//const int potInterval = 50; // Check every 50ms
//bool potChanged = false;

LEDController ledController(
    RED_PIN, GREEN_PIN, BLUE_PIN,
    0, 1, 2);

WiFiManager wifiManager(ledController);
StateHandler stateHandler(ledController);

// Simple party mode helpers
float partyHue = 0.0f; // degrees
unsigned long lastPartyMillis = 0;
float musicPhase = 0.0f; // radians for BPM fade
unsigned long lastMusicMillis = 0;
OperationMode lastMode = OperationMode::WIFI;

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

float advanceSeconds(unsigned long &lastTimestamp, unsigned long nowMillis)
{
  if (lastTimestamp == 0)
  {
    lastTimestamp = nowMillis;
    return 0.0f;
  }
  float dt = (nowMillis - lastTimestamp) / 1000.0f;
  lastTimestamp = nowMillis;
  return dt;
}

void setup()
{
  Serial.begin(115200);
  ledController.begin();
  stateHandler.begin();
  wifiManager.attachStateHandler(&stateHandler);
  wifiManager.begin();
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
    wifiManager.update(stateHandler.getCurrentMode());

    OperationMode mode = stateHandler.getCurrentMode();
    if (mode != lastMode)
    {
      if (mode != OperationMode::PARTY)
      {
        partyHue = 0.0f;
        lastPartyMillis = 0;
      }
      if (mode != OperationMode::MUSIC)
      {
        musicPhase = 0.0f;
        lastMusicMillis = 0;
      }
    }

    switch (mode)
    {
    case OperationMode::PARTY:
    {
      float dt = advanceSeconds(lastPartyMillis, currentMillis);
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
    case OperationMode::MUSIC:
    {
      float dt = advanceSeconds(lastMusicMillis, currentMillis);
      float hz = max(stateHandler.getPartyHz(), 0.05f);
      musicPhase += dt * hz * (2.0f * PI);
      if (musicPhase > 2.0f * PI)
      {
        musicPhase = fmodf(musicPhase, 2.0f * PI);
      }

      // Fade white based on BPM-driven frequency
      float fade = (sinf(musicPhase) + 1.0f) * 0.5f; // 0..1
      const float minLevel = 0.15f;                  // keep a little base light
      float brightness = minLevel + fade * (1.0f - minLevel);
      int pwm = static_cast<int>(brightness * 2047);
      ledController.setPWMDirectly(pwm, pwm, pwm);
      break;
    }
    case OperationMode::OFF:
      lastPartyMillis = 0;
      break;
    case OperationMode::WIFI:
      // LED control happens via WiFi in WIFI mode
      break;
    }

    lastMode = mode;
  }
  delay(2);
}
