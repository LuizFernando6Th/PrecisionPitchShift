#!/usr/bin/env python3
"""PrecisionPitchShift — diagnostic comparison tool (Original vs Pitch Shifted).

Compares two WAV files (same sample rate): spectrum overlay, spectrograms up
to Nyquist, band-energy table (reveals artificial rolloff above 20 kHz),
residual analysis.

Usage:
    python analyze.py --ref original.wav --proc shifted.wav --out prefix [--sr-hint 0]
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile
from scipy.signal import get_window, spectrogram


def load_mono(path):
    sr, x = wavfile.read(path)
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    # normalise scale only for display (peak)
    return sr, x


def spectrum(x, sr, skip=0):
    x = x[skip:]
    n = 1 << 17
    if len(x) < n:
        n = 1 << max(10, int(np.log2(len(x))) - 1)
    seg = x[len(x) // 2 - n // 2: len(x) // 2 + n // 2]
    w = get_window("hann", n, fftbins=False)
    S = 20 * np.log10(np.abs(np.fft.rfft(seg * w, n)) + 1e-12)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    S -= S.max()
    return f, S


def band_table(x, sr, skip=0):
    # Time-averaged power per band (Welch-style over the whole file, edges
    # excluded): truthful for sweeps and stationary signals alike.
    x = x[skip:]
    n = 1 << 16
    margin = int(len(x) * 0.1)
    span = x[margin:len(x) - margin] if len(x) > 4 * n else x
    P = None
    nwin = 9
    last = max(0, len(span) - n)
    starts = np.linspace(0, last, nwin).astype(int) if last > 0 else [0]
    for start in starts:
        seg = span[start:start + n] * get_window("hann", n, fftbins=False)
        Q = (np.abs(np.fft.rfft(seg, n)) ** 2)
        P = Q if P is None else P + Q
    P = P / max(1.0, float(P.sum()))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    tot = P.sum()
    edges = [0, 100, 1000, 5000, 10000, 15000, 20000, 30000, 45000,
             60000, 80000, 96000]
    rows = []
    for a, b in zip(edges[:-1], edges[1:]):
        if a >= sr / 2:
            break
        m = (f >= a) & (f < min(b, sr / 2))
        rows.append((f"{a / 1000:g}-{min(b, sr/2) / 1000:g}k",
                     10 * np.log10(P[m].sum() / tot + 1e-15)))
    return rows


def save_plots(ref_path, proc_path, prefix, sr_hint=0.0):
    sr_r, xr = load_mono(ref_path)
    sr_p, xp = load_mono(proc_path)
    assert sr_r == sr_p, "sample-rate mismatch between ref and proc"
    sr = float(sr_r)
    skip = int(sr * 0.3)
    os.makedirs(os.path.dirname(prefix) or ".", exist_ok=True)

    # 1) spectrum overlay to Nyquist
    fr, Sr = spectrum(xr, sr, skip)
    fp, Sp = spectrum(xp, sr, skip)
    plt.figure(figsize=(10, 4))
    plt.semilogx(fr[1:], Sr[1:], label="original", linewidth=1)
    plt.semilogx(fp[1:], Sp[1:], label="pitch shifted", linewidth=1, alpha=0.8)
    plt.axvline(20000, color="k", linestyle="--", linewidth=0.8, label="20 kHz (reference only)")
    plt.xlim(20, sr / 2)
    plt.ylim(-140, 5)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude (dB, peak-normalised)")
    plt.title(f"Spectrum to Nyquist ({sr/1000:g} kHz session) — no fixed 20 kHz filter")
    plt.legend()
    plt.tight_layout()
    plt.savefig(prefix + "_spectrum.png", dpi=110)
    plt.close()

    # 2) spectrograms
    for tag, xx in (("ref", xr), ("proc", xp)):
        f, t, Sxx = spectrogram(xx, sr, window="hann", nperseg=8192,
                                noverlap=6144, nfft=8192, mode="magnitude")
        plt.figure(figsize=(10, 4))
        plt.pcolormesh(t, f / 1000.0, 20 * np.log10(Sxx + 1e-9),
                       shading="auto", vmin=-120, vmax=0, cmap="magma")
        plt.axhline(20, color="cyan", linestyle="--", linewidth=0.8)
        plt.ylim(0, sr / 2000.0)
        plt.xlabel("Time (s)")
        plt.ylabel("Frequency (kHz)")
        plt.title(f"Spectrogram 0..Nyquist — {tag} (cyan dashed = 20 kHz)")
        plt.colorbar(label="dB")
        plt.tight_layout()
        plt.savefig(prefix + f"_gram_{tag}.png", dpi=110)
        plt.close()

    # 3) band table
    br, bp = band_table(xr, sr, skip), band_table(xp, sr, skip)
    lines = ["band | original dB | shifted dB |", "|---|---|---|"]
    for (b, a), (_, c) in zip(br, bp):
        lines.append(f"{b} | {a:.1f} | {c:.1f}")
    with open(prefix + "_bands.md", "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--proc", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    print(save_plots(args.ref, args.proc, args.out))


if __name__ == "__main__":
    main()
