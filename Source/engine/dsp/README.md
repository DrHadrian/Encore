# Source/engine/dsp: the Encore Engine's DSP

Building blocks for Encore's native modules (FX Units) and the engine
(decision 0062). Plain C++ on JUCE; no file here allocates, locks or does I/O
on the audio thread (see each header for which thread each function is for).
Coefficients and filter states are double; audio buffers are float. Written
from papers and maths in our own words, never copied.

| File | What |
|---|---|
| `FilterDesign` | Second-order sections: decramped (matched) bells and shelves after Vicanek (2016), bilinear low/high/band pass, notch, all pass, first order; Butterworth Qs; the analog prototypes' magnitudes |
| `Svf.h` | The TPT state-variable filter (Zavalishin, Simper); plays any stable section exactly and stays stable under fast modulation |
| `FilterBand` | A band (bell, shelves and tilt shelf at 6–96 dB/oct, cuts 6–96 dB/oct, notch, band pass, all pass; slopes below) as up to 8 sections, and the one response function the audio and the UI share; `glideTo` moves a band to a new design sample by sample (no zipper noise when a slider or pedal moves) |
| `Detector.h` | Peak / RMS level detectors and a gate envelope (attack, hold, release, hysteresis) with the X4's modes: Gate, Expander ((T − L)(r − 1) dB, at most \|Range\|), Ducker, and a decision made elsewhere (a linked follower) (decision 0066) |
| `AdaptiveGate.h` | The "Smart" gate the S2 and the X4's Smart mode share: the noise floor learned from steady noise (frozen near silence and while tuning), a release that follows the note, a chatter guard, a 0.3 ms opening fade; its values Proposed, as named constants (decision 0066) |
| `LookAhead.h`, `FadingDelay.h` | A delay for the audio path (the gates' look-ahead, up to 5 ms at 192 kHz; the Chain's split alignment) whose time changes crossfade instead of jumping |
| `Oversampler` | The Quality switch: 2× / 4× / 8×, prepared up front, crossfaded, latency reported; `resetTo` starts at a Quality with no fade (B4); mono has a set of its own (JUCE's delay line asserts, and allocates in Debug, on fewer channels than prepared). At 48 kHz the latency is 4 / 6 / 6 samples |
| `Crossfade.h` | The 10 ms true crossfade for a choice change (old and new setting side by side), equal-amplitude with a smoothstep shape |
| `SoftClip.h` | A smooth, slightly asymmetric soft clipper (a 9 V op-amp stage into its rails): linear to −6 dBFS, 1 dB down at +6 / +5 dBFS; the Pedal EQ's clip, the drives later (decision 0065) |
| `Saturation.h` | The character curves (B4, decision 0068; values Proposed, tuned in U3), memoryless, for the oversampled domain: **Soft** = `SoftClip` made symmetric (linear to −12 dBFS, 1 dB down at 0 dBFS; odd harmonics only), **Warm** = (1 − e^(−0.3x)) / 0.3 into an asymmetric `SoftClip` (linear to −12 dBFS, 1 dB down at −2 / +4 dBFS; the second harmonic leads up to a firm drive, makes DC: put a DC blocker after it). `LowEmphasis`: a first-order low shelf (+3 dB at 150 Hz) and its exact inverse, around Warm for its low-end weight. The Studio EQ's character now, the drives later |
| `Loudness.h` | K-weighting (ITU-R BS.1770) for any rate, and a sliding loudness meter in LUFS (bins, a window, the last bin): the Studio EQ's Smart auto gain (decision 0067) |
| `Biquad.h`, `OutputEq.h`, `SafetyLimiter`, `PitchDetector`, `StreamResampler` | Older engine pieces (today's EQ, output EQ, limiter, tuner, resampling) |

## Shapes and slopes (B3, the Studio EQ's bands)

The slope is the **filter's order × 6 dB/oct**, on every shape, as in Pro-Q
(a 12 dB/oct band pass is second order, so each of its skirts falls 6 dB/oct).

| Shape | 6 dB/oct | 12 dB/oct | 18–96 dB/oct |
|---|---|---|---|
| Bell | | one decramped section; **no slope** (Q sets the width) | as 12 |
| Low / high shelf | first order, matched at DC, the half-gain point and Nyquist; Q has no effect | the B0 decramped shelf; **Q shapes the corner** | a **Butterworth shelf** of the order: matched second-order shelves of 2/N of the gain at the same frequency with Butterworth Qs (plus a first-order one if N is odd), so it stays decramped; Q has no effect |
| Tilt shelf | | a high shelf of the gain, lowered by half: −gain/2 below, +gain/2 above, 0 dB at its frequency | likewise, on the shelf of that slope |
| Low / high cut | Butterworth, as B0 (Brickwall plays as 96 until Linear phase, B5) | | |
| Notch | plays as 12 | one notch with Q | one notch per pole pair, the same Q: wider at a steeper slope |
| Band pass | plays as 12 | one band pass with Q, 0 dB at the centre | one per pole pair, each section's Q lowered so the −3 dB width stays the one Q sets: steeper skirts, same width (96 = 8 sections, skirts 48 dB/oct) |
| All pass | first order (−90° at the frequency) | second order with Q (−180°) | −90° at the frequency per 6 dB/oct |

**Measured accuracy** against the analog prototypes, at 44.1–192 kHz, gains
±30 dB (`Tests/EqShapeTests.cpp` holds these):
- **Bells and 12 dB/oct shelves:** as B0's table (`ModuleDspTests`): within
  0.45 dB up to a corner of fs/16 and 0.9 dB up to fs/4 at ±15 dB, much
  closer at everyday settings.
- **Shelves and tilt, every slope, corner up to fs/48** (1 kHz at 48 kHz):
  within 0.03 dB below fs/4; **6 dB/oct within 0.11 dB** (a first-order
  section can't follow a +30 dB prototype whose transition runs past Nyquist).
- **Corner up to fs/10 (4.8 kHz at 48 kHz), ±12 dB:** within 0.15 dB.
- **Anything, to 0.9 × Nyquist:** within 2.35 dB. The worst case is a steep
  (48–96 dB/oct) shelf at ±30 dB with its corner above about 8 kHz at 44.1 or
  48 kHz: its high-Q sections are matched at three points each and lose a
  little right at the corner. Everything is matched; nothing is bilinear
  except the cuts, notch, band pass and all pass, which are prewarped at their
  frequency as in B0.

Tests: `Tests/ModuleDspTests.cpp` (every rate from 44.1 to 192 kHz, generated
signals from `Tests/TestSignals.h`), the CPU benchmark in
`Tests/BenchmarkTests.cpp`, and `EncoreRender` for listening.
