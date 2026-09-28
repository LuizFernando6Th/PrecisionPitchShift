# PrecisionPitchShift — engineering/QA status

## Realtime safety

The streaming engine uses preallocated fixed-capacity ring buffers, persistent
phase/spectral scratch vectors and pre-sized FFT work memory. `reset()` clears
state without reallocating. `PitchEngine::process()` can split an oversized host
block into preallocated chunks instead of growing its working buffers.

The repository includes `tools/python/check_realtime_safety.py`, a lightweight
static review aid for common allocation calls. It is not a formal proof of
realtime safety; it keeps the intended allocation boundary visible.

## Phase path

Peak-rate locking now has temporal guide tracking with hysteresis. In addition,
instantaneous-frequency phase advance uses trapezoidal integration between
adjacent analysis frames (except exact factor=1 passthrough), reducing frame-rate
FM caused by piecewise-constant hop updates.

A separate experiment with stronger identity phase locking was rejected because
it caused a round-trip regression in the shipped test set. It is not part of the
release path.

## Gain protection

The VST3 default is OFF. When enabled in the streaming plugin, gain correction
remains causal and therefore can change level when a later larger peak is found.
For offline rendering, `pps_render --autogain on` performs two passes: first measure
the complete render, then rerender with one fixed scalar gain. This preserves
relative dynamics instead of pumping gain over time.

## Sample rate / Nyquist

The DSP never intentionally resamples. Processing follows the host sample rate.
The alias guard is based on `Fs/2`, with a 1% transition width for upward shifts;
there is no fixed 20 kHz low-pass. Components that would land above Nyquist are
attenuated instead of folded back into the band.

## Validation

The current local C++ suite reports 34/34 passing tests. The same suite was also
run under AddressSanitizer and UndefinedBehaviorSanitizer with no reported memory
or undefined-behaviour errors.

The final VST3 bundle was not link-tested on this Linux environment because the
Steinberg VST3 SDK is not installed locally and external network access is not
available to fetch it. The source/CMake path remains configured for SDK 3.7.14.
