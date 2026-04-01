# Repository Guidelines

## Project Structure & Module Organization
- `Source/` contains the hand-written plugin code: `PluginProcessor.*` for audio/DSP and `PluginEditor.*` for the UI.
- `JuceLibraryCode/` is generated JUCE support code; avoid manual edits unless a JUCE export issue requires it.
- `Builds/MacOSX/` contains the generated Xcode project (`Bolbol RefMaster.xcodeproj`) and local build outputs.
- `Bolbol RefMaster.jucer` is the Projucer project definition; regenerate IDE files from here when project settings change.
- `docs/` contains plugin planning references; review these before architecture, DSP, or UI work: `docs/01_product.md.md` (product goals), `docs/02_audio_behavior.md.md` (audio behavior), `docs/03_dsp_design.md.md` (DSP design), `docs/04_ui_ux.md.md`, `docs/ui-ux-design.png` (the primary UI/UX design reference for implementation), and `docs/05_apc_workflow_reference.md` (APC-inspired workflow guidance for this repo).
- `handoff.md` captures the current product and DSP direction; read it before large changes.

## Build, Test, and Development Commands
- `open Bolbol RefMaster.jucer` — open Projucer to adjust modules, exporters, or project metadata.
- `open Builds/MacOSX/Bolbol RefMaster.xcodeproj` — open the generated Xcode project for day-to-day development.
- `xcodebuild -project Builds/MacOSX/Bolbol RefMaster.xcodeproj -scheme "Bolbol RefMaster - VST3" -configuration Debug build` — build the main plugin target.
- `xcodebuild -project Builds/MacOSX/Bolbol RefMaster.xcodeproj -scheme "Bolbol RefMaster - All" -configuration Debug build` — build every exported target.

## Coding Style & Naming Conventions
- Use the JUCE/C++ style already present: 4-space indentation, braces on the next line for functions, and spaces inside parentheses only where existing code uses them.
- Keep class names in `PascalCase`, methods in `camelCase`, and constants as `kDescriptiveName` or `constexpr` members.
- Prefer small, explicit changes in `PluginProcessor` and `PluginEditor`; do not introduce large abstractions early.
- Keep the audio thread real-time safe: no allocations, locks, or file I/O in `processBlock()`.

## Testing Guidelines
- There is no automated test suite yet. Validate changes with a clean Debug build and a manual smoke test in a host such as Ableton Live.
- For DSP changes, note the input signal used, expected behavior, and whether audio remained artifact-free.
- If you add tests later, place them in a dedicated `Tests/` directory and name files after the feature under test (for example `FFTAnalyzerTests.cpp`).

## Commit & Pull Request Guidelines
- No Git history is available in this workspace, so there is no established local commit pattern to mirror.
- Use short, imperative commit subjects with a brief body explaining why the change was made.
- Pull requests should include: a summary of behavior changes, manual test notes, affected build scheme(s), and screenshots for UI updates.
- If a change touches DSP behavior, mention the host/DAW and sample rate used for verification.

## Git Workflow (Mandatory)
After any code change:

1. Stage all changes
2. Create a clear commit

Commands:

```sh
git add .
git commit -m "<short, descriptive message>"
```

Rules:
- Commit after every meaningful change.
- Do not batch large unrelated changes into one commit.
- Commit messages must describe what changed and why.
- Keep commits small and incremental.

Examples:
- `Add initial FFT buffer setup`
- `Implement basic audio buffering for analyzer`
- `Fix build issue with JUCE module includes`

Never leave the project in a modified but uncommitted state after completing a task.

## Architecture Notes
- Keep analysis code in `PluginProcessor` and rendering in `PluginEditor`.
- Use `docs/` as the planning source of truth for plugin goals, audio behavior, DSP design, and UI/UX direction before proposing or implementing changes.
- For UI implementation, match the layout and visual direction from `docs/ui-ux-design.png`; use it as the main design reference even if the markdown UI doc is sparse.
- Use APC as a workflow reference only: follow its Dream → Plan → Design → Implement → Ship mindset via `docs/05_apc_workflow_reference.md`, but keep this repo’s existing Projucer/Xcode structure.
- The current roadmap starts with a minimal FFT analyzer; defer EQ matching and broader architecture work until the analyzer is stable.
