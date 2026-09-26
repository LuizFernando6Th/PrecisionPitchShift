// PrecisionPitchShift — VST3 parameter definitions (shared political-neutral
// mapping between Processor and Controller).
//
// Primary UI: Source Frequency X (Hz) and Target Frequency Y (Hz).
// factor = Y / X is derived. Optional internal cents display only.
#pragma once

#include <cstdint>

namespace pps::params {

// VST3-compatible parameter ID type (Steinberg::Vst::ParamID is uint32).
using ParamID = std::uint32_t;

// Stable parameter IDs.
enum Id : ParamID {
    kSourceFreq = 100, // X in Hz
    kTargetFreq = 101, // Y in Hz
    kFactorInfo = 102, // read-only display of Y/X (best effort)
    kQuality = 103,    // 0 = High Precision, 1 = Efficient
    kAutoGain = 104,   // 0/1
    kCeilingDb = 105,  // dBFS ceiling, -6.0 .. -0.1
    kBypassId = 'bypa'
};

// Frequency parameter range (Hz). Wide enough for baroque pitch (415 Hz),
// Verdi 432 Hz, modern 440-444 Hz and experimental uses, with 0.01 Hz UI
// resolution.
constexpr double kFreqMin = 100.0;
constexpr double kFreqMax = 1000.0;
constexpr double kSourceDefault = 440.0;
constexpr double kTargetDefault = 444.0;

constexpr double kCeilingMinDb = -6.0;
constexpr double kCeilingMaxDb = -0.1;
constexpr double kCeilingDefaultDb = -1.0;

inline double freqFromNorm(double n) {
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return kFreqMin + n * (kFreqMax - kFreqMin);
}
inline double freqToNorm(double hz) {
    double n = (hz - kFreqMin) / (kFreqMax - kFreqMin);
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n;
}
inline double ceilingFromNorm(double n) {
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return kCeilingMinDb + n * (kCeilingMaxDb - kCeilingMinDb);
}
inline double ceilingToNorm(double db) {
    double n = (db - kCeilingMinDb) / (kCeilingMaxDb - kCeilingMinDb);
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n;
}
// Read-only factor display maps [0.5 .. 2.0] onto [0..1].
inline double factorToNorm(double f) {
    double n = (f - 0.5) / 1.5;
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n;
}
inline double factorFromNorm(double n) { return 0.5 + n * 1.5; }

inline double factor(double sourceHz, double targetHz) {
    return (sourceHz > 0.0) ? targetHz / sourceHz : 1.0;
}

} // namespace pps::params
