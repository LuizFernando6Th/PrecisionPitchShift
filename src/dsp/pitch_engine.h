// PrecisionPitchShift — streaming phase-vocoder pitch-shift engine.
//
// Design contract (see README § Limitações):
//   f_out = f_in * factor,  factor = Y/X          (global spectral scaling)
//   Fs_processing = Fs_host                        (no internal resampling;
//                                                   host-sized blocks only)
//   duration preserved (synthesis hop == analysis hop)
//   band limit = Nyquist = Fs/2                    (no fixed 20 kHz filter)
//   content above Nyquist after scaling is discarded with a smooth guard,
//   never folded back (alias_protection).
//   all internal processing in float64 (double).
//   stereo: identical mapping/window/hop on every channel, shared transient
//   decisions — no inter-channel delay, no panorama change.
//
// Quality modes:
//   HighPrecision : FFT sized for ~10 Hz resolution at the host rate
//                   (4096 @44.1/48k, 8192 @88.2/96k, 16384 @176.4/192k),
//                   hop = N/4.
//   Efficient     : half the FFT size (lower CPU/latency, more smear).
//
// Latency = fftSize samples (reported to the host via getLatencySamples).
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "gain_control.h"
#include "ring_buffer.h"
#include "phase_processor.h"
#include "spectral_processor.h"

namespace pps {

enum class QualityMode { HighPrecision = 0, Efficient = 1 };

struct EngineConfig {
    double sampleRate = 48000.0;
    int numChannels = 2;
    double factor = 1.0; // Y / X
    QualityMode quality = QualityMode::HighPrecision;
    double crestThreshold = 10.0; // time-domain crest: globally impulsive
                                  // frames anchor every bin (isolated clicks)
    bool phaseLock = true; // peak-rate lock (see phase_processor.h)
    std::size_t fftSizeOverride = 0;  // 0 = automatic per sample rate
    WindowType window = WindowType::Hann;
    std::size_t maxBlockSize = 8192; // host/CLI block ceiling; used for preallocation
#ifdef PPS_LEGACY_PHASE
    bool sharedRotation = false; // legacy path: independent phase propagation
                                 // per channel (comparison only, NOT production)
#else
    bool sharedRotation = true;  // production default: output phase =
                                 // reinterpolated analysis phase + ONE shared
                                 // rotation R[k] for all channels
#endif
};

std::size_t suggestedFftSize(double sampleRate, QualityMode q);

class PitchEngine {
public:
    PitchEngine();
    bool configure(const EngineConfig& cfg);
    void reset();
    void setFactor(double factor); // Y/X, > 0

    std::size_t latencySamples() const { return fftSize_; }
    std::size_t fftSize() const { return fftSize_; }
    std::size_t maxBlockSize() const { return maxBlockSize_; }
    int hopSize() const { return hop_; }
    double sampleRate() const { return sampleRate_; }
    int numChannels() const { return numChannels_; }
    double factor() const { return factor_; }

    // Streaming block processing. In/out are non-interleaved float64 arrays
    // sized [numChannels][numSamples]. Sample rate must equal the configured
    // host rate (the engine never resamples).
    void process(const double* const* in, double** out, int numSamples);

    long long dbgFrames() const { return dbgFrames_; }
    long long dbgTransient() const { return dbgTransient_; }
    long long dbgAnchoredBins() const { return dbgAnchoredBins_; }
    long long dbgTotalBins() const { return dbgTotalBins_; }

private:
    struct Channel {
        RingBuffer inFifo;            // pending input samples
        RingBuffer outFifo;           // ready output samples
        std::vector<double> ola;      // overlap-add accumulator (fftSize)
        PhaseState phase;
        // Per-frame scratch:
        std::vector<double> frame, windowed, synthFrame;
        std::vector<std::complex<double>> specA, specS;
        std::vector<double> magA, phaA, trueF, magS, phaS;
        std::vector<double> scratch;  // cos table for Dirichlet mapping
        std::vector<double> scratch2; // sin table for Dirichlet mapping
        std::vector<std::complex<double>> work;
    };

    void processBlock(const double* const* in, double** out, int numSamples);

    EngineConfig cfg_{};
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    double factor_ = 1.0;
    std::size_t maxBlockSize_ = 8192;
    std::size_t fftSize_ = 0;
    int hop_ = 0;
    double olaGain_ = 1.0;
    std::vector<double> window_;
    std::vector<double> guard_;
    std::vector<Channel> channels_;
    SharedRotation shared_;
    std::vector<double> magRef_, trueRef_, outMagRef_;
    bool rotMode_ = false;
    long long consumed_ = 0; // total input samples appended (all channels alike)
    long long drained_ = 0;  // total output samples released
    // Diagnostics (CLI --count-transients): frames processed, frames with
    // any anchored bin, and total anchored bins (vs total bins processed).
    long long dbgFrames_ = 0;
    long long dbgTransient_ = 0;
    long long dbgAnchoredBins_ = 0;
    long long dbgTotalBins_ = 0;
    long long dbgGuideSwitches_ = 0;
    long long dbgGuideAssignments_ = 0;
public:
    long long dbgGuideSwitches() const { return dbgGuideSwitches_; }
    long long dbgGuideAssignments() const { return dbgGuideAssignments_; }
private:
};

} // namespace pps
