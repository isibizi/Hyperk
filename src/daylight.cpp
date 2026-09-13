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
#include <cstdlib>
#if __has_include(<stdlib_noniso.h>)
    #include <stdlib_noniso.h>   // dtostrf
#endif
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#if defined(ARDUINO_ARCH_ESP8266)
    #include <Updater.h>
#else
    #include <Update.h>
#endif
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

    struct DaylightConfig {
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

    DaylightConfig cfg;
    DaylightConfig pendingCfg;
    volatile bool hasPending = false;
    volatile bool blocked = false;
    uint32_t lastRefresh = 0;
    bool firstRefresh = true;
    bool lastSynced = false;
    bool lastBlocked = false;
    bool loggedNoTime = false;

    // Written by loop(), read by the async network task. Two buffers with an index
    // swap keep a request from ever reading a half written answer.
    char statusBuf[2][768] = {
        "{\"reason\":\"starting\",\"state\":\"unknown\",\"blocked\":false,\"timeSynced\":false}",
        "{\"reason\":\"starting\",\"state\":\"unknown\",\"blocked\":false,\"timeSynced\":false}"
    };
    volatile uint8_t statusIdx = 0;
    void buildStatusJson(char* body, size_t bodySize, const DaylightConfig& c, const Status& st, bool sim, bool gateBlocked);

    volatile bool updateFailed = false;
    volatile bool updateStarted = false;
    volatile bool rebootRequested = false;

    AsyncWebServer* server = nullptr;

    #if defined(ARDUINO_ARCH_ESP32)
        portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
        inline void lockCfg()   { portENTER_CRITICAL(&cfgMux); }
        inline void unlockCfg() { portEXIT_CRITICAL(&cfgMux); }
    #else
        inline void lockCfg()   {}
        inline void unlockCfg() {}
    #endif

    DaylightConfig snapshotConfig() {
        lockCfg();
        DaylightConfig copy = cfg;
        unlockCfg();
        return copy;
    }

    // ------------------------------------------------------------------
    // DaylightConfig <-> JSON
    // ------------------------------------------------------------------

    bool hasLocation(const DaylightConfig& c) {
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
    bool applyJson(JsonVariantConst src, DaylightConfig& dst, const char*& error) {
        error = nullptr;
        if (!src.is<JsonObjectConst>()) { error = "expected object"; return false; }
        JsonObjectConst o = src.as<JsonObjectConst>();

        DaylightConfig tmp = dst;

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

    void configToJson(const DaylightConfig& c, JsonObject o) {
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
        DaylightConfig loaded;
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

    bool saveConfig(const DaylightConfig& c) {
        JsonDocument doc;
        configToJson(c, doc.to<JsonObject>());

        LittleFS.remove(CONFIG_TMP);
        File f = LittleFS.open(CONFIG_TMP, "w");
        if (!f) {
            Log::SERIAL_LOG("Daylight: cannot open config for writing");
            return false;
        }
        const size_t expected = measureJson(doc);
        const size_t written = serializeJson(doc, f);
        f.close();

        if (written == 0 || written != expected) {
            // out of space: keep the file that is already there
            Log::SERIAL_LOG("Daylight: writing config failed, keeping the previous one");
            LittleFS.remove(CONFIG_TMP);
            return false;
        }

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

    void evaluate(int64_t at, bool synced, const DaylightConfig& c, Status& st) {
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
        DaylightConfig c = snapshotConfig();

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
        const uint8_t back = statusIdx ^ 1;
        buildStatusJson(statusBuf[back], sizeof(statusBuf[back]), c, st, false, st.blocked);
        statusIdx = back;

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

    /**
     * @brief Append a JSON string value, escaping what JSON requires.
     */
    void appendJsonString(char* out, size_t outSize, const char* value) {
        size_t n = strlen(out);
        if (n + 1 >= outSize) return;
        out[n++] = '"';
        for (const char* p = value; *p && n + 7 < outSize; ++p) {
            const unsigned char ch = (unsigned char)*p;
            if (ch == '"' || ch == '\\') { out[n++] = '\\'; out[n++] = (char)ch; }
            else if (ch == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
            else if (ch == '\r') { out[n++] = '\\'; out[n++] = 'r'; }
            else if (ch == '\t') { out[n++] = '\\'; out[n++] = 't'; }
            else if (ch < 0x20) { n += snprintf(out + n, outSize - n, "\\u%04x", ch); }
            else out[n++] = (char)ch;
        }
        if (n + 1 < outSize) out[n++] = '"';
        out[n] = 0;
    }

    /**
     * @brief Render the status as JSON with snprintf. No heap, no JSON document:
     * both are too expensive inside the async callback of an ESP8266.
     */
    void buildStatusJson(char* body, size_t bodySize, const DaylightConfig& c, const Status& st, bool sim, bool gateBlocked) {
        char latText[20] = "null", lonText[20] = "null", altText[16];
        if (hasLocation(c)) {
            dtostrf(c.lat, 0, 6, latText);
            dtostrf(c.lon, 0, 6, lonText);
        }
        dtostrf(c.altitude, 0, 3, altText);

        snprintf(body, bodySize,
            "{\"config\":{\"enabled\":%s,\"lat\":%s,\"lon\":%s,\"label\":",
            c.enabled ? "true" : "false", latText, lonText);
        appendJsonString(body, bodySize, c.label);

        size_t n = strlen(body);
        n += snprintf(body + n, bodySize - n,
            ",\"altitude\":%s,\"riseOffset\":%d,\"setOffset\":%d,\"override\":\"%s\",\"ntp\":",
            altText, (int)c.riseOffsetMin, (int)c.setOffsetMin, overrideName(c.overrideMode));
        appendJsonString(body, bodySize, c.ntp);

        n = strlen(body);
        snprintf(body + n, bodySize - n,
            "},\"timeSynced\":%s,\"now\":%lu,\"state\":\"%s\",\"blocked\":%s,\"reason\":\"%s\","
            "\"sunrise\":%lu,\"sunset\":%lu,\"nextChange\":%lu,\"sim\":%s,"
            "\"fw\":\"%s\",\"build\":\"%s\",\"uptime\":%lu,\"freeHeap\":%lu}",
            st.timeSynced ? "true" : "false",
            (unsigned long)(st.now > 0 ? st.now : 0),
            stateName(st),
            gateBlocked ? "true" : "false",
            st.reason,
            (unsigned long)(st.hasResult && st.result.nextRise > 0 ? st.result.nextRise : 0),
            (unsigned long)(st.hasResult && st.result.nextSet > 0 ? st.result.nextSet : 0),
            (unsigned long)(st.hasResult && st.result.nextChange > 0 ? st.result.nextChange : 0),
            sim ? "true" : "false",
            APP_VERSION, HYPERK_DAYLIGHT_BUILD,
            (unsigned long)(millis() / 1000),
            (unsigned long)ESP.getFreeHeap());
    }

    void handleStatus(AsyncWebServerRequest* request) {
        // Without query parameters answer from the buffer that loop() keeps up to
        // date. Nothing is computed inside the async callback in that case.
        if (request->params() == 0) {
            AsyncWebServerResponse* cached = request->beginResponse(200, "application/json", statusBuf[statusIdx]);
            cached->addHeader("Cache-Control", "no-store");
            request->send(cached);
            return;
        }

        DaylightConfig c = snapshotConfig();
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

        static char body[768];
        buildStatusJson(body, sizeof(body), c, st, true, st.blocked);

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", body);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    }

    /**
     * @brief Strict number parsing: "north" must not silently become 0.
     */
    bool toNumber(const String& v, double& out) {
        if (v.length() == 0) return false;
        char* end = nullptr;
        const double n = strtod(v.c_str(), &end);
        if (end == v.c_str()) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != '\0') return false;
        if (std::isnan(n) || std::isinf(n)) return false;
        out = n;
        return true;
    }

    bool paramText(AsyncWebServerRequest* request, const char* name, String& out) {
        if (!request->hasParam(name, true)) return false;
        out = request->getParam(name, true)->value();
        return true;
    }

    /**
     * @brief Apply posted form fields. Plain form encoding keeps the request path
     * free of JSON parsing, which matters on the ESP8266.
     */
    bool applyParams(AsyncWebServerRequest* request, DaylightConfig& dst, const char*& error) {
        error = nullptr;
        String v;

        if (paramText(request, "enabled", v)) dst.enabled = (v == "1" || v == "true");
        if (paramText(request, "clearLocation", v) && (v == "1" || v == "true")) {
            dst.lat = NAN;
            dst.lon = NAN;
        }
        if (paramText(request, "lat", v)) {
            double n;
            if (!toNumber(v, n) || n < -90.0 || n > 90.0) { error = "lat is not a valid number"; return false; }
            dst.lat = n;
        }
        if (paramText(request, "lon", v)) {
            double n;
            if (!toNumber(v, n) || n < -180.0 || n > 180.0) { error = "lon is not a valid number"; return false; }
            dst.lon = n;
        }
        if (paramText(request, "altitude", v)) {
            double n;
            if (!toNumber(v, n) || n < -18.0 || n > 0.0) { error = "altitude is not a valid number"; return false; }
            dst.altitude = (float)n;
        }
        if (paramText(request, "riseOffset", v)) {
            double n;
            if (!toNumber(v, n) || n < -360 || n > 360) { error = "riseOffset is not a valid number"; return false; }
            dst.riseOffsetMin = (int16_t)n;
        }
        if (paramText(request, "setOffset", v)) {
            double n;
            if (!toNumber(v, n) || n < -360 || n > 360) { error = "setOffset is not a valid number"; return false; }
            dst.setOffsetMin = (int16_t)n;
        }
        if (paramText(request, "override", v)) {
            if (v == "auto") dst.overrideMode = OVERRIDE_AUTO;
            else if (v == "allow") dst.overrideMode = OVERRIDE_ALLOW;
            else if (v == "block") dst.overrideMode = OVERRIDE_BLOCK;
            else { error = "override must be auto, allow or block"; return false; }
        }
        if (paramText(request, "label", v)) strlcpy(dst.label, v.c_str(), sizeof(dst.label));
        if (paramText(request, "ntp", v)) {
            strlcpy(dst.ntp, v.length() ? v.c_str() : "pool.ntp.org", sizeof(dst.ntp));
        }
        return true;
    }

    void handleConfigPost(AsyncWebServerRequest* request) {
        DaylightConfig c = snapshotConfig();
        const char* error = nullptr;
        if (!applyParams(request, c, error)) {
            static char err[96];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", error ? error : "invalid");
            request->send(400, "application/json", err);
            return;
        }
        lockCfg();
        pendingCfg = c;
        hasPending = true;
        unlockCfg();
        request->send(200, "application/json", "{\"ok\":true}");
    }

    /**
     * @brief Receive a firmware file and write it to the free sketch space.
     * The stock GUI on port 80 can only pull updates from the project's own
     * release server, so this offers a plain file upload instead.
     */
    /**
     * @brief Disarm the updater so a cancelled upload cannot block the next one.
     */
    void abortUpdate() {
        #if defined(ARDUINO_ARCH_ESP8266)
            if (Update.isRunning()) Update.end(false);
            Update.clearError();
        #else
            if (Update.isRunning()) Update.abort();
        #endif
    }

    void handleUpdateUpload(AsyncWebServerRequest*, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
            updateFailed = false;
            updateStarted = true;
            Log::SERIAL_LOG("Daylight: firmware upload started: ", filename.c_str());
            abortUpdate();   // a previous upload may have been cut off half way
            #if defined(ARDUINO_ARCH_ESP8266)
                Update.runAsync(true);
                const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSketchSpace)) updateFailed = true;
            #else
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) updateFailed = true;
            #endif
            if (updateFailed) Log::SERIAL_LOG("Daylight: Update.begin failed");
        }

        if (!updateFailed && len > 0 && Update.write(data, len) != len) {
            updateFailed = true;
            Log::SERIAL_LOG("Daylight: Update.write failed");
            abortUpdate();
        }

        if (final) {
            if (updateFailed) {
                abortUpdate();
            } else if (!Update.end(true)) {
                updateFailed = true;
                Log::SERIAL_LOG("Daylight: Update.end failed");
                abortUpdate();
            } else {
                Log::SERIAL_LOG("Daylight: firmware written, rebooting");
            }
        }
    }

    void handleUpdateResult(AsyncWebServerRequest* request) {
        const bool started = updateStarted;
        updateStarted = false;
        if (!started) {
            // no file in the request: never reboot on that
            request->send(400, "text/plain", "No firmware file was uploaded.");
            return;
        }

        const bool ok = !updateFailed && !Update.hasError();
        AsyncWebServerResponse* response = request->beginResponse(ok ? 200 : 400, "text/plain",
            ok ? "Firmware written. The device is rebooting." : "Firmware update failed. The device keeps the current firmware.");
        response->addHeader("Connection", "close");
        request->send(response);
        if (ok) rebootRequested = true;   // actual reboot is scheduled from loop()
    }

    void setupWebServer() {
        server = new AsyncWebServer(DAYLIGHT_PORT);

        server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response = request->beginResponse(200, "text/html",
                (const uint8_t*)DAYLIGHT_HTML, sizeof(DAYLIGHT_HTML) - 1);
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        });

        server->on("/api/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "ok");
        });

        server->on("/api/daylight", HTTP_GET, handleStatus);
        server->on("/api/daylight", HTTP_POST, handleConfigPost);

        server->on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);

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
    if (rebootRequested) {
        rebootRequested = false;
        Manager::scheduleReboot(1500);
    }

    if (hasPending) {
        lockCfg();
        DaylightConfig c = pendingCfg;
        hasPending = false;
        const bool ntpChanged = strcmp(c.ntp, cfg.ntp) != 0;
        cfg = c;
        unlockCfg();

        saveConfig(c);
        // cfg has static storage: lwIP keeps the pointer we hand to configTime()
        if (ntpChanged) startNtp(cfg.ntp);
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
        #if defined(ARDUINO_ARCH_ESP32)
            udp.clear();   // discard the received datagram (flush() is deprecated on ESP32)
        #else
            udp.flush();
        #endif
    }
}

#else   // !DAYLIGHT_ACTIVE — inert stubs keep the rest of the firmware unchanged

void Daylight::begin() {}
void Daylight::loop() {}
bool Daylight::isStreamBlocked() { return false; }
void Daylight::drainUdp(WiFiUDP&) {}

#endif
