#pragma once

#include <fstream>
#include <boost/filesystem.hpp>
#include <v8.h>

// Subclassing v8::platform::DefaultPlatform pulls in V8's internal base
// headers, which expect the build-config define V8's own BUILD.gn passes.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "src/libplatform/default-platform.h"

#include "Config.h"


class CPlatform
{
private:
    static bool inited;
    static std::unique_ptr<v8::Platform> platform;

    constexpr static const char *icu_data_system = ICU_DATA_SYSTEM;
    constexpr static const char *icu_data_user = ICU_DATA_USER;

    const char *GetICUDataFile();

    std::string argv;
public:
    CPlatform() : argv(std::string()) {};
    CPlatform(std::string argv0) : argv(argv0) {};
    ~CPlatform() {};
    void Init();
};

// G4.152: rate-gated Date-clock acceleration.  The challenge payload
// benchmarks engine speed with a wall-time spin loop: it burns a fixed
// budget of Date-clock milliseconds (~884) and records how many loop
// iterations fit in it.  The reference engine spends the budget in ~159
// iterations; lv8 is ~630x faster per iteration and records 100k+, which
// lands in the WP plaintext and is rejected as implausible.  Scaling the
// wall clock by the measured ratio *inside the spin window only* makes the
// same tick budget exhaust after ~159 iterations; every Date-based elapsed
// measurement spanning the window stays identical to the reference's, and
// the mapping val += dt*scale is monotone and continuous, so no reader can
// observe a discontinuity.  The gate keys on the observable call rate:
// the loop reads the clock >100k times/s, everything else <100/s.
class CScaledClockPlatform : public v8::platform::DefaultPlatform
{
public:
    explicit CScaledClockPlatform(double scale);
    ~CScaledClockPlatform() override = default;

    double CurrentClockTimeMillis() override;
    double MonotonicallyIncreasingTime() override;

private:
    // G4.152b: the spin loop's budget burns on the monotonic clock
    // (performance.now), not the wall clock — empirically proven by the
    // wall-only scale leaving the iteration count untouched.  Both clocks
    // share one gate/engage state and integrate the same scaled dt.
    double Scaled(bool monotonic);

    double scale_;
};
