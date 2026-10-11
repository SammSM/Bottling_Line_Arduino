# Bottling Line (Arduino Uno, Wokwi)

Cooperative mini-OS simulation of a bottling line on Arduino Uno.

## Features

- **Cooperative mini-OS architecture**
  - 8 periodic tasks (buttons, 4 phase tasks, display, sound, report)
  - non-blocking scheduling with `millis()` (no `delay()` in runtime logic)

- **Deterministic bottling workflow**
  - phase sequence: `idle -> fill -> cap -> label -> idle`
  - fixed timing model: wait/start delay, per-phase durations, controlled transitions
  - completed cycle counter increments on full return to `idle`

- **Operator controls with safety rules**
  - green button / `run`: accepted only in valid state
  - yellow button / skip: allowed only during `label` with minimum-time threshold
  - blue button: starts periodic status reporting

- **Real-time HMI (human-machine interface)**
  - 5 LEDs for quick state indication (`filler`, `capper`, `labeler`, `belt go`, `belt stop`)
  - 16x2 LCD for phase text and remaining time
  - standby indication with dedicated blink pattern and LCD message

- **Sound indication**
  - phase-entry beep and hurry-window pulse pattern
  - time-based pulse intervals near cycle end

## Project files
- `sketch.ino` - main firmware
- `diagram.json` - Wokwi circuit
- `wokwi.toml` - firmware paths for simulation

## Build
```bash
arduino-cli compile --fqbn arduino:avr:uno --build-path build .
