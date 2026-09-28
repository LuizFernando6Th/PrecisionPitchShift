# VST3 parameter layout

The controller must export exactly 7 public parameters, in this order:

1. `kSourceFreq` — continuous, 100..1000 Hz, `kCanAutomate`
2. `kTargetFreq` — continuous, 100..1000 Hz, `kCanAutomate`
3. `kFactorInfo` — read-only, derived Y/X
4. `kQuality` — list: Alta Precisao / Eficiente, `kCanAutomate | kIsList`
5. `kAutoGain` — toggle, `kCanAutomate`
6. `kCeilingDb` — continuous, -6..-0.1 dBFS, `kCanAutomate`
7. `kBypassId` — toggle, `kCanAutomate | kIsBypass`

The controller checks that `parameters.getParameterCount() == 7` before reporting successful initialization.

For Windows testing, remove any previous PrecisionPitchShift installation before installing this build. The processor/controller FUIDs and product version were bumped in v1.0.2 specifically to avoid reusing cached parameter metadata from the previous development build.
