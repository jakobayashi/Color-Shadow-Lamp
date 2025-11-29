# Manual test plan: remote-only control

## Purpose
Verify that remote control (web UI) is the only source of control, button/knob inputs do not change modes, and rapid updates (colors, party speed, music sync) do not overload the ESP.

## Setup
- Flash firmware and upload SPIFFS.
- Power on; device should immediately start in remote (Wi‑Fi) mode.
- Open the web UI at the device IP.

## Test cases
1) **Boot mode**
   - After power on, open `/api/status`.
   - Expect `mode` = `wifi`, `apFallback` reflecting network state.
   - Confirm LEDs remain unchanged until a web command is sent.

2) **Button disabled**
   - Press/release the rear button several times.
   - Poll `/api/status` before/after; `mode` should remain `wifi` (or `party` if previously set via UI).

3) **Knobs disabled**
   - Rotate all three knobs while in `wifi` mode.
   - Confirm LED output does not change and status stays in `wifi`.

4) **Rapid color drag**
   - Drag the color wheel continuously for 30 seconds.
   - Expect responsive color changes with no UI hangs and `/api/status` reachable.
   - Device must not reset or stop responding.

5) **Party mode rate changes**
   - Set party mode via UI; verify smooth cycling.
   - Move the party rate slider/number up and down rapidly for 15 seconds.
   - Expect smooth speed adjustments, no hangs; `/api/status` remains responsive.

6) **Source swaps**
   - Switch between `music` and `party` via UI five times; in `music`, confirm BPM appears and LEDs cycle; in `party`, confirm manual Hz applies.
   - Expect no crashes/reboots; modes change only through UI.

7) **Music visualizer**
   - Ensure proxy is running; switch to `music` mode.
   - Confirm now playing, next track, progress bar, and album art render; BPM is nonzero.
   - Verify party hue speed follows BPM (visible change when switching from a slow to fast song).

8) **Overload resilience**
   - In `wifi` mode, alternate rapidly between color wheel drags and party rate tweaks for 60 seconds.
   - Then in `music` mode, keep the UI open for 2 minutes while tracks change; confirm the device stays responsive and `/api/status` continues to answer.
