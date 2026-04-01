# APC Workflow Reference for Bolbol RefMaster

This repository **uses Audio Plugin Coder (APC) as a workflow reference only**.  
Do **not** migrate the project to APC’s CMake/plugin-folder structure unless explicitly planned.

## What We Adopt

Use APC’s five-phase flow as lightweight guidance:

1. **Dream** — define plugin intent, user value, core controls
2. **Plan** — choose architecture, scope, technical constraints
3. **Design** — align UI with `docs/ui-ux-design.png`
4. **Implement** — build the smallest safe vertical slice
5. **Ship** — verify build, smoke-test in host, package later

## How It Maps to This Repo

- APC `creative brief` → `docs/01_product.md.md`
- APC `audio behavior` → `docs/02_audio_behavior.md.md`
- APC `DSP planning` → `docs/03_dsp_design.md.md`
- APC `design phase` → `docs/04_ui_ux.md.md` + `docs/ui-ux-design.png`
- APC `plugin source` → `Source/`
- APC `state tracking` → local notes, commits, and optional `docs/apc-status-template.json`

## Rules for This Project

- Keep the current **Projucer + Xcode** setup.
- Do not import APC’s top-level CMake/build system into this repo.
- Prefer small, phase-based progress:
  - FFT analyzer
  - reference analysis
  - comparison preview
  - later EQ generation
- Keep audio-thread code real-time safe.
- Always enforce the mandatory engineering guardrails in `docs/06_engineering_guardrails.md`.

## Suggested Phase Exit Criteria

### Dream
- problem statement is clear
- top controls/features are named

### Plan
- affected files are identified
- DSP/UI scope is limited

### Design
- screen/layout matches the PNG direction

### Implement
- feature works
- build succeeds
- real-time safety rules still hold
- no channel-sharing regressions
- no new unload / leak risk introduced

### Ship
- manual DAW smoke test completed
- remaining risks documented
