// Host-side sanity test for include/sun_calc.h (not part of the PlatformIO build).
// Build & run:  g++ -std=gnu++17 -I include test/sun_calc/sun_calc_test.cpp -o /tmp/suntest && /tmp/suntest
#include "sun_calc.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void expectNear(const char* what, double got, double want, double tol) {
    if (std::fabs(got - want) > tol) {
        std::printf("FAIL %s: got %.0f want %.0f (diff %.0f)\n", what, got, want, got - want);
        failures++;
    } else {
        std::printf("ok   %s: %.0f\n", what, got);
    }
}

static void expectBool(const char* what, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL %s: got %s want %s\n", what, got ? "true" : "false", want ? "true" : "false");
        failures++;
    } else {
        std::printf("ok   %s: %s\n", what, got ? "true" : "false");
    }
}

static void checkDay(const char* name, double lat, double lon, double alt, int64_t anyTimeOfDay, double rise, double set, double tol = 120) {
    const int32_t n = SunCalc::dayIndex(anyTimeOfDay);
    const auto ev = SunCalc::eventsForDay(n, lat, lon, alt);
    if (ev.kind != SunCalc::DayKind::Normal) {
        std::printf("FAIL %s: expected normal day\n", name);
        failures++;
        return;
    }
    char b[128];
    std::snprintf(b, sizeof(b), "%s rise", name); expectNear(b, ev.rise, rise, tol);
    std::snprintf(b, sizeof(b), "%s set", name);  expectNear(b, ev.set, set, tol);
}

int main() {
    const double MUC_LAT = 48.137154, MUC_LON = 11.576124;

    // Almanac reference values (UTC epochs), tolerance 2 minutes
    checkDay("Munich 2026-06-21", MUC_LAT, MUC_LON, -0.833, 1782043200, 1782011602, 1782069443);
    checkDay("Munich civil 2026-06-21", MUC_LAT, MUC_LON, -6.0, 1782043200, 1782009110, 1782071935);
    checkDay("Munich 2026-12-21", MUC_LAT, MUC_LON, -0.833, 1797854400, 1797836471, 1797866524);
    checkDay("Munich 2026-09-13", MUC_LAT, MUC_LON, -0.833, 1789300800, 1789274853, 1789320733);
    checkDay("London 2026-06-21", 51.5074, -0.1278, -0.833, 1782043200, 1782013381, 1782073283);
    checkDay("New York 2026-06-21", 40.7128, -74.0060, -0.833, 1782043200, 1782033897, 1782088234);
    checkDay("Sydney 2026-06-21", -33.8688, 151.2093, -0.833, 1782043200, 1781989193, 1782024820);

    // Polar cases
    {
        auto summer = SunCalc::eventsForDay(SunCalc::dayIndex(1782043200), 78.2232, 15.6267, -0.833);
        auto winter = SunCalc::eventsForDay(SunCalc::dayIndex(1797854400), 78.2232, 15.6267, -0.833);
        expectBool("Longyearbyen June = polar day", summer.kind == SunCalc::DayKind::PolarDay, true);
        expectBool("Longyearbyen December = polar night", winter.kind == SunCalc::DayKind::PolarNight, true);
    }

    // isDay evaluation (Munich, no offsets)
    SunCalc::Params muc{MUC_LAT, MUC_LON, -0.833, 0, 0};
    expectBool("Munich 02:00Z is night", SunCalc::evaluate(1782007200, muc).isDay, false);
    expectBool("Munich 03:30Z is day",   SunCalc::evaluate(1782012600, muc).isDay, true);
    expectBool("Munich 12:00Z is day",   SunCalc::evaluate(1782043200, muc).isDay, true);
    expectBool("Munich 19:00Z is day",   SunCalc::evaluate(1782068400, muc).isDay, true);
    expectBool("Munich 20:00Z is night", SunCalc::evaluate(1782072000, muc).isDay, false);

    SunCalc::Params mucLate{MUC_LAT, MUC_LON, -0.833, 0, 3600};
    expectBool("Munich 20:00Z with +60min sunset offset is day", SunCalc::evaluate(1782072000, mucLate).isDay, true);

    // next events across UTC midnight
    {
        auto r = SunCalc::evaluate(1782043200, muc);
        expectNear("Munich next set from noon", (double)r.nextSet, 1782069443, 120);
        expectNear("Munich next change from noon", (double)r.nextChange, 1782069443, 120);
        expectNear("Munich next rise from noon (tomorrow)", (double)r.nextRise, 1782011602 + 86400, 180);
    }
    SunCalc::Params nyc{40.7128, -74.0060, -0.833, 0, 0};
    expectBool("NYC 2026-06-22 00:15Z is day", SunCalc::evaluate(1782087300, nyc).isDay, true);
    expectBool("NYC 2026-06-22 01:15Z is night", SunCalc::evaluate(1782090900, nyc).isDay, false);
    SunCalc::Params syd{-33.8688, 151.2093, -0.833, 0, 0};
    expectBool("Sydney 2026-06-21 21:30Z is day", SunCalc::evaluate(1782077400, syd).isDay, true);

    // polar evaluation
    SunCalc::Params lyr{78.2232, 15.6267, -0.833, 0, 0};
    expectBool("Longyearbyen June midnight is day", SunCalc::evaluate(1782000000, lyr).isDay, true);
    expectBool("Longyearbyen December noon is night", SunCalc::evaluate(1797854400, lyr).isDay, false);

    // Polar day must stay continuous: sweep a whole arctic summer minute by minute
    // and make sure the light never flickers off.
    {
        SunCalc::Params lyrSweep{78.2232, 15.6267, -0.833, 0, 0};
        int64_t gaps = 0;
        for (int64_t t = 1779408000; t < 1782000000; t += 60) {   // 2026-05-22 .. 2026-06-21 UTC
            if (!SunCalc::evaluate(t, lyrSweep).isDay) gaps++;
        }
        expectNear("Longyearbyen polar day has no dark minutes", (double)gaps, 0.0, 0.0);
    }

    // A normal day must report a sensible next change in both directions
    {
        SunCalc::Params p{MUC_LAT, MUC_LON, -6.0, 0, 0};
        auto r = SunCalc::evaluate(1789300800, p);
        expectBool("Munich next change lies ahead", r.nextChange > 1789300800, true);
        expectBool("Munich next change within a day", r.nextChange < 1789300800 + 86400, true);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
