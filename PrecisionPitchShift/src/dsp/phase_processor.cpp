// PrecisionPitchShift — phase propagation implementation.
#include "phase_processor.h"

#include <cmath>

namespace pps {

void propagateFrame(const double* anaMag, const double* anaPhase,
                    const double* trueFreq, double* outMag, double* outPhase,
                    const double* guard, std::size_t numBins, std::size_t fftSize,
                    double factor, int hop, bool crestFire, PhaseState& state,
                    bool phaseLock, std::vector<double>& cosT,
                    std::vector<double>& sinT) {
    const double invFactor = 1.0 / factor;
    const double hd = static_cast<double>(hop);
    const double lastBin = static_cast<double>(numBins - 1);
    const bool wasInit = state.initialized;
    // Peak-rate locking defers phase advance to the lock block below (which
    // advances each NON-ANCHORED bin at its peak's rate).
    const bool lockActive = phaseLock && wasInit && numBins >= 3;
    if (cosT.size() != numBins) cosT.assign(numBins, 1.0);
    if (sinT.size() != numBins) sinT.assign(numBins, 0.0);
    double framePeak = 0.0;
    for (std::size_t k = 0; k < numBins; ++k) {
        cosT[k] = std::cos(anaPhase[k]);
        sinT[k] = std::sin(anaPhase[k]);
        if (anaMag[k] > framePeak) framePeak = anaMag[k];
    }
    const double absFloor = framePeak * kAbsFloorRel;
    constexpr double kPi = 3.14159265358979323846;
    const double twistK = kPi * static_cast<double>(fftSize - 1) /
                          static_cast<double>(fftSize);
    std::vector<char> anchored(numBins, 0);
    std::size_t nAnchored = 0;

    for (std::size_t k = 0; k < numBins; ++k) {
        const double j = static_cast<double>(k) * invFactor; // source position
        double m = 0.0, ph = 0.0, tf = 0.0;
        std::size_t j0 = 0, j1 = 0;
        double frac = 0.0;
        const bool inRange = (j <= lastBin);
        if (inRange) {
            j0 = static_cast<std::size_t>(j);
            j1 = j0 + 1;
            if (j1 > numBins - 1) j1 = numBins - 1;
            frac = j - static_cast<double>(j0);
            // Dirichlet (windowed-sinc with causal phase twist) interpolation
            // of the COMPLEX spectrum: exact bandlimited resampling, immune
            // to the pi alternation between adjacent bins.
            double re = 0.0, im = 0.0;
            const long long jc = static_cast<long long>(j + 0.5); // nearest
            for (int dt = -kSpectralTaps; dt < kSpectralTaps; ++dt) {
                const long long jj = jc + dt;
                if (jj < 0 || jj >= static_cast<long long>(numBins)) continue;
                const double d = j - static_cast<double>(jj);
                double wmag;
                if (d > -1e-9 && d < 1e-9) {
                    wmag = 1.0;
                } else {
                    const double a = kPi * d;
                    wmag = (std::sin(a) / a) * (std::sin(a / kSpectralTaps) /
                                                (a / kSpectralTaps));
                }
                const double tw = twistK * d;
                const double ctw = std::cos(tw), stw = std::sin(tw);
                // weight (complex) times guarded analysis bin (complex)
                const std::size_t u = static_cast<std::size_t>(jj);
                const double ar = anaMag[u] * guard[u] * cosT[u];
                const double ai = anaMag[u] * guard[u] * sinT[u];
                const double wr = wmag * ctw, wi = wmag * stw;
                re += ar * wr - ai * wi;
                im += ar * wi + ai * wr;
            }
            m = std::sqrt(re * re + im * im);
            ph = std::atan2(im, re);
            if (!(ph == ph)) { ph = 0.0; m = 0.0; } // both-zero guard
            // Instantaneous rate still via (peak-locked or interpolated)
            // true frequencies; only used when NOT rate-locked.
            const double t0 = trueFreq[j0], t1 = trueFreq[j1];
            tf = (t0 * (1.0 - frac) + t1 * frac) * factor;
        }
        outMag[k] = m;
        // Per-bin selective attack anchoring: anchor only bins carrying
        // significant NEW energy — above the absolute floor and grown vs
        // the previous frame INTERPOLATED at the same source position —
        // or during globally impulsive frames. Sustained partials keep
        // propagating even under drum hits (no phase kicks: no chorus).
        bool anchor = !wasInit || crestFire;
        if (!anchor && wasInit && inRange && m > absFloor &&
            k < state.prevMag.size()) {
            const double pm = state.prevMag[j0] * (1.0 - frac) +
                              state.prevMag[j1] * frac;
            anchor = (pm <= 0.0) || (m > kAttackRatio * pm);
        }
        if (anchor) {
            state.synthesis[k] = ph;
            anchored[k] = 1;
            ++nAnchored;
        } else if (!lockActive) {
            state.synthesis[k] += tf * hd;
        }
        outPhase[k] = state.synthesis[k];
    }
    state.anchoredBins = nAnchored;

    // Peak-rate locking: the instantaneous-frequency estimator only
    // resolves +-frameRate/2 around each bin centre, so bins in a partial's
    // skirt (>~2 bins from centre) wrap by +-frameRate (e.g. 393.1 instead of
    // 440 Hz at 48 kHz). Every NON-ANCHORED output bin advances at the rate
    // of the nearest ANALYSIS peak (peaks sit at partial centres, where
    // estimates are unambiguous), while magnitudes/anchors come from
    // Dirichlet mapping.
    if (lockActive) {
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
            // Anchored bins (fresh attack energy) keep their anchor.
            for (std::size_t k = 1; k + 1 < numBins; ++k) {
                if (anchored[k]) continue;
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
