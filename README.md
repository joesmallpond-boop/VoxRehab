# Vox Rehab — vocal repair + mix suite

One plugin to take a badly recorded vocal to mix-ready. VST3 / AU / Standalone, mono or stereo, 44.1–192 kHz.

## Build & install (Mac)

```
cd VoxRehab
./build_mac.sh
```

Needs Xcode command line tools (`xcode-select --install`) and cmake (the script installs it via Homebrew if you have it). It builds a universal (Apple Silicon + Intel) VST3, AU and Standalone app, installs the plugins for your user, and makes `dist/VoxRehab-1.1.0.dmg` (an installer you can run on other Macs). Then rescan plugins in your DAW.

## Signal chain (re-orderable)

Default order:
```
Input trim + Low cut → De-Hum → De-Click → De-Plosive →
[ De-Noise · Background · Voice Isolator · Breath · De-Verb ]  (one spectral pass)
→ De-Esser → FET → Opto → 8-band Dynamic EQ → Glue / Console → Output (gain, mix, Δ)
```

The module panels are laid out left→right, top→bottom **in the order the audio flows**, each with its step number. **Drag a module by its title bar** to move it — the processing order changes to match and is saved with your session. **RESET ORDER** restores the default. Input is always first and Output always last. The five spectral modules share one analysis pass, so they move together as one block (outlined in blue). The header shows the current chain.

Latency ≈ 52 ms, reported to the DAW so it is compensated automatically, whatever the order.

## Transparency (mastering-grade engine)
- **Bit-exact when idle**: with every module off — and also with modules switched on but not acting (de-ess below threshold, compressor above signal, EQ bands at 0 dB, de-click/de-plosive not triggering) — the output is the input, sample for sample. Processing only touches the audio where you ask it to.
- The spectral engine skips its FFT entirely when none of its modules are on (crossfades in/out when you switch one on).
- Every gain, makeup and mix control is smoothed per sample and EQ bands glide, so knob moves and automation never click or zipper.
- Filters run in double precision; the glue saturation runs 4× oversampled (linear phase).
- Parameters are resolved once, with no allocation or string handling on the audio thread. Everything on uses roughly 8 % of one CPU core.

## Quick workflow

1. Pick a preset that matches the problem (Home Studio Rescue, Phone/Laptop Mic, Noisy Room, Boomy/Echoey Room, Poppy & Sibilant, Podcast Polish, Rap Vocal Punch, Tape Glue, Neve Warmth, API Punch, Tube Console Smooth, Boomy Plosives (band), Hum Kill).
2. Open the **Process Monitor** (click a module's title) to see exactly when and how hard each module works.
3. Turn on **Δ LISTEN REMOVED** in Output. You now hear only what's being taken away. If you can hear words in it, that module is too strong — back it off until the delta is just noise/room/clicks.
4. Fix in chain order: noise/room first, then de-ess, then compression, then EQ.
5. **Artifacts → Smoothing**: raise it if you hear watery/"phasey" noise reduction; lower it for sharper consonants.

## Modules

| Module | What it does | Controls |
|---|---|---|
| Input + Low Cut | Trim and rumble filter | Trim, Low Cut freq, 12/24 dB slope |
| De-Hum | Notches mains hum and its harmonics | 50/60 Hz, Harmonics, Narrow (Q), Depth |
| De-Click | Finds mouth clicks/crackles by comparing each spike with the loudest peak of the surrounding 20 ms, so voice pulses and consonants are left alone; repairs only the click band | Sensitivity, Width, Focus (split freq), Depth. Header shows clicks fixed |
| De-Plosive | Ducks a pop only when it bursts relative to the rest of the voice (3 ms lookahead). **Low Band** mode ducks everything below Freq; **Band** mode ducks only the band around Freq, Width octaves wide (e.g. a 200–500 Hz boom) | Sensitivity, Freq, Width, Depth, Release, Low Band / Band |
| De-Noise | Spectral noise reduction with automatic noise tracking, or a **learned** profile | Sensitivity, Reduction (= dB of noise removed), Tilt, Adaptive/Learned, LEARN |
| Background Remover | Gate for room tone between phrases + spectral expander for low-level spill under the voice | Gate Thresh, Gate Depth, Release, Range, Under Voice |
| Voice Isolator | Tracks pitch and keeps the voice's harmonics, pushes down what sits between them in voiced parts; band-limits outside the voice range. Consonants are not gated | Amount, Low, High, Focus. Header shows detected pitch |
| Breath | Turns down unvoiced, quiet passages between lines | Below Peak (threshold), Reduce |
| De-Verb | Estimates and subtracts the late reverb tail of the room | Amount, Room Decay (set ≈ the room's RT60), Early Keep, Floor |
| Artifacts | Smoothing for all five spectral modules | Smoothing |
| De-Esser | Split (dynamic high shelf above Freq) or Wide | Freq, Thresh, Range, Release, Split/Wide, LISTEN |
| Comp 1 · FET | 1176-flavoured: peak detector, very fast attack range, program-dependent release, All-Buttons mode | Thresh, Ratio, Attack (0.02–50 ms), Release, Knee, Makeup, Mix, SC HPF, Auto Gain, All Buttons |
| Comp 2 · Opto | LA-2A style, after the FET: fixed ~10 ms attack, two-stage release (fast first half, then 0.5–5 s depending on how hard it's been working), soft knee | Peak Reduction, Gain, HF Emphasis, Mix, Compress/Limit |
| Glue / Console | End-of-chain colour + bus compression, 4× oversampled. **Vintage Tape** (emphasis-wrapped soft saturation, head bump, HF roll-off, slow gentle glue), **Tube (Siemens)** (triode-style 2nd harmonic, warm lows, softened top, slow levelling), **API Console** (op-amp odd harmonics, low-mid punch, forward mids, fast punchy glue), **Neve Console** (transformer saturation weighted to the lows + a little 2nd, thick bottom, silky air, programme-dependent release) | Character, Drive, Colour (tone amount), Glue (bus compression), Mix, Output |
| 8-band Dynamic EQ | Bell / shelves / cuts / notch; any bell or shelf can be dynamic | Per band: type, freq, gain, Q, Dynamic + threshold, ratio, range (− cuts / + boosts when loud), attack, release |
| Output | Gain, dry/wet mix (latency-aligned), Δ listen-removed, meters | |

### Process Monitor (real-time "what is it doing" view)
The dropdown at the top-left of the big display switches between the **8-Band EQ** and a **Process Monitor** for any module — or just click a module's title (the monitored panel gets a coloured outline). Span: 2 / 5 / 10 s.

- **Level lane**: input (grey) vs output (module colour), time-aligned through the module's own latency. The shaded red area is level the module removed (blue on the De-Plosive page).
- **Detector + threshold** (De-Esser, De-Plosive, FET, Opto): the white trace is what the module is listening to, the dashed line its threshold. De-Plosive also shows the voice body (dashed) the pop is compared against.
- **Gain-reduction lane** (top): how hard it's working moment to moment — you can see attack/release shapes, the de-plosive's lookahead duck, the opto's slow recovery.
- **Markers** (De-Click, De-Plosive, Breath): a white triangle + line at every click / pop / breath caught; the header counts them.
- **Reduction by frequency** (right): *where* in the spectrum it's acting. Spectral modules (noise, background, isolator, breath, de-verb) show the real per-frequency mask; time-domain modules show their filter shape at the current reduction, with a dashed curve for the peak in view. Overall shows output-minus-input spectrum.

### EQ display
- Drag a node: frequency + gain. Mouse wheel: Q.
- Double-click empty space: add a band there. Double-click a node: band on/off.
- Right-click a node: dynamic on/off. Alt-click: reset gain.
- Dynamic bands show their range as a shaded bar and a ring that moves with the live gain.
- Behind the curve: input spectrum (grey line) and output spectrum (teal fill).

### Learning a noise profile
Find 2+ seconds of just noise (before the vocal starts), turn **LEARN** on while it plays, then turn it off and set the mode to **Learned**. The profile is saved with your session.

## Tips
- Most "badly recorded" vocals: De-Noise 10–15 dB, De-Verb 30–50 %, De-Ess on, FET 3–6 dB of gain reduction, Opto 2–4 dB. Two gentle compressors sound more natural than one heavy one.
- Dynamic EQ is great for harshness that only appears when the singer pushes: a bell at 2.5–4 kHz, dynamic, range −4 to −6 dB.
- For a boxy/boomy room use De-Verb plus a dynamic bell around 200–400 Hz.

## Honest limits
All processing is classic DSP — no neural network models. It is very good at steady noise, hum, clicks, pops, sibilance, room tail and dynamics. For loud music or crowd chatter right under the voice, an ML "dialogue isolate" tool will separate better than the Voice Isolator here.

## Rebuilding / tests
`cmake -B build -DVOXREHAB_TESTS=ON && cmake --build build --target VoxRehabTests` builds an offline test suite that measures transparency, latency, noise/click/pop/sibilance/reverb reduction, compressor and EQ behaviour and CPU.
