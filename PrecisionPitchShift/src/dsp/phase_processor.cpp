// PrecisionPitchShift — phase propagation implementation.
#include "phase_processor.h"

#include <cmath>

namespace pps {

double spectralFlux(const double* mag, const double* prevMag, std::size_t numBins) {
    double pos = 0.0, tot = 0.0;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double d = mag[k] - prevMag[k];
        if (d > 0.0) pos += d * d;
        tot += mag[k] * mag[k];
    }
    if (tot <= 1e-18) return 0.0;
    return std::sqrt(pos / (tot + 1e-18));
}

void propagateFrame(const double* anaMag, const double* anaPhase,
                    const double* trueFreq, double* outMag, double* outPhase,
                    const double* guard, std::size_t numBins, double factor,
                    int hop, bool transient, PhaseState& state, bool phaseLock,
                    std::vector<double>& scratchPhase) {
    const double invFactor = 1.0 / factor;
    const double hd = static_cast<double>(hop);
    const double lastBin = static_cast<double>(numBins - 1);
    const bool wasInit = state.initialized;
    // When peak-rate locking, the main loop must NOT advance phases; the
    // lock block below advances each bin at its peak's rate instead.
    const bool rateLocked = phaseLock && wasInit && !transient && numBins >= 3;
    if (scratchPhase.size() != numBins) scratchPhase.assign(numBins, 0.0);

    for (std::size_t k = 0; k < numBins; ++k) {
        const double j = static_cast<double>(k) * invFactor; // source position
        double m = 0.0, ph = 0.0, tf = 0.0;
        if (j <= lastBin) {
            std::size_t j0 = static_cast<std::size_t>(j);
            std::size_t j1 = j0 + 1;
            if (j1 > numBins - 1) j1 = numBins - 1;
            const double frac = j - static_cast<double>(j0);
            const double g0 = guard[j0], g1 = guard[j1];
            m = (anaMag[j0] * g0) * (1.0 - frac) + (anaMag[j1] * g1) * frac;
            // Anchor phase: NEAREST analysis bin, never interpolated.
            // Adjacent bins of a windowed partial alternate by pi (sidelobe
            // sign); blending across that discontinuity synthesises a
            // meaningless mid-value at high-magnitude bins and collapses
            // partials (-14 dB measured at unlucky alignments). Magnitudes
            // (non-negative, no wrapping) keep linear interpolation.
            ph = (frac < 0.5) ? anaPhase[j0] : anaPhase[j1];
            scratchPhase[k] = ph;
            const double t0 = trueFreq[j0], t1 = trueFreq[j1];
            tf = (t0 * (1.0 - frac) + t1 * frac) * factor;
        }
        outMag[k] = m;
        if (!wasInit) {
            state.synthesis[k] = ph;
        } else if (transient) {
            // Preserve attack: re-anchor to the (shifted) analysis phase.
            state.synthesis[k] = ph;
        } else if (!rateLocked) {
            state.synthesis[k] += tf * hd;
        }
        outPhase[k] = state.synthesis[k];
    }

    // Peak-rate locking (Engine B): the instantaneous-frequency estimator only
    // resolves +-frameRate/2 around each bin centre, so bins in a partial's
    // skirt (>~2 bins from centre) wrap by +-frameRate (e.g. 393.1 instead of
    // 440 Hz at 48 kHz). Blending wrapped and correct rates via interpolation
    // synthesises spurious side partials (~-19 dB @ ~15 Hz off, measured).
    // Fix: every output bin advances at the rate of the nearest ANALYSIS peak
    // (peaks sit at partial centres, where estimates are unambiguous).
    // Magnitudes keep fractional interpolation; anchor phases use the
    // nearest bin (never blended across pi discontinuities).
    if (rateLocked) {
        // 1) analysis peaks: local maxima with >=6 dB contrast over the
        // surrounding +-3 bins (rejects sidelobe ripples, whose wrapped
        // rates would otherwise become guides for nearby bins).
        std::vector<std::size_t> peaks;
        peaks.reserve(64);
        for (std::size_t k = 1; k + 1 < numBins; ++k) {
            if (!(anaMag[k] >= anaMag[k - 1] && anaMag[k] > anaMag[k + 1])) continue;
            if (anaMag[k] <= 1e-9) continue;
            double vmin = anaMag[k];
            for (int d = 1; d <= 3; ++d) {
                if (k >= static_cast<std::size_t>(d)) vmin = std::min(vmin, anaMag[k - d]);
                if (k + d < numBins) vmin = std::min(vmin, anaMag[k + d]);
            }
            if (anaMag[k] >= 2.0 * vmin) peaks.push_back(k);
        }
        if (!peaks.empty()) {
            // 2) influence-region boundaries at midpoints between peaks.
            for (std::size_t k = 1; k + 1 < numBins; ++k) {
                const double j = static_cast<double>(k) * invFactor;
                if (j > lastBin) continue;
                // Nearest peak in source coordinates (linear scan; few peaks).
                std::size_t best = peaks[0];
                double bestD = std::fabs(j - static_cast<double>(best));
                for (std::size_t p = 1; p < peaks.size(); ++p) {
                    const double d = std::fabs(j - static_cast<double>(peaks[p]));
                    if (d < bestD) { bestD = d; best = peaks[p]; }
                }
                state.synthesis[k] += trueFreq[best] * factor * hd;
                outPhase[k] = state.synthesis[k];
            }
        }
    }
}

} // namespace pps
