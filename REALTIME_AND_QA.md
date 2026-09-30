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

Default path (`EngineConfig::sharedRotation = true`, what the plugin uses):
shared-rotation phase model. Each channel keeps its own reinterpolated *analysis*
phase and a single rotation `R[k]`, common to all channels, is advanced at
`(factor - 1) * omega` and re-anchored on new energy / impulsive frames. The
output phase therefore never drifts away from the analysis phase, and the
inter-channel phase/level relations are inherited exactly. The twist sign is the
same as in the legacy mapping (`kFixTwistSign = false`); there is no environment
variable or build switch for any of this. `tests/cpp/regression_tests.cpp` locks
the behaviour (see CHANGES.md for the measured numbers and known limitations).

Legacy path (`sharedRotation = false`, kept only for A/B comparison in tests and
`pps_render --shared-rotation off`; verified bit-identical to the previous engine
on the 9 synthetic gate renders). The next two paragraphs describe this legacy path.

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

The local C++ suite (`ctest`) runs three tests: `dsp_tests` (34/34),
`regression_tests` (14/14: five gates that lock the shared-rotation model plus
contract tests) and `regression_tests_detect_legacy` (5/5: the same gates run on
the legacy engine must all fail, which proves they are not vacuous). All three
were also run under AddressSanitizer and UndefinedBehaviorSanitizer with no
reported memory or undefined-behaviour errors.

The VST3 target was built on Linux (GCC 13, SDK `v3.7.14_build_55` via
FetchContent) and passed the Steinberg validator (47 tests passed, 0 failed).
The Windows/MSVC build is done by the CI workflow and was not run locally; no
host (e.g. Audacity) test has been run by the author of this revision. See
CHANGES.md for measured numbers, known limitations and what is not verified.
