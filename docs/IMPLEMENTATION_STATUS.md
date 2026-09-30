# Implementation status — 2026-09-30

The accepted full-game plan remains incomplete. This delivery establishes the reference, Android host, content decoder, interpolated room viewer, native Samus movement, doors, items, saves, a playable Landing Site → Bomb Torizo route and the companion UI. It cannot yet complete a game.

## Working validation policy

Per the user's 2026-09-30 direction, use basic build and focused smoke checks for each implementation increment, then continue to the next milestone once its functional scope works. Use deeper testing for observed failures or compatibility-sensitive changes. Exact frame-budget percentages and the firmware's approximately 119.3 Hz cadence are diagnostics, not milestone blockers. Full reference traces and long thermal suites are optional follow-up tools; they are not required at every stage.

## Verification completed locally

| Check | Result |
| --- | --- |
| Pinned NTSC reference reconstruction | Byte-for-byte identical to the ROM on the connected Thor |
| Native ARM64 verification with ROM fixtures | 2,363 checks passed on the Thor; 41 ROM-independent checks also passed |
| Android debug build | Passed; installed and launched on the Thor |
| Android unsigned release build | Passed |
| Android lint | 0 errors, 32 warnings; mostly development text/localization and target-device scope |
| Screens | Main 1920×1080 and companion 1240×1080; both OS modes advertise 60/120 Hz |
| Bottom-screen pause | Tick remained fixed while GL frames continued; resumed on second tap |
| Bottom-screen inventory UI | Opened correctly without reparenting/crash |
| Bottom-screen development exit traversal | Landing Site to Parlor loaded via the companion; original ROM link and destination entry screen used; both displays updated |
| Native connected-room fixtures | Landing Site / Parlor / Climb / Pit / elevator boundary; safe standing bounds, pause/tick preservation, generation reset, stale/invalid choices and special/elevator rejection passed |
| Bottom-screen room selection | Inline picker selected Parlor; ordinary dialogs are avoided because Presentation window types differ |
| Suspend/resume | Main activity stopped on Home; render logs ceased; resumed the same room/tick without catching up elapsed background time |
| Presentation sample, Milestone 1 | 3,647 frames over 30.565 s; 119.285 fps; median 8.383229 ms; p99 8.386562 ms; worst 8.388489 ms; no interval above 1.5 nominal refresh periods |
| Milestone 1 host/device tests | Seven host policy tests, seven device instrumentation tests and four presentation metric tests passed |
| Bottom-screen Milestone 1 controls | Refresh/button settings, inline button picker and scrolling to display assignment/footer visually checked |
| Presentation diagnostics | Static-room capture: approximately 119.3 Hz; exact 8.333 ms coverage is retained as diagnostic data and does not block progression |
| Milestone 2 route | Native `--milestone2` (122 checks) and `--route` replay (Landing Site to defeated Bomb Torizo, Samus alive) passed; debug APK built, installed and launched on the Thor |
| Milestone 2 first increment | Native build and 108 focused checks passed; Android debug build and seven host-policy tests passed; installed and launched on Thor, companion Shoot visibly fires original beam artwork |

Device: AYN Thor, Snapdragon 8 Gen 2, firmware `Thor_V1.0.0.377_20260206_165408_user`. Logical display IDs were 0 and 4 in this run. The implementation discovers displays dynamically; it does not embed those IDs. Serial numbers are omitted from source/configuration.

Frame counts in app logs measure GL submissions. `tools/measure_frames.py` separately samples SurfaceFlinger presentation timestamps. Its first batch includes one timestamp ring of history (roughly one second at 120 Hz or two seconds at 60 Hz). Raw screenshots, logs, ROM inputs and JSON samples are under ignored `build/`. CI is defined but has not run remotely.

## Remaining milestones

### 1. Reference and hardware foundation — complete on the verified Thor configuration

Completed: pinned disassembly, verified ROM reconstruction, decoder/index, physical display discovery, 120 Hz mode request, controller/touch input, shared session, tested companion presentation, pause/resume clock handling and SRAM inspection.

Closed in the Milestone 1 continuation: companion ownership now covers Presentation, a single tracked fallback activity and inline recovery when secondary windows are denied. Pending activity launches carry generation tokens; stopped, removed or reassigned targets cannot attach stale windows. Assignment applies immediately and excludes the game display. Controller jump/pause bindings persist, conflicting bindings swap, and per-device held state clears on remapping, disconnect, configuration changes and lifecycle suspension.

Seven portable host-policy tests and seven Android instrumentation tests exercise duplicate display callbacks, stale launches, actual virtual-display removal, immediate reassignment, fallback launch/denial, Home/resume, remapped events and advertised-mode requests. Presentation rejection and activity denial are injected explicitly; this firmware normally accepts Presentation, including a test display without FLAG_PRESENTATION. Physical built-in-panel removal and a different firmware image were not tested. These limits remain part of Milestone 5's configuration qualification.

Both settled firmware modes have been measured independently with the companion active. At 120 Hz, Choreographer callbacks and presentation agree at approximately 119.285 Hz with no missed refresh intervals in the captured room. At 60 Hz, the final capture passed 99.740% of the 16.667 ms interval budget, with five missed intervals. Retain Choreographer for the current foundation: the measurements do not justify changing the swap backend for this slice. Swappy remains an option if demanding-scene measurements establish a pacing problem; no AGDK library is integrated. Per the updated validation policy, the exact 8.333 ms target does not block milestone progression. Address visible stutter or measured regressions when they arise.

See [Milestone 1 evidence and reproduction](MILESTONE_1_VERIFICATION.md) for the final capture numbers, test boundaries and commands.

### 2. Authentic connected-room native slice — complete for the Landing Site → Bomb Torizo route

The route now plays end to end in the native session, from the original ship load station through Bomb Torizo, using only ROM-derived data and native translations (no 65816 interpreter):

- **Samus**: pose/animation transitions come from the bank `$91` transition tables and `$92` animation delays; ground/air movement, morph ball roll/unmorph, Power Beam, missiles (Select toggles; selection reverts to Power Beam at zero) and bombs use original constants, radii, cooldowns and damage.
- **Rooms and doors**: bank `$83` door headers drive tick-boundary door activation, cap closing on entry, scroll/transition timing and destination placement; the Blue Brinstar elevator is native. Blue caps open with any shot; red caps need five missile hits; grey caps lock during the Bomb Torizo fight and release on its death. Room states follow collected items, events, bosses and the entering door.
- **Items**: Morph Ball, Bombs, first Missile tank (shot-revealed Chozo orb) and energy tanks update authoritative progression; revealed orbs and shot/crumble blocks follow their original draw lists.
- **Enemies**: Pit pirates and Bomb Torizo run native behaviour from original headers, health, damage, artwork and drops. The statue awakening and death script run natively.
- **Saves**: new game, load and save use the original ship/save-station placement and the vanilla SRAM payload layout (items, doors, events, bosses, map bits).
- **Rendering/companion**: enemies, enemy shots and door transitions render with interpolation; the companion has new/load/save controls, slot selection and a death state.

Verification (basic policy): `thor_tests ROM.sfc --milestone2` (122 checks) covers stations, SRAM round trip, Morph Ball, first missile, pirates and Bomb Torizo. `thor_tests ROM.sfc --route tools/routes/landing_site_to_bomb_torizo.txt` replays 33,320 frames of recorded raw controller input through the session: Landing Site → Parlor → Climb → Pit → elevator → Morph Ball → Construction Zone → first Missile → back up the Parlor → morph-ball tunnel → five missiles into the Flyway red door → Bomb Torizo room → Bombs → Torizo defeated by Power Beam with Samus alive. The debug APK built and launched on the Thor at the Landing Site load station; the route itself was run natively on the host, not by hand on the device.

Known limits: audio, impact/particle effects, background layer 2 and full layered sprite priority are absent; only the enemy families and item types on this route are translated (other PLMs, items and enemies are inert or skipped); slope alignment and unusual collision exploits are unverified; there is no per-tick reference trace comparison. These carry into Milestone 3.

### 3. Entire game

Translate remaining movement (morph, spin, wall jump, grapple, speed booster/shinespark, water/suits), weapon behavior, every enemy/boss/projectile family, pickups, damage, event/door state, liquids and room effects, elevators, menus, Ceres, escape and all endings. Add the isolated SPC/S-DSP audio backend plus Android low-latency output; no audio backend is currently present.

Completion scope: start-to-ending playthroughs and all bosses work, with focused compatibility checks for the sequence-breaking cases selected for support. No gameplay emulation/65816 interpreter may be substituted for native implementations.

### 4. Complete companion and saves

Replace the current per-room exploration grid with the original area map and persistent explored cells/doors/items. Feed inventory/equipment actions from authoritative progression events. Add opt-in item tracking, graded hints and routes that respect known-world information and spoiler preferences. Current Guidance tab honestly reports pending functionality.

Implement original three-slot new/load/save/copy/clear behavior, load-station restoration and bidirectional SRAM compatibility. Current import/export preserves bytes and inspects slots; it does not load the game or write movement-session progress into a save.

Completion scope: import vanilla SRAM, continue natively, save, and load that SRAM back in the reference with matching inventory/events/maps/station. Keep emulator save states and ROM hacks out of v1.

### 5. Widescreen, performance and release acceptance

Use additional horizontal view without changing physics, enemy activation, room triggers or the original camera scroll rules. Mask narrow rooms and scripted scenes where widening would reveal invalid geometry. Render backgrounds, priority layers, effects and original HUD correctly.

Check smooth rendering and responsive input in representative gameplay with the companion active, consistent simulation at 60/120 Hz, audio, suspend/resume and process restart. Run longer thermal or demanding-scene captures when symptoms justify them. Approximate firmware cadence and exact frame-budget compliance are diagnostics; no rigid 8.333 ms or ≥99% gate is required.

Validate on each intended Thor configuration/firmware, package without ROM-derived assets, and sign a release once whole-game functionality and save compatibility are verified.

## Continuation handoff

Continue in this existing checkout. The development APK was installed on the Thor before it disconnected. Port source, build configuration and documentation are version controlled; private ROMs, saves, captures and build outputs remain ignored. No release has been published.

Milestone 2 is complete; start Milestone 3. The development exit picker remains in Settings but is no longer the traversal path. Next: remaining movement (spin/wall jump, speed booster, grapple, suits/liquids), remaining weapons, the general PLM/item set and enemy families, then audio. Use `--route` as the regression check when touching movement, doors or weapons, and re-record or extend it as scope grows.

The Milestone 1 continuation reconnected the Thor, rebuilt and installed the current development APK, and ran the foundation device tests. The previous APK had a different debug signing key; its APK and private data were backed up under ignored `build/m1/pre-update/`, and every restored private file matched its backup SHA-256. ROM fixtures and user preferences were preserved. The new hardware evidence supersedes the earlier disconnection limitation for the foundation. Authentic gameplay and complete-game acceptance remain pending.
