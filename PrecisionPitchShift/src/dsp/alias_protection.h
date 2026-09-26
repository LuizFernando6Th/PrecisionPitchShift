// PrecisionPitchShift — anti-alias / Nyquist-guard module.
//
// Physics: with pitch_factor = Y/X, an input component f_in moves to
// f_out = f_in * factor. If f_out > Fs/2 it cannot be represented and must
// NOT be folded back into the band (folding == aliasing). The input band
// that survives an upward shift is therefore f_in < (Fs/2)/factor.
//
// This module computes a per-bin gain applied on the ANALYSIS (input) side:
//   * factor <= 1 : no guard needed (everything maps downward, band shrinks).
//   * factor >  1 : unity up to a knee below cutoffJ=(N/2)/factor, then a
//                   raised-cosine fade to zero at cutoffJ.
//
// There is intentionally NO fixed 20 kHz filter: the only limit used is
// Nyquist = Fs/2, so 96/192 kHz content above 20 kHz is preserved whenever
// it maps to a representable output bin.
#pragma once

#include <cstddef>
#include <vector>

namespace pps {

// Fills `gains` (size numBins = N/2+1) with the input-side guard curve.
// fadeFraction: width of the raised-cosine transition as a fraction of
//               numBins (default 0.05). Set 0 for a hard edge (not advised).
void computeAliasGuard(std::vector<double>& gains, std::size_t fftSize,
                       double factor, double fadeFraction = 0.05);

// Returns the highest input frequency (Hz) that survives the shift.
// Returns Nyquist when factor <= 1.
double survivingInputBandwidth(double sampleRate, double factor);

} // namespace pps
