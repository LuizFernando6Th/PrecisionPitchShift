// PrecisionPitchShift — measurement helpers shared by tests/tools.
// Float64 throughout. Frequency estimation: Hann-windowed FFT peak with
// parabolic interpolation over the log magnitude.
#pragma once

#include <cstddef>
#include <vector>

namespace pps::analysis {

void sine(std::vector<double>& out, double freq, double sampleRate,
          double amplitude = 0.5, double phase0 = 0.0);

void harmonicStack(std::vector<double>& out, double f0, int count,
                   double sampleRate, double amplitude = 0.3);

void impulse(std::vector<double>& out, std::size_t pos);

// Dominant frequency of `x` (Hz). Uses a Hann-windowed FFT of up to 65536
// samples centred in the buffer (skips `skipSamples` at both ends to avoid
// edge/latency artefacts).
double peakFrequency(const double* x, std::size_t n, double sampleRate,
                     std::size_t skipSamples = 0);

// Peak magnitude (linear) of the spectrum at exactly `freq` Hz neighbourhood.
double magnitudeNear(const double* x, std::size_t n, double sampleRate,
                     double freq, std::size_t skipSamples = 0);

// Peak absolute value in [start, end).
double peakAbs(const double* x, std::size_t start, std::size_t end);

// RMS in [start, end).
double rms(const double* x, std::size_t start, std::size_t end);

// Highest magnitude (dB relative to the global spectral peak) of any bin
// outside ±toleranceHz of the expected harmonic series k*f0 (k>=1) while
// below Nyquist. Measures spurious/alias content.
double worstSpuriousDb(const double* x, std::size_t n, double sampleRate,
                       double f0, double toleranceHz, std::size_t skipSamples = 0);

} // namespace pps::analysis
