// File: include/daylight.h
//
// Daylight gate: decides from the configured location and the current UTC time
// whether it is light outside. While it is light, the HyperHDR UDP stream is
// discarded so the backend sees "stream lost" and switches the LEDs off.
// Configuration UI + JSON API live on their own small web server (port 8080).

#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

namespace Daylight {
    /**
     * @brief Load configuration, start NTP and the configuration web server.
     * Call once from setup() after the network is up (not in AP mode).
     */
    void begin();

    /**
     * @brief Housekeeping: persist pending configuration, re-evaluate day/night.
     * Call from every loop() iteration.
     */
    void loop();

    /**
     * @brief True while the LED stream must be discarded (daylight or manual block).
     * Cheap: returns a cached flag.
     */
    bool isStreamBlocked();

    /**
     * @brief Discard datagrams waiting on the socket (bounded amount per call).
     */
    void drainUdp(WiFiUDP& udp);
};
