#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include "LEDController.h"
#include <ESPmDNS.h>
#include "State.h"
#include <HTTPClient.h>

class WiFiManager
{
private:
    AsyncWebServer server;
    LEDController &ledController;
    StateHandler *stateHandler = nullptr;

    // Station (home network) credentials and static IP config
    const char *staSsid = "USSS-Van-4";
    const char *staPassword = "40LeeNumber1";
    IPAddress localIp = IPAddress(192, 168, 1, 80);
    IPAddress gateway = IPAddress(192, 168, 1, 1);
    IPAddress subnet = IPAddress(255, 255, 255, 0);
    IPAddress primaryDns = IPAddress(8, 8, 8, 8);
    IPAddress secondaryDns = IPAddress(1, 1, 1, 1);

    // Access Point fallback config
    const char *apSsid = "Color_Shadow";
    const char *apPassword = "password";

    // Music proxy (companion service) endpoint
    const char *musicProxyBase = "http://192.168.1.20:3000";
    const char *musicPlaybackPath = "/playback";
    unsigned long lastMusicPoll = 0;
    const unsigned long MUSIC_POLL_INTERVAL_MS = 3000;

    bool apFallback = false;
    bool started = false;
    float partyHz = 0.6f;

    struct MusicInfo
    {
        String track;
        String artist;
        String albumArt;
        String nextTrack;
        String nextArtist;
        int durationMs = 0;
        int progressMs = 0;
        float bpm = 0.0f;
        unsigned long lastUpdated = 0;
        bool hasData = false;
    } musicInfo;

    unsigned long lastUpdate = 0;
    const unsigned long MIN_UPDATE_INTERVAL = 5;

    void handleRoot(AsyncWebServerRequest *request)
    {
        Serial.println("Serving index.html");
        request->send(SPIFFS, "/index.html", "text/html");
    }

    void handleIroMin(AsyncWebServerRequest *request)
    {
        Serial.println("Serving iro.min.js");
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/iro.min.js", "text/javascript");
        response->addHeader("Cache-Control", "max-age=31536000");
        response->addHeader("Expires", "Thu, 31 Dec 2037 23:59:59 GMT");
        request->send(response);
    }

    void handleIroScript(AsyncWebServerRequest *request)
    {
        Serial.println("Serving iro_script.js");
        request->send(SPIFFS, "/iro_script.js", "text/javascript");
    }

    void handleLockStatus(AsyncWebServerRequest *request)
    {
        Serial.println("Lock status requested");
        bool isUnlocked = ledController.isUnlocked();
        Serial.printf("Current lock status: %s\n", isUnlocked ? "unlocked" : "locked");
        String response = isUnlocked ? "{\"unlocked\":true}" : "{\"unlocked\":false}";
        request->send(200, "application/json", response);
    }

    void handleUnlock(AsyncWebServerRequest *request)
    {
        Serial.println("Unlock requested");
        ledController.unlock();
        ledController.checkAndUpdatePowerLimit();
        Serial.println("Unlock complete");
        request->send(200, "text/plain", "OK");
    }

    void handleReset(AsyncWebServerRequest *request)
    {
        Serial.println("Reset requested");
        ledController.resetToSafeMode();
        ledController.checkAndUpdatePowerLimit();
        Serial.println("Reset complete");
        request->send(200, "text/plain", "OK");
    }

    void handleRGB(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
    {
        if (request->hasParam("r", true) && request->hasParam("g", true) && request->hasParam("b", true))
        {
            int r = request->getParam("r", true)->value().toInt();
            int g = request->getParam("g", true)->value().toInt();
            int b = request->getParam("b", true)->value().toInt();

            // Debug print
            Serial.printf("Received RGB request: r=%d, g=%d, b=%d\n", r, g, b);

            int mappedR = map(constrain(r, 0, 255), 0, 255, 0, 2047);
            int mappedG = map(constrain(g, 0, 255), 0, 255, 0, 2047);
            int mappedB = map(constrain(b, 0, 255), 0, 255, 0, 2047);

            Serial.printf("Mapped RGB values: r=%d, g=%d, b=%d\n", mappedR, mappedG, mappedB);

            ledController.setPWMDirectly(mappedR, mappedG, mappedB);
            request->send(200, "text/plain", "OK");
        }
        else
        {
            Serial.println("Invalid RGB parameters received");
            request->send(400, "text/plain", "Bad Request");
        }
    }

    String modeToString(OperationMode mode) const
    {
        switch (mode)
        {
        case OperationMode::PARTY:
            return "party";
        case OperationMode::MUSIC:
            return "music";
        case OperationMode::WIFI:
            return "wifi";
        case OperationMode::OFF:
            return "off";
        default:
            return "unknown";
        }
    }

    bool stringToMode(const String &value, OperationMode &modeOut) const
    {
        String lower = value;
        lower.toLowerCase();
        if (lower == "party")
        {
            modeOut = OperationMode::PARTY;
            return true;
        }
        if (lower == "music")
        {
            modeOut = OperationMode::MUSIC;
            return true;
        }
        if (lower == "wifi" || lower == "remote")
        {
            modeOut = OperationMode::WIFI;
            return true;
        }
        if (lower == "off" || lower == "sleep")
        {
            modeOut = OperationMode::OFF;
            return true;
        }
        return false;
    }

    void sendStatus(AsyncWebServerRequest *request)
    {
        String mode = "unknown";
        if (stateHandler)
        {
            mode = modeToString(stateHandler->getCurrentMode());
        }

        IPAddress ip = apFallback ? WiFi.softAPIP() : WiFi.localIP();
        String ipStr = ip.toString();

        String payload = "{";
        payload += "\"mode\":\"" + mode + "\",";
        payload += "\"unlocked\":" + String(ledController.isUnlocked() ? "true" : "false") + ",";
        payload += "\"ip\":\"" + ipStr + "\",";
        payload += "\"apFallback\":" + String(apFallback ? "true" : "false") + ",";
        payload += "\"partyHz\":" + String(partyHz, 2) + ",";
        payload += "\"musicBpm\":" + String(musicInfo.bpm, 2);
        payload += "}";

        request->send(200, "application/json", payload);
    }

    void applyScene(const String &scene)
    {
        String key = scene;
        key.toLowerCase();

        int r = 0, g = 0, b = 0;
        if (key == "sunset")
        {
            r = 1900;
            g = 750;
            b = 180;
        }
        else if (key == "ocean")
        {
            r = 250;
            g = 1100;
            b = 1900;
        }
        else if (key == "forest")
        {
            r = 250;
            g = 1600;
            b = 450;
        }
        else if (key == "focus")
        {
            r = 1450;
            g = 1500;
            b = 1400;
        }
        else if (key == "calm")
        {
            r = 900;
            g = 1050;
            b = 1200;
        }
        else if (key == "off")
        {
            r = g = b = 0;
        }
        else
        {
            // Unknown scene
            return;
        }

        if (stateHandler)
        {
            stateHandler->setMode(OperationMode::WIFI);
        }
        ledController.setPWMDirectly(r, g, b);
    }

    void updatePartyHz(float hz)
    {
        partyHz = hz;
        if (stateHandler)
        {
            stateHandler->setPartyHz(hz);
        }
    }

    void sendMusicStatus(AsyncWebServerRequest *request)
    {
        // Refresh data on-demand so UI sees latest even if mode isn't actively polling
        pollMusicPlayback();

        String payload = "{";
        payload += "\"track\":\"" + musicInfo.track + "\",";
        payload += "\"artist\":\"" + musicInfo.artist + "\",";
        payload += "\"albumArt\":\"" + musicInfo.albumArt + "\",";
        payload += "\"nextTrack\":\"" + musicInfo.nextTrack + "\",";
        payload += "\"nextArtist\":\"" + musicInfo.nextArtist + "\",";
        payload += "\"durationMs\":" + String(musicInfo.durationMs) + ",";
        payload += "\"progressMs\":" + String(musicInfo.progressMs) + ",";
        payload += "\"bpm\":" + String(musicInfo.bpm, 2) + ",";
        payload += "\"hasData\":" + String(musicInfo.hasData ? "true" : "false");
        payload += "}";
        request->send(200, "application/json", payload);
    }

    String extractStringValue(const String &body, const String &key)
    {
        String pattern = "\"" + key + "\"";
        int pos = body.indexOf(pattern);
        if (pos < 0) return "";
        pos = body.indexOf(":", pos);
        if (pos < 0) return "";
        // skip colon and any whitespace
        while (pos < (int)body.length() && (body[pos] == ':' || body[pos] == ' ')) pos++;
        if (pos >= (int)body.length() || body[pos] != '\"') return "";
        pos++; // move past opening quote
        int end = body.indexOf("\"", pos);
        if (end < 0) return "";
        return body.substring(pos, end);
    }

    long extractLongValue(const String &body, const String &key)
    {
        String pattern = "\"" + key + "\"";
        int pos = body.indexOf(pattern);
        if (pos < 0) return -1;
        pos = body.indexOf(":", pos);
        if (pos < 0) return -1;
        while (pos < (int)body.length() && (body[pos] == ':' || body[pos] == ' ')) pos++;
        int end = pos;
        while (end < (int)body.length() && (isDigit(body[end]) || body[end] == '-')) end++;
        if (end == pos) return -1;
        return body.substring(pos, end).toInt();
    }

    float extractFloatValue(const String &body, const String &key)
    {
        String pattern = "\"" + key + "\"";
        int pos = body.indexOf(pattern);
        if (pos < 0) return -1.0f;
        pos = body.indexOf(":", pos);
        if (pos < 0) return -1.0f;
        while (pos < (int)body.length() && (body[pos] == ':' || body[pos] == ' ')) pos++;
        int end = pos;
        while (end < (int)body.length() && (isDigit(body[end]) || body[end] == '.' || body[end] == '-')) end++;
        if (end == pos) return -1.0f;
        return body.substring(pos, end).toFloat();
    }

    void pollMusicPlayback()
    {
        if (millis() - lastMusicPoll < MUSIC_POLL_INTERVAL_MS)
        {
            return;
        }
        lastMusicPoll = millis();

        String url = String(musicProxyBase) + musicPlaybackPath;
        HTTPClient http;
        http.setTimeout(3000);
        if (!http.begin(url))
        {
            Serial.println("[Music] http.begin failed");
            return;
        }
        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            Serial.printf("[Music] HTTP error %d from proxy, WiFi status=%d\n", code, WiFi.status());
            Serial.printf("[Music] errorString: %s\n", http.errorToString(code).c_str());
            http.end();
            return;
        }
        String body = http.getString();
        http.end();
        if (body.length() == 0)
        {
            Serial.println("[Music] Empty body from proxy");
            return;
        }
        Serial.printf("[Music] body len=%d\n", body.length());

        MusicInfo info;
        info.track = extractStringValue(body, "track");
        info.artist = extractStringValue(body, "artist");
        info.albumArt = extractStringValue(body, "albumArt");
        info.nextTrack = extractStringValue(body, "nextTrack");
        info.nextArtist = extractStringValue(body, "nextArtist");
        info.durationMs = (int)extractLongValue(body, "durationMs");
        info.progressMs = (int)extractLongValue(body, "progressMs");
        Serial.printf("[Music] Parsed track='%s' artist='%s' duration=%d progress=%d\n",
                      info.track.c_str(), info.artist.c_str(), info.durationMs, info.progressMs);
        float bpmVal = extractFloatValue(body, "bpm");
        if (bpmVal <= 0.0f)
        {
            // Preserve previous BPM when the same track is playing; otherwise leave 0
            if (musicInfo.hasData && info.track == musicInfo.track && musicInfo.bpm > 0.1f)
            {
                bpmVal = musicInfo.bpm;
            }
            else
            {
                bpmVal = 0.0f;
            }
        }
        info.bpm = bpmVal;
        info.lastUpdated = millis();
        info.hasData = (info.track.length() > 0);
        musicInfo = info;
    }

    bool connectToStation()
    {
        Serial.printf("Connecting to Wi-Fi SSID: %s\n", staSsid);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        if (!WiFi.config(localIp, gateway, subnet, primaryDns, secondaryDns))
        {
            Serial.println("Failed to configure static IP");
        }

        WiFi.begin(staSsid, staPassword);

        unsigned long startAttempt = millis();
        const unsigned long timeoutMs = 12000;
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeoutMs)
        {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED)
        {
            apFallback = false;
            Serial.printf("Connected! IP address: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }

        Serial.println("Failed to join Wi-Fi network, will fall back to AP mode");
        WiFi.disconnect(true);
        return false;
    }

    void startAccessPoint()
    {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSsid, apPassword);
        apFallback = true;
        delay(200);
        Serial.printf("Access Point started. Connect to %s (IP %s)\n", apSsid, WiFi.softAPIP().toString().c_str());
    }

public:
    WiFiManager(LEDController &controller) : server(80), ledController(controller) {}

    void attachStateHandler(StateHandler *handler)
    {
        stateHandler = handler;
        if (stateHandler)
        {
            partyHz = stateHandler->getPartyHz();
        }
    }

    void begin()
    {
        if (started)
        {
            return;
        }

        if (!SPIFFS.begin(true))
        {
            Serial.println("SPIFFS Mount Failed");
            return;
        }

        // 1. Register WiFi event handler FIRST
        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
                     { Serial.printf("[WiFi] Event: %d\n", event); });

        // Try to join existing network, otherwise start AP fallback
        if (!connectToStation())
        {
            startAccessPoint();
        }

        // Verify IP
        IPAddress currentIp = apFallback ? WiFi.softAPIP() : WiFi.localIP();
        if (currentIp == IPAddress(0, 0, 0, 0))
        {
            Serial.println("Network failed to provide IP - Rebooting");
            ESP.restart();
        }

        // mDNS setup after AP is confirmed working
        if (MDNS.begin("colorshadow"))
        {
            Serial.println("MDNS responder started");
            MDNS.addService("http", "tcp", 80);
        }

        
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

        server.on("/", HTTP_GET, std::bind(&WiFiManager::handleRoot, this, std::placeholders::_1));
        server.on("/iro.min.js", HTTP_GET, std::bind(&WiFiManager::handleIroMin, this, std::placeholders::_1));
        server.on("/iro_script.js", HTTP_GET, std::bind(&WiFiManager::handleIroScript, this, std::placeholders::_1));
        server.on("/lockStatus", HTTP_GET, std::bind(&WiFiManager::handleLockStatus, this, std::placeholders::_1));
        server.on("/unlock", HTTP_POST, std::bind(&WiFiManager::handleUnlock, this, std::placeholders::_1));
        server.on("/reset", HTTP_POST, std::bind(&WiFiManager::handleReset, this, std::placeholders::_1));

        // In WiFiManager.h constructor
        server.on("/postRGB", HTTP_POST, [this](AsyncWebServerRequest *request)
                  {
            // Check for all 3 parameters first
            if(!request->hasParam("r", true) || !request->hasParam("g", true) || !request->hasParam("b", true)) {
                request->send(400, "text/plain", "Missing parameters");
                return;
            }

            // Get and constrain values
            int r = request->getParam("r", true)->value().toInt();
            int g = request->getParam("g", true)->value().toInt();
            int b = request->getParam("b", true)->value().toInt();
            
            r = constrain(r, 0, 255);
            g = constrain(g, 0, 255);
            b = constrain(b, 0, 255);

            // Convert to 11-bit PWM range (0-2047)
            int pwm_r = map(r, 0, 255, 0, 2047);
            int pwm_g = map(g, 0, 255, 0, 2047);
            int pwm_b = map(b, 0, 255, 0, 2047);

            // Debug output
            Serial.printf("[WiFi] Received RGB: %d,%d,%d -> PWM: %d,%d,%d\n", 
                        r, g, b, pwm_r, pwm_g, pwm_b);

            // Set LEDs - account for physical pin swap
            ledController.setPWMDirectly(pwm_r, pwm_g, pwm_b);

            // Force remote mode so pots do not override
            if (stateHandler) {
                stateHandler->setMode(OperationMode::WIFI);
            }
    
        request->send(200, "text/plain", "OK"); });

        server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(404); });

        server.on("/api/status", HTTP_GET, std::bind(&WiFiManager::sendStatus, this, std::placeholders::_1));
        server.on("/api/music", HTTP_GET, std::bind(&WiFiManager::sendMusicStatus, this, std::placeholders::_1));

        server.on("/api/mode", HTTP_POST, [this](AsyncWebServerRequest *request)
                  {
            if (!stateHandler || !request->hasParam("mode", true)) {
                request->send(400, "application/json", "{\"error\":\"mode missing\"}");
                return;
            }

            OperationMode requestedMode;
            if (!stringToMode(request->getParam("mode", true)->value(), requestedMode)) {
                request->send(400, "application/json", "{\"error\":\"unknown mode\"}");
                return;
            }

            stateHandler->setMode(requestedMode);

            if (requestedMode == OperationMode::OFF) {
                ledController.setPWMDirectly(0, 0, 0);
            }

            request->send(200, "application/json", "{\"ok\":true}"); });

        server.on("/api/scene", HTTP_POST, [this](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("scene", true)) {
                request->send(400, "application/json", "{\"error\":\"scene missing\"}");
                return;
            }
            applyScene(request->getParam("scene", true)->value());
            request->send(200, "application/json", "{\"ok\":true}"); });

        server.on("/api/party", HTTP_POST, [this](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("hz", true)) {
                request->send(400, "application/json", "{\"error\":\"hz missing\"}");
                return;
            }
            float hz = request->getParam("hz", true)->value().toFloat();
            hz = constrain(hz, 0.05f, 5.0f);
            updatePartyHz(hz);
            if (stateHandler) {
                stateHandler->setMode(OperationMode::PARTY);
            }
            request->send(200, "application/json", "{\"ok\":true}"); });

        try
        {
            server.begin();
            Serial.println("Async HTTP server started successfully");
            started = true;
        }
        catch (...)
        {
            Serial.println("Failed to start server - attempting restart");
            delay(1000);
            ESP.restart();
        }
    }

    void update(OperationMode mode)
    {
        // Always poll so /api/music stays fresh even if mode isn't music
        pollMusicPlayback();

        if (mode == OperationMode::MUSIC)
        {
            if (musicInfo.bpm > 0.1f)
            {
                float hz = constrain(musicInfo.bpm / 60.0f, 0.05f, 5.0f);
                updatePartyHz(hz);
            }
        }
    }

    void stop()
    {
        server.end();
        delay(100);
        WiFi.softAPdisconnect(true);
        delay(100);
        Serial.println("WiFi and server stopped");
        started = false;
    }
};
