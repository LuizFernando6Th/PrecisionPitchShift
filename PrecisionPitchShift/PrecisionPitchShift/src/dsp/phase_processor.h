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
#include <algorithm>

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
    std::vector<double> prevMag;      // N/2+1 (per-bin attack decisions)
    std::vector<int> guidePeak;       // tracked analysis-peak bin per output bin
    std::vector<double> prevRate;     // previous synthesis angular rate (rad/sample)
    std::vector<char> prevRateValid;  // true once a previous rate exists
    std::vector<char> anchored;       // scratch: attack-anchored output bins
    std::vector<std::size_t> peaks;   // scratch: current-frame analysis peaks
    bool initialized = false;
    std::size_t anchoredBins = 0; // bins anchored in the last propagate call
    std::size_t guideSwitches = 0; // guide changes in the last frame
    std::size_t peakCount = 0; // number of valid entries in peaks[]

    void resize(std::size_t numBins) {
        prevAnalysis.assign(numBins, 0.0);
        synthesis.assign(numBins, 0.0);
        prevMag.assign(numBins, 0.0);
        guidePeak.assign(numBins, -1);
        prevRate.assign(numBins, 0.0);
        prevRateValid.assign(numBins, 0);
        anchored.assign(numBins, 0);
        peaks.assign((numBins + 1) / 2, 0);
        initialized = false;
        anchoredBins = 0;
        guideSwitches = 0;
        peakCount = 0;
    }

    void reset() {
        std::fill(prevAnalysis.begin(), prevAnalysis.end(), 0.0);
        std::fill(synthesis.begin(), synthesis.end(), 0.0);
        std::fill(prevMag.begin(), prevMag.end(), 0.0);
        std::fill(guidePeak.begin(), guidePeak.end(), -1);
        std::fill(prevRate.begin(), prevRate.end(), 0.0);
        std::fill(prevRateValid.begin(), prevRateValid.end(), 0);
        std::fill(anchored.begin(), anchored.end(), 0);
        peakCount = 0;
        initialized = false;
        anchoredBins = 0;
        guideSwitches = 0;
    }
};

// Spectral-mapping taps per side (Lanczos-8 Dirichlet kernel).
constexpr int kSpectralTaps = 8;

// Per-bin attack anchoring: anchor bin k only when it carries significant
// NEW energy — magnitude above an absolute floor (relative to frame peak)
// AND grown by kAttackRatio vs the previous frame — or when the frame is
// globally impulsive (crestFire). Sustained partials (stable mags) keep
// propagating even through drum hits: re-anchoring them all on every hit
// kicks their phases several times per second (audible chorus/phaser on
// guitar and voice with dense mixes — measured). Noise-floor bins (tiny
// magnitudes fluctuating wildly in ratio) are excluded by the floor.
constexpr double kAttackRatio = 2.5;
constexpr double kAbsFloorRel = 1e-4; // -80 dB relative to frame peak

// Advances synthesis phases for one frame.
//   anaMag/anaPhase : analysis spectrum (N/2+1)
//   trueFreq        : instantaneous frequency per analysis bin (rad/sample)
//   outMag/outPhase : synthesis spectrum to fill (N/2+1)
//   guard           : input-side alias guard (N/2+1)
//   fftSize         : STFT size (for the Dirichlet phase twist)
//   factor          : pitch factor Y/X
//   hop             : hop size in samples
//   crestFire       : whole frame is impulsive: anchor all bins (attacks win
//                     over sustain; masked content underneath is inaudible)
//   state           : persistent per-channel state (updated in place;
//                     prevMag drives per-bin attack decisions)
//   phaseLock       : peak-rate locking: each output bin advances at the
//                     instantaneous rate of the nearest ANALYSIS peak instead
//                     of its own interpolated rate (fixes wrapped skirt rates)
//   cosT/sinT       : scratch buffers (N/2+1), resized as needed
//
// Spectral mapping uses a finite windowed-sinc/Lanczos-8 interpolation with
// a causal phase twist. It is a practical bandlimited approximation, not a
// mathematically infinite-support exact Dirichlet reconstruction. Linear magnitude/phase interpolation is WRONG here: the spectrum
// oscillates (pi alternation between adjacent bins) faster than the bin
// spacing, so blending across bins synthesises meaningless mid-values and
// collapses partials by up to -14 dB at unlucky alignments (measured).
void propagateFrame(const double* anaMag, const double* anaPhase,
                    const double* trueFreq, double* outMag, double* outPhase,
                    const double* guard, std::size_t numBins, std::size_t fftSize,
                    double factor, int hop, bool crestFire, PhaseState& state,
                    bool phaseLock, std::vector<double>& cosT,
                    std::vector<double>& sinT);

} // namespace pps
