# Engineering Guardrails (Mandatory)

Always follow these real-time safety rules.

## Never introduce these failures

- Real-time audio thread violations:
  - allocations in `processBlock()`
  - locks on the audio thread
  - file I/O on the audio thread
- Channel sharing bugs:
  - left/right processing overwriting each other
  - shared state used incorrectly across channels
- No parameter smoothing when modulation or automation changes audible values:
  - avoid zipper noise
- Bad filter implementations:
  - denormals
  - unstable coefficient use
  - unstable behavior at edge values
- Deprecated JUCE classes or incorrect CMake / exporter flags
- Memory leaks or crashes on plugin unload

## Required habits

- Keep DSP state deterministic and channel-safe.
- Prefer preallocation and reset in `prepareToPlay()`.
- Guard all preview-only logic from affecting the audio path unless explicitly intended.
- Rebuild after each meaningful DSP or project-configuration change.
- If a change touches filters, automation, channel routing, or teardown, explicitly review those risks before finishing.

## RefMaster-specific meaning

For this project, these rules apply first to:

- `Source/PluginProcessor.*`
- reference analysis code
- future filter / EQ generation
- plugin state save/load
- project/exporter changes that affect JUCE modules or build flags
