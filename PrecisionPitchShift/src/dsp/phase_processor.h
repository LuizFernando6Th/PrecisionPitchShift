// PrecisionPitchShift — phase propagation for the phase-vocoder shifter.
//
// For each output bin k we track a synthesis phase advanced every hop by
//   synth[k] += trueFreqMapped[k] * hop,
// where trueFreqMapped is the instantaneous (unwrapped, interpolated)
// analysis frequency scaled by `factor`. Vertical coherence is improved by
// anchoring each new frame's non-peak bins to interpolated analysis phases
// during detected transients (attack preservation), and by letting strong
// spectral peaks dominate neighbouring bins (lightweight identity phase
// locking) — without any cross-channel divergence: L/R share the same
// mapping and transient decisions (see pitch_engine).
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pps {

constexpr double kTwoPi = 6.28318530717958647692;

// Wrap phase difference to [-pi, pi].
inline double wrapPi(double x) {
    while (x > 3.14159265358979323846) x -= kTwoPi;
    while (x < -3.14159265358979323846) x += kTwoPi;
    return x;
}

// Per-channel phase state for one STFT stream.
struct PhaseState {
    std::vector<double> prevAnalysis; // N/2+1
    std::vector<double> synthesis;    // N/2+1
    std::vector<double> prevMag;      // N/2+1 (for flux / transient detect)
    bool initialized = false;

    void resize(std::size_t numBins) {
        prevAnalysis.assign(numBins, 0.0);
        synthesis.assign(numBins, 0.0);
        prevMag.assign(numBins, 0.0);
        initialized = false;
    }
};

// Spectral flux (positive part, normalised). Used for transient detection.
double spectralFlux(const double* mag, const double* prevMag, std::size_t numBins);

// Advances synthesis phases for one frame.
//   anaMag/anaPhase : analysis spectrum (N/2+1)
//   trueFreq        : instantaneous frequency per analysis bin (rad/sample)
//   outMag/outPhase : synthesis spectrum to fill (N/2+1)
//   guard           : input-side alias guard (N/2+1)
//   factor          : pitch factor Y/X
//   hop             : hop size in samples
//   transient       : if true, anchor phases to analysis (attack preserving)
//   state           : persistent per-channel state (updated in place)
//   phaseLock       : Engine B — peak-rate locking: each output bin advances
//                     at the instantaneous rate of the nearest ANALYSIS peak
//                     instead of its own interpolated rate. Fixes wrapped
//                     (±frameRate) skirt estimates that otherwise blend into
//                     spurious side partials (~-19 dB measured without it).
//   scratchPhase    : scratch buffer (N/2+1), resized as needed
void propagateFrame(const double* anaMag, const double* anaPhase,
                    const double* trueFreq, double* outMag, double* outPhase,
                    const double* guard, std::size_t numBins, double factor,
                    int hop, bool transient, PhaseState& state, bool phaseLock,
                    std::vector<double>& scratchPhase);

} // namespace pps
