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
#include "phase_processor.h"
#include "spectral_processor.h"

namespace pps {

enum class QualityMode { HighPrecision = 0, Efficient = 1 };

struct EngineConfig {
    double sampleRate = 48000.0;
    int numChannels = 2;
    double factor = 1.0; // Y / X
    QualityMode quality = QualityMode::HighPrecision;
    double transientThreshold = 0.30; // absolute flux floor for attacks
    double transientRatio = 2.0;      // + must exceed this x median of past
                                      // BACKGROUND frames (history excludes
                                      // firing frames, so attacks never
                                      // desensitize their own tail)
    double crestThreshold = 10.0;     // time-domain crest threshold
    bool phaseLock = true; // peak-rate lock + nearest-phase anchor (Engine B).
                           // Default ON: measured -53 dB spurious (vs -19 dB
                           // OFF) on static stacks, no pitch-accuracy cost.
    std::size_t fftSizeOverride = 0;  // 0 = automatic per sample rate
    WindowType window = WindowType::Hann;
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
    int hopSize() const { return hop_; }
    double sampleRate() const { return sampleRate_; }
    int numChannels() const { return numChannels_; }
    double factor() const { return factor_; }

    // Streaming block processing. In/out are non-interleaved float64 arrays
    // sized [numChannels][numSamples]. Sample rate must equal the configured
    // host rate (the engine never resamples).
    void process(const double* const* in, double** out, int numSamples);

private:
    struct Channel {
        std::vector<double> inFifo;   // pending input samples
        std::vector<double> outFifo;  // ready output samples
        std::vector<double> ola;      // overlap-add accumulator (fftSize)
        PhaseState phase;
        // Per-frame scratch:
        std::vector<double> frame, windowed, synthFrame;
        std::vector<std::complex<double>> specA, specS;
        std::vector<double> magA, phaA, trueF, magS, phaS;
        std::vector<double> scratch;  // cos table for Dirichlet mapping
        std::vector<double> scratch2; // sin table for Dirichlet mapping
        std::vector<double> fluxHist; // past spectral-flux values (adaptive)
        std::vector<std::complex<double>> work;
        long long olaPos = 0; // samples consumed from ola into outFifo
    };

    void processFrame(Channel& ch, bool transient);
    bool takeFrame(Channel& ch);

    EngineConfig cfg_{};
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    double factor_ = 1.0;
    std::size_t fftSize_ = 0;
    int hop_ = 0;
    double olaGain_ = 1.0;
    std::vector<double> window_;
    std::vector<double> guard_;
    std::vector<Channel> channels_;
    long long consumed_ = 0; // total input samples appended (all channels alike)
    long long drained_ = 0;  // total output samples released
};

} // namespace pps
