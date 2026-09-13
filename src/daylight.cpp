/* daylight.cpp
*
*  MIT License
*
*  Copyright (c) 2026 awawa-dev
*
*  Project homesite: https://github.com/awawa-dev/Hyperk
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.

*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE.
*/

#include <Arduino.h>
#include "daylight.h"
#include "main.h"

// The feature needs the async web server (second instance on port 8080) and the
// configTime() NTP API which ESP8266 and ESP32 share. Other targets get inert stubs.
#if defined(HYPERK_DAYLIGHT) && defined(USE_ASYNC_WEBSERWER) && (defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32))
    #define DAYLIGHT_ACTIVE 1
#endif

#ifdef DAYLIGHT_ACTIVE

#include <time.h>
#include <cmath>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "sun_calc.h"
#include "daylight_page.h"

#ifndef APP_VERSION
    #define APP_VERSION "unknown"
#endif
#ifndef HYPERK_DAYLIGHT_BUILD
    #define HYPERK_DAYLIGHT_BUILD "dev"
#endif

namespace {
    constexpr uint16_t DAYLIGHT_PORT = 8080;
    constexpr const char* CONFIG_FILE = "/daylight.json";
    constexpr const char* CONFIG_TMP  = "/daylight.tmp";
    constexpr int64_t  MIN_VALID_TIME = 1700000000LL;   // 2023-11-14, anything below means "not synced yet"
    constexpr uint32_t REFRESH_MS = 5000;

    enum OverrideMode : uint8_t { OVERRIDE_AUTO = 0, OVERRIDE_ALLOW = 1, OVERRIDE_BLOCK = 2 };

    struct Config {
        bool    enabled = false;
        double  lat = NAN;
        double  lon = NAN;
        float   altitude = -6.0f;          // civil twilight by default
        int16_t riseOffsetMin = 0;
        int16_t setOffsetMin = 0;
        uint8_t overrideMode = OVERRIDE_AUTO;
        char    label[64] = "";
        char    ntp[48] = "pool.ntp.org";
    };

    struct Status {
        bool            timeSynced = false;
        int64_t         now = 0;
        bool            hasResult = false;
        SunCalc::Result result{};
        bool            blocked = false;
        const char*     reason = "disabled";
    };

    Config cfg;
    Config pendingCfg;
    volatile bool hasPending = false;
    volatile bool blocked = false;
    uint32_t lastRefresh = 0;
    bool firstRefresh = true;
    bool lastSynced = false;
    bool lastBlocked = false;
    bool loggedNoTime = false;

    AsyncWebServer* server = nullptr;

    #if defined(ARDUINO_ARCH_ESP32)
        portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
        inline void lockCfg()   { portENTER_CRITICAL(&cfgMux); }
        inline void unlockCfg() { portEXIT_CRITICAL(&cfgMux); }
    #else
        inline void lockCfg()   {}
        inline void unlockCfg() {}
    #endif

    Config snapshotConfig() {
        lockCfg();
        Config copy = cfg;
        unlockCfg();
        return copy;
    }

    // ------------------------------------------------------------------
    // Config <-> JSON
    // ------------------------------------------------------------------

    bool hasLocation(const Config& c) {
        return !std::isnan(c.lat) && !std::isnan(c.lon);
    }

    const char* overrideName(uint8_t mode) {
        switch (mode) {
            case OVERRIDE_ALLOW: return "allow";
            case OVERRIDE_BLOCK: return "block";
            default:             return "auto";
        }
    }

    /**
     * @brief Copy validated fields from JSON into dst. Unknown keys are ignored,
     * missing keys keep their current value. Returns false (and touches nothing
     * else) as soon as a value is out of range.
     */
    bool applyJson(JsonVariantConst src, Config& dst, const char*& error) {
        error = nullptr;
        if (!src.is<JsonObjectConst>()) { error = "expected object"; return false; }
        JsonObjectConst o = src.as<JsonObjectConst>();

        Config tmp = dst;

        if (!o["enabled"].isNull()) tmp.enabled = o["enabled"].as<bool>();

        if (!o["lat"].isNull()) {
            double v = o["lat"].as<double>();
            if (std::isnan(v) || v < -90.0 || v > 90.0) { error = "lat out of range"; return false; }
            tmp.lat = v;
        }
        if (!o["lon"].isNull()) {
            double v = o["lon"].as<double>();
            if (std::isnan(v) || v < -180.0 || v > 180.0) { error = "lon out of range"; return false; }
            tmp.lon = v;
        }
        if (o["clearLocation"].as<bool>()) { tmp.lat = NAN; tmp.lon = NAN; }

        if (!o["altitude"].isNull()) {
            float v = o["altitude"].as<float>();
            if (std::isnan(v) || v < -18.0f || v > 0.0f) { error = "altitude out of range"; return false; }
            tmp.altitude = v;
        }
        if (!o["riseOffset"].isNull()) {
            int v = o["riseOffset"].as<int>();
            if (v < -360 || v > 360) { error = "riseOffset out of range"; return false; }
            tmp.riseOffsetMin = (int16_t)v;
        }
        if (!o["setOffset"].isNull()) {
            int v = o["setOffset"].as<int>();
            if (v < -360 || v > 360) { error = "setOffset out of range"; return false; }
            tmp.setOffsetMin = (int16_t)v;
        }
        if (!o["override"].isNull()) {
            const char* s = o["override"].as<const char*>();
            if (s == nullptr) { error = "override must be a string"; return false; }
            if (!strcmp(s, "auto"))       tmp.overrideMode = OVERRIDE_AUTO;
            else if (!strcmp(s, "allow")) tmp.overrideMode = OVERRIDE_ALLOW;
            else if (!strcmp(s, "block")) tmp.overrideMode = OVERRIDE_BLOCK;
            else { error = "override must be auto|allow|block"; return false; }
        }
        if (!o["label"].isNull()) {
            const char* s = o["label"].as<const char*>();
            strlcpy(tmp.label, s ? s : "", sizeof(tmp.label));
        }
        if (!o["ntp"].isNull()) {
            const char* s = o["ntp"].as<const char*>();
            if (s == nullptr || strlen(s) == 0) strlcpy(tmp.ntp, "pool.ntp.org", sizeof(tmp.ntp));
            else strlcpy(tmp.ntp, s, sizeof(tmp.ntp));
        }

        dst = tmp;
        return true;
    }

    void configToJson(const Config& c, JsonObject o) {
        o["enabled"] = c.enabled;
        if (hasLocation(c)) {
            o["lat"] = c.lat;
            o["lon"] = c.lon;
        } else {
            o["lat"] = nullptr;
            o["lon"] = nullptr;
        }
        o["label"] = c.label;
        o["altitude"] = c.altitude;
        o["riseOffset"] = c.riseOffsetMin;
        o["setOffset"] = c.setOffsetMin;
        o["override"] = overrideName(c.overrideMode);
        o["ntp"] = c.ntp;
    }

    bool loadConfig() {
        if (!LittleFS.exists(CONFIG_FILE)) {
            Log::SERIAL_LOG("Daylight: no config file, feature disabled");
            return false;
        }
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (!f) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) {
            Log::SERIAL_LOG("Daylight: config parse error: ", err.c_str());
            return false;
        }
        const char* error = nullptr;
        Config loaded;
        if (!applyJson(doc.as<JsonVariantConst>(), loaded, error)) {
            Log::SERIAL_LOG("Daylight: config invalid: ", error);
            return false;
        }
        lockCfg();
        cfg = loaded;
        unlockCfg();
        Log::SERIAL_LOG("Daylight: config loaded, enabled=", cfg.enabled, " lat=", cfg.lat, " lon=", cfg.lon);
        return true;
    }

    bool saveConfig(const Config& c) {
        JsonDocument doc;
        configToJson(c, doc.to<JsonObject>());

        File f = LittleFS.open(CONFIG_TMP, "w");
        if (!f) {
            Log::SERIAL_LOG("Daylight: cannot open config for writing");
            return false;
        }
        serializeJson(doc, f);
        f.close();

        LittleFS.remove(CONFIG_FILE);
        if (!LittleFS.rename(CONFIG_TMP, CONFIG_FILE)) {
            Log::SERIAL_LOG("Daylight: rename failed");
            return false;
        }
        Log::SERIAL_LOG("Daylight: config saved");
        return true;
    }

    // ------------------------------------------------------------------
    // Time & evaluation
    // ------------------------------------------------------------------

    void startNtp(const char* server1) {
        configTime(0, 0, server1, "time.nist.gov");
        Log::SERIAL_LOG("Daylight: NTP started with ", server1);
    }

    bool timeSynced(int64_t& now) {
        now = (int64_t)time(nullptr);
        return now > MIN_VALID_TIME;
    }

    void evaluate(int64_t at, bool synced, const Config& c, Status& st) {
        st.now = at;
        st.timeSynced = synced;
        st.hasResult = false;
        st.blocked = false;

        if (hasLocation(c)) {
            SunCalc::Params p{c.lat, c.lon, (double)c.altitude,
                              (int32_t)c.riseOffsetMin * 60, (int32_t)c.setOffsetMin * 60};
            if (synced) {
                st.result = SunCalc::evaluate(at, p);
                st.hasResult = true;
            }
        }

        if (!c.enabled) { st.reason = "disabled"; return; }
        if (c.overrideMode == OVERRIDE_BLOCK) { st.blocked = true; st.reason = "override"; return; }
        if (c.overrideMode == OVERRIDE_ALLOW) { st.reason = "override"; return; }
        if (!hasLocation(c)) { st.reason = "noLocation"; return; }
        if (!synced) { st.reason = "noTime"; return; }

        st.blocked = st.result.isDay;
        st.reason = st.blocked ? "day" : "night";
    }

    const char* stateName(const Status& st) {
        if (!st.hasResult) return "unknown";
        switch (st.result.kindToday) {
            case SunCalc::DayKind::PolarDay:   return "polarDay";
            case SunCalc::DayKind::PolarNight: return "polarNight";
            default: return st.result.isDay ? "day" : "night";
        }
    }

    void refresh(bool force) {
        const uint32_t nowMs = millis();
        if (!force && !firstRefresh && (nowMs - lastRefresh) < REFRESH_MS) return;
        lastRefresh = nowMs;
        firstRefresh = false;

        int64_t now;
        const bool synced = timeSynced(now);
        Config c = snapshotConfig();

        Status st;
        evaluate(now, synced, c, st);

        if (synced && !lastSynced) {
            Log::SERIAL_LOG("Daylight: time synchronized, epoch=", (uint32_t)now);
        }
        if (!synced && c.enabled && hasLocation(c) && !loggedNoTime) {
            loggedNoTime = true;
            Log::SERIAL_LOG("Daylight: waiting for NTP time, LEDs stay enabled meanwhile");
        }
        lastSynced = synced;

        blocked = st.blocked;
        if (st.blocked != lastBlocked || force) {
            lastBlocked = st.blocked;
            Log::SERIAL_LOG("Daylight: stream ", st.blocked ? "BLOCKED" : "allowed", " (", st.reason, ")");
        }
    }

    // ------------------------------------------------------------------
    // HTTP
    // ------------------------------------------------------------------

    bool paramDouble(AsyncWebServerRequest* request, const char* name, double& out) {
        if (!request->hasParam(name)) return false;
        const String& v = request->getParam(name)->value();
        if (v.length() == 0) return false;
        out = strtod(v.c_str(), nullptr);
        return true;
    }

    void handleStatus(AsyncWebServerRequest* request) {
        Config c = snapshotConfig();
        bool sim = false;

        double d;
        if (paramDouble(request, "lat", d) && d >= -90 && d <= 90)      { c.lat = d; sim = true; }
        if (paramDouble(request, "lon", d) && d >= -180 && d <= 180)    { c.lon = d; sim = true; }
        if (paramDouble(request, "altitude", d) && d >= -18 && d <= 0)  { c.altitude = (float)d; sim = true; }
        if (paramDouble(request, "riseOffset", d) && d >= -360 && d <= 360) { c.riseOffsetMin = (int16_t)d; sim = true; }
        if (paramDouble(request, "setOffset", d) && d >= -360 && d <= 360)  { c.setOffsetMin = (int16_t)d; sim = true; }

        int64_t now;
        bool synced = timeSynced(now);
        if (paramDouble(request, "at", d) && d > 0) {
            now = (int64_t)d;
            synced = true;
            sim = true;
        }
        if (sim) {
            // a simulation shows what the automatic rule would do
            c.enabled = true;
            c.overrideMode = OVERRIDE_AUTO;
        }

        Status st;
        evaluate(now, synced, c, st);

        AsyncJsonResponse* response = new AsyncJsonResponse();
        JsonObject root = response->getRoot().to<JsonObject>();
        configToJson(c, root["config"].to<JsonObject>());
        root["timeSynced"] = st.timeSynced;
        root["now"] = (int64_t)st.now;
        root["state"] = stateName(st);
        root["blocked"] = sim ? st.blocked : (bool)blocked;
        root["reason"] = st.reason;
        root["sunrise"] = st.hasResult ? st.result.nextRise : (int64_t)0;
        root["sunset"] = st.hasResult ? st.result.nextSet : (int64_t)0;
        root["nextChange"] = st.hasResult ? st.result.nextChange : (int64_t)0;
        root["sim"] = sim;
        root["fw"] = APP_VERSION;
        root["build"] = HYPERK_DAYLIGHT_BUILD;
        root["uptime"] = (uint32_t)(millis() / 1000);
        root["freeHeap"] = (uint32_t)ESP.getFreeHeap();
        response->addHeader("Cache-Control", "no-store");
        response->setLength();
        request->send(response);
    }

    void handleConfigPost(AsyncWebServerRequest* request, JsonVariant& json) {
        Config c = snapshotConfig();
        const char* error = nullptr;
        if (!applyJson(json.as<JsonVariantConst>(), c, error)) {
            String body = "{\"ok\":false,\"error\":\"";
            body += error ? error : "invalid";
            body += "\"}";
            request->send(400, "application/json", body);
            return;
        }
        lockCfg();
        pendingCfg = c;
        hasPending = true;
        unlockCfg();
        request->send(200, "application/json", "{\"ok\":true}");
    }

    void setupWebServer() {
        server = new AsyncWebServer(DAYLIGHT_PORT);

        server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response = request->beginResponse(200, "text/html",
                (const uint8_t*)DAYLIGHT_HTML, sizeof(DAYLIGHT_HTML) - 1);
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        });

        server->on("/api/daylight", HTTP_GET, handleStatus);

        AsyncCallbackJsonWebHandler* post = new AsyncCallbackJsonWebHandler("/api/daylight", handleConfigPost);
        post->setMethod(HTTP_POST);
        post->setMaxContentLength(1024);
        server->addHandler(post);

        server->onNotFound([](AsyncWebServerRequest* request) {
            request->send(404, "text/plain", "Not found");
        });

        server->begin();
        Log::SERIAL_LOG("Daylight UI started on port ", DAYLIGHT_PORT);
    }
}

void Daylight::begin() {
    loadConfig();
    startNtp(cfg.ntp);
    setupWebServer();
    refresh(true);
}

void Daylight::loop() {
    if (hasPending) {
        lockCfg();
        Config c = pendingCfg;
        hasPending = false;
        const bool ntpChanged = strcmp(c.ntp, cfg.ntp) != 0;
        cfg = c;
        unlockCfg();

        saveConfig(c);
        if (ntpChanged) startNtp(c.ntp);
        loggedNoTime = false;
        refresh(true);
    }
    refresh(false);
}

bool Daylight::isStreamBlocked() {
    return blocked;
}

void Daylight::drainUdp(WiFiUDP& udp) {
    for (int i = 0; i < 32 && udp.parsePacket() > 0; ++i) {
        udp.flush();
    }
}

#else   // !DAYLIGHT_ACTIVE — inert stubs keep the rest of the firmware unchanged

void Daylight::begin() {}
void Daylight::loop() {}
bool Daylight::isStreamBlocked() { return false; }
void Daylight::drainUdp(WiFiUDP&) {}

#endif
