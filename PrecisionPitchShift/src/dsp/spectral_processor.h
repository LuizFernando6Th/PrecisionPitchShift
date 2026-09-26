// PrecisionPitchShift — spectral mapping helpers (STFT <-> shifted spectrum).
//
// Kept as free functions so unit tests can validate the mapping in isolation
// from the streaming engine.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pps {

// Window types for the STFT.
enum class WindowType { Hann = 0, BlackmanHarris = 1 };

// Hann window (periodic, suitable for 75% overlap OLA/WOLA).
void makeHannWindow(std::vector<double>& w, std::size_t n);

// 4-term Blackman-Harris (periodic, -92 dB sidelobes, wider main lobe).
void makeBlackmanHarrisWindow(std::vector<double>& w, std::size_t n);

void makeWindow(std::vector<double>& w, std::size_t n, WindowType t);

// Mean of w^2 (for WOLA normalisation with 75% overlap).
double meanSquare(const std::vector<double>& w);

// Instantaneous (true) frequency per analysis bin in rad/sample, from the
// unwrapped phase increment relative to the bin centre frequency.
//   anaPhase/prevAna : current/previous analysis phases (N/2+1)
//   trueFreq         : output (N/2+1)
//   fftSize, hop     : STFT parameters
void estimateTrueFrequencies(const double* anaPhase, const double* prevAna,
                             double* trueFreq, std::size_t numBins,
                             std::size_t fftSize, int hop);

// Time-domain crest factor (peak/RMS) of a windowed frame. Isolated impulses
// score ~sqrt(N) regardless of intra-window position; steady tones ~1.5-3.5.
// Used together with spectral flux so that an impulse is still detected while
// it slides through the window (flux misses the decay side).
double timeCrest(const double* frame, std::size_t n);

// (Identity phase locking lives in phase_processor.h — applyIdentityPhaseLock —
// where it can update the propagation state consistently.)

} // namespace pps
