// File: include/sun_calc.h
//
// Pure C++ sunrise/sunset math (no Arduino dependencies) so it can be unit
// tested on a desktop compiler. Based on the well known "sunrise equation"
// (NOAA / Wikipedia). Accuracy is within a couple of minutes, which is more
// than enough to decide whether it is light or dark outside.
//
// All times are Unix seconds (UTC). No time zone handling is required:
// the device only needs UTC and the browser converts to local time.

#pragma once

#include <cmath>
#include <cstdint>

namespace SunCalc {

    enum class DayKind : uint8_t {
        Normal,      // regular sunrise and sunset on this day
        PolarDay,    // the sun never goes below the configured altitude
        PolarNight   // the sun never reaches the configured altitude
    };

    struct DayEvents {
        DayKind kind;
        double  transit;   // solar noon, unix seconds
        double  rise;      // unix seconds (only valid for Normal)
        double  set;       // unix seconds (only valid for Normal)
    };

    struct Params {
        double  lat;             // degrees, north positive
        double  lon;             // degrees, east positive
        double  altitudeDeg;     // sun altitude that separates "day" from "night" (-0.833 sunset, -6 civil, -12 nautical, -18 astronomical)
        int32_t riseOffsetSec;   // added to sunrise: > 0 means darkness ends later
        int32_t setOffsetSec;    // added to sunset:  > 0 means darkness starts later
    };

    struct Result {
        bool    isDay;        // true when the light interval contains the evaluated time
        DayKind kindToday;    // kind of the UTC day the evaluated time belongs to
        int64_t nextRise;     // next raw sunrise > t (0 if none within the window)
        int64_t nextSet;      // next raw sunset  > t (0 if none within the window)
        int64_t nextChange;   // next boundary (with offsets) > t, 0 if none within the window
    };

    namespace detail {
        constexpr double PI_D   = 3.14159265358979323846;
        constexpr double DEG    = PI_D / 180.0;
        constexpr double J2000  = 2451545.0;
        constexpr double JUNIX  = 2440587.5;   // Julian date of 1970-01-01T00:00Z

        inline double wrap360(double x) {
            x = std::fmod(x, 360.0);
            return (x < 0) ? x + 360.0 : x;
        }

        inline double julianToUnix(double j) {
            return (j - JUNIX) * 86400.0;
        }
    }

    /**
     * @brief Integer day index since 2000-01-01 (UTC) for a unix timestamp.
     */
    inline int32_t dayIndex(int64_t unixSeconds) {
        int64_t days = unixSeconds / 86400;
        if (unixSeconds < 0 && (unixSeconds % 86400) != 0) days -= 1;
        return static_cast<int32_t>(days - 10957);
    }

    /**
     * @brief Sunrise / sunset / transit for day index n at the given location.
     */
    inline DayEvents eventsForDay(int32_t n, double latDeg, double lonDeg, double altitudeDeg) {
        using namespace detail;

        if (latDeg >  89.9) latDeg =  89.9;
        if (latDeg < -89.9) latDeg = -89.9;

        const double jStar   = static_cast<double>(n) - lonDeg / 360.0;
        const double M       = wrap360(357.5291 + 0.98560028 * jStar);
        const double Mr      = M * DEG;
        const double C       = 1.9148 * std::sin(Mr) + 0.0200 * std::sin(2.0 * Mr) + 0.0003 * std::sin(3.0 * Mr);
        const double lambda  = wrap360(M + C + 180.0 + 102.9372);
        const double lr      = lambda * DEG;
        const double jTransit = J2000 + jStar + 0.0053 * std::sin(Mr) - 0.0069 * std::sin(2.0 * lr);

        const double sinDec  = std::sin(lr) * std::sin(23.4397 * DEG);
        const double cosDec  = std::sqrt(1.0 - sinDec * sinDec);
        const double latR    = latDeg * DEG;

        const double cosOmega = (std::sin(altitudeDeg * DEG) - std::sin(latR) * sinDec) / (std::cos(latR) * cosDec);

        DayEvents ev;
        ev.transit = julianToUnix(jTransit);

        if (cosOmega >= 1.0) {
            ev.kind = DayKind::PolarNight;
            ev.rise = ev.set = ev.transit;
        } else if (cosOmega <= -1.0) {
            ev.kind = DayKind::PolarDay;
            // Half a day either side of solar noon plus a margin, so that the spans of
            // consecutive polar days overlap. Without it the drift of solar noon leaves
            // short gaps in which the light would be reported as gone.
            ev.rise = ev.transit - 44100.0;
            ev.set  = ev.transit + 44100.0;
        } else {
            const double omegaDeg = std::acos(cosOmega) / DEG;
            ev.kind = DayKind::Normal;
            ev.rise = julianToUnix(jTransit - omegaDeg / 360.0);
            ev.set  = julianToUnix(jTransit + omegaDeg / 360.0);
        }
        return ev;
    }

    /**
     * @brief Decide whether time t is "day" (LEDs should stay off) and report the next events.
     *
     * Three consecutive solar days are inspected so that events which fall on the
     * previous/next UTC day (e.g. New York sunset after 00:00 UTC) are handled.
     */
    inline Result evaluate(int64_t t, const Params& p) {
        Result r;
        r.isDay = false;
        r.kindToday = DayKind::Normal;
        r.nextRise = r.nextSet = r.nextChange = 0;

        const int32_t n0 = dayIndex(t);

        auto consider = [&](int64_t candidate, int64_t& slot) {
            if (candidate > t && (slot == 0 || candidate < slot)) slot = candidate;
        };

        for (int32_t d = -1; d <= 1; ++d) {
            const DayEvents ev = eventsForDay(n0 + d, p.lat, p.lon, p.altitudeDeg);
            if (d == 0) r.kindToday = ev.kind;

            if (ev.kind == DayKind::PolarNight) {
                continue;
            }

            if (ev.kind == DayKind::PolarDay) {
                const int64_t from = static_cast<int64_t>(std::llround(ev.rise));
                const int64_t to   = static_cast<int64_t>(std::llround(ev.set));
                if (t >= from && t < to) r.isDay = true;
                continue;   // these bounds are artificial, never report them as events
            }

            const int64_t rise = static_cast<int64_t>(std::llround(ev.rise));
            const int64_t set  = static_cast<int64_t>(std::llround(ev.set));
            consider(rise, r.nextRise);
            consider(set,  r.nextSet);
            const int64_t lightStart = rise + p.riseOffsetSec;
            const int64_t lightEnd   = set  + p.setOffsetSec;

            if (t >= lightStart && t < lightEnd) r.isDay = true;
            consider(lightStart, r.nextChange);
            consider(lightEnd,   r.nextChange);
        }

        return r;
    }
}
