# Bolbol RefMaster

Bolbol RefMaster is a JUCE-based VST3 plugin for **reference-based mastering assistance**.  
It analyzes incoming audio, compares it to a loaded reference track, visualizes tonal differences, and previews conservative EQ-match moves.

## Current Status

This repository is an active prototype with:

- real-time FFT analyzer
- offline reference-track analysis
- input/reference/target spectrum overlays
- preview EQ auditioning
- committed applied-match state
- processor smoke-test coverage

Still pending:

- DAW listening validation
- deeper match-engine refinement
- final UI/product polish

## Project Structure

- `Source/` — plugin DSP and UI code
- `Tests/` — smoke-test utility
- `docs/` — product, DSP, UI/UX, APC workflow, and engineering guardrails
- `Builds/MacOSX/` — generated Xcode project
- `Bolbol RefMaster.jucer` — Projucer project

## Build

```sh
xcodebuild -project "Builds/MacOSX/Bolbol RefMaster.xcodeproj" \
  -scheme "Bolbol RefMaster - VST3" \
  -configuration Debug build
```

## Smoke Test

With an audio file in the repo root:

```sh
scripts/run_processor_smoke.sh "your-audio-file.mp3"
```

This checks dry transparency, preview/applied processing behavior, state restore, and numerical safety.

## Design References

Before changing architecture, DSP, or UI, review:

- `docs/01_product.md.md`
- `docs/03_dsp_design.md.md`
- `docs/06_engineering_guardrails.md`
- `docs/ui-ux-design.png`

## Notes

- Keep `processBlock()` real-time safe: no allocations, locks, or file I/O.
- `handoff.md` is for local direction/context and should not be committed.
