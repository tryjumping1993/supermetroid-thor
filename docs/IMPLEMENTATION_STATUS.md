# Implementation status — 2026-09-30

The accepted full-game plan remains incomplete. This delivery establishes the reference, Android host, content decoder, interpolated room viewer, provisional native movement and companion UI. It cannot yet complete a game or play an authentic connected-room slice.

## Verification completed locally

| Check | Result |
| --- | --- |
| Pinned NTSC reference reconstruction | Byte-for-byte identical to the ROM on the connected Thor |
| Native ARM64 verification with ROM fixtures | 2,363 checks passed on the Thor; 41 ROM-independent checks also passed |
| Android debug build | Passed; installed and launched on the Thor |
| Android unsigned release build | Passed |
| Android lint | 0 errors, 25 warnings; mostly development text/localization and target-device scope |
| Screens | Main 1920×1080 and companion 1240×1080; both OS modes advertise 60/120 Hz |
| Bottom-screen pause | Tick remained fixed while GL frames continued; resumed on second tap |
| Bottom-screen inventory UI | Opened correctly without reparenting/crash |
| Bottom-screen development exit traversal | Landing Site to Parlor loaded via the companion; original ROM link and destination entry screen used; both displays updated |
| Native connected-room fixtures | Landing Site / Parlor / Climb / Pit / elevator boundary; safe standing bounds, pause/tick preservation, generation reset, stale/invalid choices and special/elevator rejection passed |
| Bottom-screen room selection | Inline picker selected Parlor; ordinary dialogs are avoided because Presentation window types differ |
| Suspend/resume | Main activity stopped on Home; render logs ceased; resumed the same room/tick without catching up elapsed background time |
| Presentation sample | 3,703 frames over 31.037 s; 119.276 fps; median 8.383802 ms; p99 8.387864 ms; worst 8.389219 ms; no interval above 1.5 nominal refresh periods |
| Full 120 Hz acceptance gate | Not passed: this was one static room, and strict ≤8.333 ms coverage was 0% on this firmware |

Device: AYN Thor, Snapdragon 8 Gen 2, firmware `Thor_V1.0.0.377_20260206_165408_user`. Logical display IDs were 0 and 4 in this run. The implementation discovers displays dynamically; it does not embed those IDs. Serial numbers are omitted from source/configuration.

Frame counts in app logs measure GL submissions. `tools/measure_frames.py` separately samples SurfaceFlinger presentation timestamps. Its first batch includes up to roughly one second of history. Raw screenshots, logs, ROM inputs and JSON samples are under ignored `build/`. CI is defined but has not run remotely.

## Remaining milestones

### 1. Reference and hardware foundation

Completed: pinned disassembly, verified ROM reconstruction, decoder/index, physical display discovery, 120 Hz mode request, controller/touch input, shared session, tested companion presentation, pause/resume clock handling and SRAM inspection.

Remaining: test activity fallback on firmware that rejects Presentation; test display disconnect/reassignment and controller remapping; compare measured display cadence against firmware/mode behavior; introduce AGDK frame pacing if required. Current Choreographer pacing is provisional.

### 2. Authentic connected-room native slice

Door foundation added in the continuation session: the generated address index includes source-derived door counts (vanilla lists have no terminator). All 262 rooms decode bounded bank-$83 exit headers and resolve their destination room. The companion's explicit development exit picker loads ordinary links, supplies the entering-door pointer to room-state selection, and finds a safe standing spawn near the destination cap within its entry screen. It preserves simulation tick/pause and resets velocity, held input, clock and interpolation. Settings now scroll to keep the text and controls accessible.

This is **development traversal**, not authentic door gameplay. It bypasses caps/locks and custom door ASM; elevators and special transitions are refused. No claim of original transition timing, camera scrolling, progression, or gameplay parity is made. Door tests run through Landing Site / Parlor / Climb / Pit and explicitly stop at the Blue Brinstar elevator.

Translate the `$90/$91` Samus state machine and animation timing, `$94` directional collision and slope alignment, `$82/$83/$8F` doors/loading/scroll state, `$84` PLMs/doors/items, `$93` projectiles and one `$A0+` enemy family. Add background/library drawing and layered sprite priority. Implement load-station spawn rather than the current floor-search development spawn.

Gate: Landing Site → Parlor → Climb → Morph Ball → first missile → Bomb Torizo must work with original-width triggers, progression-dependent room states and native save/load. Record deterministic inputs and compare positions, velocities, pose/timers, RNG, enemy activation and room events against the pinned reference at each native tick. None of those gameplay parity traces are available yet.

### 3. Entire game

Translate remaining movement (morph, spin, wall jump, grapple, speed booster/shinespark, water/suits), weapon behavior, every enemy/boss/projectile family, pickups, damage, event/door state, liquids and room effects, elevators, menus, Ceres, escape and all endings. Add the isolated SPC/S-DSP audio backend plus Android low-latency output; no audio backend is currently present.

Gate: start-to-ending playthroughs and all bosses pass scripted reference comparisons, including sequence-breaking cases explicitly selected for compatibility. No gameplay emulation/65816 interpreter may be substituted for native implementations.

### 4. Complete companion and saves

Replace the current per-room exploration grid with the original area map and persistent explored cells/doors/items. Feed inventory/equipment actions from authoritative progression events. Add opt-in item tracking, graded hints and routes that respect known-world information and spoiler preferences. Current Guidance tab honestly reports pending functionality.

Implement original three-slot new/load/save/copy/clear behavior, load-station restoration and bidirectional SRAM compatibility. Current import/export preserves bytes and inspects slots; it does not load the game or write movement-session progress into a save.

Gate: import vanilla SRAM, continue natively, save, and load that SRAM back in the reference with matching inventory/events/maps/station. Keep emulator save states and ROM hacks out of v1.

### 5. Widescreen, performance and release acceptance

Use additional horizontal view without changing physics, enemy activation, room triggers or the original camera scroll rules. Mask narrow rooms and scripted scenes where widening would reveal invalid geometry. Render backgrounds, priority layers, effects and original HUD correctly.

Run the proposed 30-minute thermal/performance suites over all demanding bosses and effects, with the companion active. Verify ≥99% frame-budget compliance, consistent native tick state across 60/120 Hz rendering, input latency, audio underruns, suspend/resume, process restart and both displays. Establish an explicit handling rule for the observed 119.276 Hz effective panel cadence rather than silently relaxing the agreed 8.333 ms target.

Validate on each intended Thor configuration/firmware, package without ROM-derived assets, and sign a release only after the whole-game and save-compatibility gates pass.

## Continuation handoff

Continue in this existing checkout. The development APK was installed on the Thor before it disconnected. Port source, build configuration and documentation are version controlled; private ROMs, saves, captures and build outputs remain ignored. No release has been published.

The next concrete translation is `$84` blue door PLMs plus `$93` basic Power Beam firing and `$94` door-block reactions, followed by the `$82` door-scroll state machine. Replace the explicit development traversal with tick-boundary door activation only after cap state, destination placement and script ordering are verified. The Blue Brinstar elevator then gates the route to Morph Ball. Authentic Samus movement/pose timing and replay traces remain required before claiming the connected-room slice is playable.

The Thor disconnected after the final APK reinstall. Exit-picker traversal was visually verified on the preceding APK; the final settings-scroll change passed both APK builds and lint, but its on-device visual recheck could not run after disconnection.
