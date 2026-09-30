// PrecisionPitchShift — phase propagation implementation.
#include "phase_processor.h"

#include <cmath>
#include <cassert>
#include <algorithm>
#include <cstdlib>

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
    assert(cosT.size() == numBins && sinT.size() == numBins);
    assert(state.anchored.size() == numBins &&
           state.peaks.size() >= (numBins + 1) / 2);
    if (cosT.size() != numBins || sinT.size() != numBins ||
        state.anchored.size() != numBins ||
        state.peaks.size() < (numBins + 1) / 2) {
        return;
    }
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
    std::fill(state.anchored.begin(), state.anchored.end(), 0);
    state.peakCount = 0;
    state.guideSwitches = 0;
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
            state.anchored[k] = 1;
            ++nAnchored;
        } else if (!lockActive) {
            // Integrate the instantaneous-frequency trajectory with a
            // trapezoidal step instead of holding the current rate constant
            // for the whole hop. This suppresses hop-rate FM on vibrato while
            // preserving the same phase endpoint for a stationary tone.
            const double stepRate =
                (std::fabs(factor - 1.0) < 1e-12)
                    ? tf
                    : (state.prevRateValid[k]
                           ? 0.5 * (state.prevRate[k] + tf)
                           : tf);
            state.synthesis[k] += stepRate * hd;
        }
        if (!lockActive) {
            state.prevRate[k] = tf;
            state.prevRateValid[k] = 1;
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
        // 1) Analysis peaks: local maxima with >=6 dB contrast over the
        // surrounding +-3 bins (rejects sidelobe ripples).
        for (std::size_t k = 1; k + 1 < numBins; ++k) {
            if (!(anaMag[k] >= anaMag[k - 1] && anaMag[k] > anaMag[k + 1])) continue;
            if (anaMag[k] <= 1e-9) continue;
            double vmin = anaMag[k];
            for (int d = 1; d <= 3; ++d) {
                if (k >= static_cast<std::size_t>(d)) vmin = std::min(vmin, anaMag[k - d]);
                if (k + d < numBins) vmin = std::min(vmin, anaMag[k + d]);
            }
            if (anaMag[k] >= 2.0 * vmin &&
                state.peakCount < state.peaks.size())
                state.peaks[state.peakCount++] = k;
        }
        if (state.peakCount > 0) {
            // 2) Temporally track the nearest current peak instead of making
            // a memoryless nearest-peak decision. A partial with vibrato can
            // move across one or more FFT bins; switching guides merely because
            // the geometric nearest bin changed creates frame-rate phase FM.
            // Hysteresis: retain a tracked peak while it remains within 2 bins
            // of its previous location, within 0.75 bin of the best geometric
            // candidate, and no more than 6 dB weaker.
            constexpr double kTrackMaxBins = 2.0;
            constexpr double kSwitchMarginBins = 0.75;
            constexpr double kKeepMinMagRatio = 0.5;

            for (std::size_t k = 1; k + 1 < numBins; ++k) {
                if (state.anchored[k]) continue;
                const double j = static_cast<double>(k) * invFactor;
                if (j > lastBin) continue;

                std::size_t best = state.peaks[0];
                double bestD = std::fabs(j - static_cast<double>(best));
                for (std::size_t p = 1; p < state.peakCount; ++p) {
                    const double d = std::fabs(j - static_cast<double>(state.peaks[p]));
                    if (d < bestD) { bestD = d; best = state.peaks[p]; }
                }

                std::size_t guide = best;
                const int previous = state.guidePeak[k];
                if (previous >= 0 && previous < static_cast<int>(numBins)) {
                    std::size_t tracked = state.peaks[0];
                    double trackedMove = std::fabs(static_cast<double>(tracked) -
                                                   static_cast<double>(previous));
                    for (std::size_t p = 1; p < state.peakCount; ++p) {
                        const double move = std::fabs(static_cast<double>(state.peaks[p]) -
                                                      static_cast<double>(previous));
                        if (move < trackedMove) {
                            trackedMove = move;
                            tracked = state.peaks[p];
                        }
                    }

                    const double trackedD =
                        std::fabs(j - static_cast<double>(tracked));
                    const double bestMag = anaMag[best];
                    const double trackedMag = anaMag[tracked];
                    const bool sameTrack =
                        trackedMove <= kTrackMaxBins &&
                        trackedD <= bestD + kSwitchMarginBins &&
                        trackedMag >= kKeepMinMagRatio * bestMag;
                    if (sameTrack) guide = tracked;
                }

                if (previous >= 0 && guide != static_cast<std::size_t>(previous))
                    ++state.guideSwitches;
                state.guidePeak[k] = static_cast<int>(guide);
                const double currentRate = trueFreq[guide] * factor;
                const double stepRate =
                    (std::fabs(factor - 1.0) < 1e-12)
                        ? currentRate
                        : (state.prevRateValid[k]
                               ? 0.5 * (state.prevRate[k] + currentRate)
                               : currentRate);
                state.synthesis[k] += stepRate * hd;
                state.prevRate[k] = currentRate;
                state.prevRateValid[k] = 1;
                outPhase[k] = state.synthesis[k];
            }
        } else {
            // No trustworthy analysis peak exists: fall back to the regular
            // rate estimate rather than freezing phase.
            for (std::size_t k = 0; k < numBins; ++k) {
                if (state.anchored[k]) {
                    state.prevRate[k] = (trueFreq[k] * factor);
                    state.prevRateValid[k] = 1;
                    continue;
                }
                const double currentRate = trueFreq[k] * factor;
                const double stepRate = state.prevRateValid[k]
                                            ? 0.5 * (state.prevRate[k] + currentRate)
                                            : currentRate;
                state.synthesis[k] += stepRate * hd;
                state.prevRate[k] = currentRate;
                state.prevRateValid[k] = 1;
                outPhase[k] = state.synthesis[k];
            }
        }
    }
}


// ===== Shared-rotation phase model (production) ============================
// NOTE on the twist sign: with an FFT using e^{-j} convention the Dirichlet
// kernel requires the NEGATIVE twist plus an e^{-jk(1-1/f)} recentering, but
// that sign correction was validated ONLY on synthetic impulses/sweeps, NOT
// on music. It therefore stays OFF: flip kFixTwistSign to true only after
// re-validating the full gate battery on music. There is intentionally NO
// environment-variable switch (plugins have no environment to read).
static constexpr bool kFixTwistSign = false;
void mapSpectrum(const double* anaMag, const double* anaPhase, const double* guard,
                 std::size_t numBins, std::size_t fftSize, double factor,
                 double* outMag, double* outPhase,
                 std::vector<double>& cosT, std::vector<double>& sinT) {
    const double invFactor = 1.0 / factor;
    const double lastBin = static_cast<double>(numBins - 1);
    constexpr double kPi = 3.14159265358979323846;
    const double twistK = kPi * static_cast<double>(fftSize - 1) / static_cast<double>(fftSize);
    for (std::size_t k = 0; k < numBins; ++k) {
        cosT[k] = std::cos(anaPhase[k]); sinT[k] = std::sin(anaPhase[k]);
    }
    for (std::size_t k = 0; k < numBins; ++k) {
        const double j = static_cast<double>(k) * invFactor;
        double m = 0.0, ph = 0.0;
        if (j <= lastBin) {
            double re = 0.0, im = 0.0;
            const long long jc = static_cast<long long>(j + 0.5);
            for (int dt = -kSpectralTaps; dt < kSpectralTaps; ++dt) {
                const long long jj = jc + dt;
                if (jj < 0 || jj >= static_cast<long long>(numBins)) continue;
                const double d = j - static_cast<double>(jj);
                double wmag;
                if (d > -1e-9 && d < 1e-9) wmag = 1.0;
                else {
                    const double a = kPi * d;
                    wmag = (std::sin(a) / a) * (std::sin(a / kSpectralTaps) / (a / kSpectralTaps));
                }
                const double tw = (kFixTwistSign ? -1.0 : 1.0) * twistK * d;
                const double ctw = std::cos(tw), stw = std::sin(tw);
                const std::size_t u = static_cast<std::size_t>(jj);
                const double ar = anaMag[u] * guard[u] * cosT[u];
                const double ai = anaMag[u] * guard[u] * sinT[u];
                const double wr = wmag * ctw, wi = wmag * stw;
                re += ar * wr - ai * wi; im += ar * wi + ai * wr;
            }
            m = std::sqrt(re * re + im * im); ph = std::atan2(im, re);
            if (kFixTwistSign) ph -= kPi * static_cast<double>(k) * (1.0 - invFactor);
            if (!(ph == ph)) { ph = 0.0; m = 0.0; }
        }
        outMag[k] = m; outPhase[k] = ph;
    }
}

void updateSharedRotation(const double* magRef, const double* trueRef,
                          const double* outMagRef, std::size_t numBins,
                          double factor, int hop, bool crestFire, SharedRotation& st) {
    const double invFactor = 1.0 / factor;
    const double hd = static_cast<double>(hop);
    const double lastBin = static_cast<double>(numBins - 1);
    const bool wasInit = st.initialized;
    double framePeak = 0.0;
    for (std::size_t k = 0; k < numBins; ++k) if (outMagRef[k] > framePeak) framePeak = outMagRef[k];
    const double absFloor = framePeak * kAbsFloorRel;
    std::fill(st.anchored.begin(), st.anchored.end(), 0);
    st.anchoredBins = 0;
    // 1) analysis peaks (same rule as the original lock block) on the channel-power-sum magnitude
    st.peakCount = 0;
    for (std::size_t k = 1; k + 1 < numBins; ++k) {
        if (!(magRef[k] >= magRef[k - 1] && magRef[k] > magRef[k + 1])) continue;
        if (magRef[k] <= 1e-9) continue;
        double vmin = magRef[k];
        for (int d = 1; d <= 3; ++d) {
            if (k >= static_cast<std::size_t>(d)) vmin = std::min(vmin, magRef[k - d]);
            if (k + d < numBins) vmin = std::min(vmin, magRef[k + d]);
        }
        if (magRef[k] >= 2.0 * vmin && st.peakCount < st.peaks.size()) st.peaks[st.peakCount++] = k;
    }
    constexpr double kTrackMaxBins = 2.0, kSwitchMarginBins = 0.75, kKeepMinMagRatio = 0.5;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double j = static_cast<double>(k) * invFactor;
        const bool inRange = (j <= lastBin);
        bool anchor = !wasInit || crestFire;
        if (!anchor && inRange && outMagRef[k] > absFloor) {
            const std::size_t j0 = static_cast<std::size_t>(j);
            std::size_t j1 = j0 + 1; if (j1 > numBins - 1) j1 = numBins - 1;
            const double frac = j - static_cast<double>(j0);
            const double pm = st.prevMagRef[j0] * (1.0 - frac) + st.prevMagRef[j1] * frac;
            anchor = (pm <= 0.0) || (outMagRef[k] > kAttackRatio * pm);
        }
        if (!inRange) { st.rotNew[k] = 0.0; st.rateValidNew[k] = 0; continue; }
        if (anchor) {
            st.anchored[k] = 1; ++st.anchoredBins;
            st.rotNew[k] = 0.0;
            st.rateNew[k] = (factor - 1.0) * trueRef[std::min<std::size_t>(static_cast<std::size_t>(j + 0.5), numBins - 1)];
            st.rateValidNew[k] = 1;
            continue;
        }
        if (st.peakCount == 0) {   // no trustworthy peak: own rate
            const std::size_t jn = std::min<std::size_t>(static_cast<std::size_t>(j + 0.5), numBins - 1);
            const double now = (factor - 1.0) * trueRef[jn];
            const double step = st.rateValid[k] ? 0.5 * (st.rate[k] + now) : now;
            st.rotNew[k] = st.rot[k] + step * hd; st.rateNew[k] = now; st.rateValidNew[k] = 1;
            continue;
        }
        std::size_t best = st.peaks[0];
        double bestD = std::fabs(j - static_cast<double>(best));
        for (std::size_t p = 1; p < st.peakCount; ++p) {
            const double d = std::fabs(j - static_cast<double>(st.peaks[p]));
            if (d < bestD) { bestD = d; best = st.peaks[p]; }
        }
        std::size_t g = best;
        const int previous = st.guide[k];
        if (previous >= 0 && previous < static_cast<int>(numBins)) {
            std::size_t tracked = st.peaks[0];
            double trackedMove = std::fabs(static_cast<double>(tracked) - static_cast<double>(previous));
            for (std::size_t p = 1; p < st.peakCount; ++p) {
                const double mv = std::fabs(static_cast<double>(st.peaks[p]) - static_cast<double>(previous));
                if (mv < trackedMove) { trackedMove = mv; tracked = st.peaks[p]; }
            }
            const double trackedD = std::fabs(j - static_cast<double>(tracked));
            if (trackedMove <= kTrackMaxBins && trackedD <= bestD + kSwitchMarginBins &&
                magRef[tracked] >= kKeepMinMagRatio * magRef[best]) g = tracked;
        }
        st.guide[k] = static_cast<int>(g);
        // rotation of the guide's OUTPUT bin (previous frame) advanced by (f-1)*omega_guide*hop
        std::size_t kg = static_cast<std::size_t>(static_cast<double>(g) * factor + 0.5);
        if (kg >= numBins) kg = numBins - 1;
        const double now = (factor - 1.0) * trueRef[g];
        const double step = st.rateValid[kg] ? 0.5 * (st.rate[kg] + now) : now;
        st.rotNew[k] = st.rot[kg] + step * hd;
        st.rateNew[k] = now; st.rateValidNew[k] = 1;
    }
    st.rot.swap(st.rotNew); st.rate.swap(st.rateNew); st.rateValid.swap(st.rateValidNew);
    for (std::size_t k = 0; k < numBins; ++k) st.prevMagRef[k] = magRef[k];
    st.initialized = true;
}

} // namespace pps
