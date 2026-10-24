#include "libplatform/libplatform.h"

#include <chrono>
#include <cstdlib>

#include "Platform.h"

std::unique_ptr<v8::Platform> CPlatform::platform;
bool CPlatform::inited = false;

namespace {

// The spin loop reads the clock >100k times/s; any legitimate page stays
// orders of magnitude below.  Engage after 16 reads land within 8ms of
// real time (>=2000/s), disengage once the window stretches past 250ms
// (<64/s).  Hysteresis keeps mid-loop pauses (GC, scheduling) from
// dropping the scale while the loop is still hot.
constexpr double kEngageSpanMs = 8.0;
constexpr double kDisengageSpanMs = 250.0;
constexpr int kRateWindow = 16;

double SteadyMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(
        steady_clock::now().time_since_epoch()).count();
}

double WallMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(
        system_clock::now().time_since_epoch()).count();
}

struct ClockGate
{
    bool primed = false;
    bool engaged = false;
    double win[kRateWindow] = {};
    int widx = 0;
    // per-track integration state: [0]=wall, [1]=monotonic
    bool has_prev[2] = {false, false};
    double last_real[2] = {0.0, 0.0};
    double last_val[2] = {0.0, 0.0};
    uint64_t reads = 0;
    uint64_t engaged_reads = 0;
    int episodes = 0;
};

// Per-thread gate: JS clock reads all land on the isolate thread, and
// sparse background-thread reads never accumulate enough rate to engage.
thread_local ClockGate tls_gate;

void GateLog(const char *event, uint64_t reads, double span, int episodes)
{
    const char *path = getenv("LV8_CLOCK_STATS");
    if (!path)
        return;
    std::ofstream f(path, std::ios::app);
    if (!f)
        return;
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%.0f", WallMs());
    char tail[64];
    snprintf(tail, sizeof(tail), "%llu %.3f",
             static_cast<unsigned long long>(reads), span);
    f << stamp << " " << event << " reads=" << tail
      << " episodes=" << episodes << std::endl;
}

}  // namespace

CScaledClockPlatform::CScaledClockPlatform(double scale)
    : v8::platform::DefaultPlatform(
          1, v8::platform::IdleTaskSupport::kDisabled, {},
          v8::platform::PriorityMode::kDontApply),
      scale_(scale)
{
}

double CScaledClockPlatform::Scaled(bool monotonic)
{
    ClockGate &g = tls_gate;
    int track = monotonic ? 1 : 0;
    if (!g.primed)
    {
        double now = SteadyMs();
        for (int i = 0; i < kRateWindow; i++)
            g.win[i] = now;
        g.primed = true;
        GateLog("init", 0, 0.0, 0);
    }

    double real = SteadyMs();
    g.reads++;

    double oldest = g.win[g.widx];
    g.win[g.widx] = real;
    g.widx = (g.widx + 1) % kRateWindow;
    double span = real - oldest;

    if (!g.engaged)
    {
        // reads >= kRateWindow: the primed window would otherwise engage
        // on the very first reads (span vs the priming instant is tiny).
        if (g.reads >= kRateWindow && span <= kEngageSpanMs)
        {
            g.engaged = true;
            g.episodes++;
            GateLog("engage", g.reads, span, g.episodes);
        }
    }
    else if (span > kDisengageSpanMs)
    {
        g.engaged = false;
        GateLog("disengage", g.reads, span, g.episodes);
    }

    if (g.engaged)
        g.engaged_reads++;

    if (!g.has_prev[track])
    {
        g.last_real[track] = real;
        g.last_val[track] = monotonic
            ? DefaultPlatform::MonotonicallyIncreasingTime()
            : SystemClockTimeMillis();
        g.has_prev[track] = true;
        return g.last_val[track];
    }

    // Integral form: val += dt * f keeps both clocks monotone and
    // continuous across engage/disengage transitions, and the leap
    // accumulated inside a spin window persists afterwards, so every
    // later elapsed measurement matches the reference engine's timeline.
    double dt = real - g.last_real[track];
    if (dt < 0.0)
        dt = 0.0;
    double f = g.engaged ? scale_ : 1.0;
    g.last_real[track] = real;
    g.last_val[track] += dt * f;
    return g.last_val[track];
}

double CScaledClockPlatform::CurrentClockTimeMillis()
{
    return Scaled(false);
}

double CScaledClockPlatform::MonotonicallyIncreasingTime()
{
    return Scaled(true);
}

void CPlatform::Init()
{
    if(inited)
        return;

    v8::V8::InitializeICUDefaultLocation(argv.c_str(), GetICUDataFile());
    v8::V8::InitializeExternalStartupData(argv.c_str());

    double scale = 0.0;
    if (const char *s = getenv("LV8_CLOCK_SCALE"))
        scale = strtod(s, nullptr);

    // G4.152: opt-in via LV8_CLOCK_SCALE (e.g. 630.9 = measured lv8/reference
    // iteration-cost ratio); unset keeps the stock default platform so
    // every other consumer sees byte-identical time behavior.
    if (scale >= 1.5)
        platform = std::make_unique<CScaledClockPlatform>(scale);
    else
        platform = v8::platform::NewDefaultPlatform();

    v8::V8::InitializePlatform(platform.get());
    // Reference-engine stack parity (reference/V8 12.5): the direct-eval builtin
    // frame is part of Error stacks there, while V8 13.1 hides builtin
    // frames by default.  The challenge SDK fingerprints that bottom frame.
    // Must run before Initialize(): flags are frozen afterwards.
    v8::V8::SetFlagsFromString("--builtins-in-stack-traces");
    v8::V8::Initialize();

    inited = true;
}

const char * CPlatform::GetICUDataFile()
{
    // The returned pointer is consumed by
    // v8::V8::InitializeICUDefaultLocation(), which keeps it alive past the
    // call. The original upstream implementation returned the c_str() of a
    // function-local std::string, i.e. a dangling pointer: whether ICU data
    // loaded was pure stack luck. Store the resolved path in static storage
    // and always hand V8 a pointer that outlives the call.
    static std::string resolved_icu_data_path;

#if defined(_WIN32) || defined (_WIN64)
    boost::filesystem::path icu_data_path = getenv("APPDATA") ? getenv("APPDATA") : "";
#else
    boost::filesystem::path icu_data_path = getenv("HOME") ? getenv("HOME") : "";
#endif
    if (icu_data_user == nullptr)
        return nullptr;

    if (boost::filesystem::is_directory(icu_data_path)) {
        icu_data_path /= icu_data_user;

        resolved_icu_data_path = icu_data_path.string();

        std::ifstream ifile(resolved_icu_data_path.c_str());
        if (ifile.good())
            return resolved_icu_data_path.c_str();
    }

    if (icu_data_system != nullptr) {
#if defined(_WIN32) || defined (_WIN64)
        boost::filesystem::path icu_windows_data_path = getenv("PROGRAMDATA") ? getenv("PROGRAMDATA") : "";
        if (boost::filesystem::is_directory(icu_windows_data_path)) {
            icu_windows_data_path /= icu_data_system;

            resolved_icu_data_path = icu_windows_data_path.string();

            std::ifstream ifile(resolved_icu_data_path.c_str());
            if (ifile.good())
                return resolved_icu_data_path.c_str();
        }
#else
        std::ifstream ifile(icu_data_system);
        if (ifile.good())
            return icu_data_system;
#endif
    }

    return nullptr;
}
