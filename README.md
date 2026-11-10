# ThirtySixThirty

A VST3 compressor / limiter / gate modelled on the signal flow of the Alesis 3630.
Unofficial homage: not affiliated with Alesis, and "3630" is their trademark, so don't
ship this under that name.

## Building

You need CMake 3.22+ and a C++17 compiler. JUCE 8.0.6 is downloaded automatically.

**Windows** (Visual Studio 2022 or the Build Tools, with "Desktop development with C++"):

    cmake -S . -B build
    cmake --build build --config Release

The plugin lands in `build/SixThirty_artefacts/Release/VST3/SixThirty.vst3`.
Copy that folder to `C:\Program Files\Common Files\VST3\` and rescan in Ableton.

**No local toolchain?** Push this folder to a GitHub repo. The workflow in
`.github/workflows/build.yml` builds Windows, macOS and Linux and attaches the `.vst3`
as a downloadable artifact on the Actions tab.

**macOS / Linux:** same two cmake commands (Linux needs the X11/freetype/fontconfig
dev packages; see the workflow for the list).

`ctest --test-dir build -C Release` runs the DSP tests.

## Controls

Same as the front panel, plus plugin conveniences.

| Control | Range | Notes |
|---|---|---|
| Threshold | -40 to +20 dBu | 0 dBFS is treated as +18 dBu (the unit's headroom spec) |
| Ratio | 1:1 to inf:1 | max = infinity |
| Attack | 0.1 to 200 ms | peak mode only |
| Release | 50 ms to 3 s | peak mode only |
| Output | -20 to +20 dB | |
| RMS | on/off | off = peak. In RMS the attack/release knobs are inert and timing is program dependent, as on the hardware |
| Soft knee | on/off | |
| -10 dBV | on/off | detector driven 11.78 dB harder; audio stays unity |
| Link | on/off | on = one control signal from both channels; off = two independent mono compressors |
| Gate threshold | Off to -10 dBu | knob at minimum = gate off |
| Gate rate | 20 ms to 2 s | time to fade from open to closed |
| Ext SC | on/off | detect from the VST3 sidechain input (keying / ducking) |
| Bypass | | crossfades, no click |

## What is modelled, and what is approximated

From the 3630 service and reference manuals:

- Feed-forward topology: the detector reads the input buffer; the VCA sits in the audio path.
- Peak or RMS detection (the hardware RMS detector is a THAT 2252).
- Threshold behaves like the hardware's biased rectifier: nothing happens below it.
- Ratio, hard/soft knee, attack/release ranges, output gain, gate ranges, stereo link
  that combines both channels into one control signal, sidechain keying.
- VCA gain is linear in dB (a THAT 2150-style exponential VCA), so gain reduction is
  smoothed in the dB domain.

Approximated, because the public documentation doesn't give the numbers:

- Soft knee is a 10 dB quadratic knee.
- The RMS detector is a 25 ms mean-square average; RMS-mode envelope timing
  (faster attack for bigger overshoots, slower release for deeper compression) is my
  approximation of "program dependent", not a measurement of the 2252.
- Peak detector holds with an 8 ms decay so low bass doesn't ripple the gain.
- A closed gate attenuates 60 dB; it opens in 0.5 ms and closes linearly in dB over the Rate time.
- No component-level analog modelling (no VCA distortion or noise, no saturation).

I did not have a readable schematic, only the written circuit description, so this
reproduces the behaviour the manuals describe rather than the component-level circuit.

## Licence note

JUCE is dual-licensed (AGPLv3 or commercial). Fine for personal use; if you distribute
the plugin you need to either release the source under the AGPL or hold a JUCE licence.
