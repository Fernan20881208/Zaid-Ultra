# Zaid-Ultra

Zaid-Ultra is an all-in-one Geometry Dash / Geode suite focused on performance diagnostics, adaptive detail, gameplay intelligence, Android-first performance policy, and reactive icon visuals.

**Target:** Geometry Dash 2.2081 + Geode 5.10.1

## Unified menu

Zaid-Ultra uses one popup with six tabs:

- **Home** — shared dashboard for frame pacing, benchmark score, run momentum and Smart Detail state.
- **Performance** — Frame Doctor, Android Performance Guardian and Auto Benchmark.
- **Smart Detail** — predictive Smart LDM / ULDM learning and current detail tier.
- **Gameplay** — Run Predictor and player-intelligence metrics.
- **Icon FX** — Reactive RTX Bloom and visual controls.
- **Android** — mobile performance policy and Smart Detail integration.

The main menu gets a **ZU** button. The Geometry Dash Garage gets a dedicated **FX** button that opens the Icon FX tab directly, so Reactive RTX Bloom can be enabled from the icon-customization flow.

## Current systems

### Frame Doctor

A shared telemetry engine records frame time, worst frame, estimated 1% low, frame-pacing stability and spike count. The same samples feed the benchmark and adaptive-detail systems rather than running multiple profilers.

### Auto Benchmark Level

Produces a 0–100 performance score weighted from frame budget adherence, estimated 1% low, pacing stability and spike penalties.

### Run Predictor

Learns from the current session's attempts and deaths. It exposes session best, PB momentum and completion-confidence estimates. It does not automate inputs or alter gameplay.

### Smart LDM / ULDM

The predictive engine learns an exponential moving average of frame time for each 1% section of a level and also looks at neighboring sections. It uses hysteresis so quality does not flicker between states.

Current safe renderer stage:

1. **Normal** — untouched visuals.
2. **LDM / ULDM prediction** — first sheds non-essential built-in glitter while keeping gameplay objects and collision state intact.
3. Future renderer passes will add verified high-detail-object and particle culling without deleting gameplay objects.

### Reactive RTX Bloom

A fake-RTX visual effect for the player icon. It keeps the Geometry Dash glow and adds two additive bloom shells with soft expansion and reactive pulsing. Intensity is adjustable from the Icon FX menu. This is a visual bloom technique, not hardware ray tracing.

## Architecture

```text
src/
├── core/
│   ├── Telemetry.*
│   ├── RunPredictor.*
│   └── SmartDetail.*
├── ui/
│   └── ZaidUltraPopup.*
├── visual/
│   └── IconFX.cpp
└── main.cpp
```

## Build

GitHub Actions builds Android64, Android32 and Windows with the official Geode build action.
