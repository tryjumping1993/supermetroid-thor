# Milestone 1 verification — 2026-09-30

Milestone 1's reference and hardware foundation is complete for the connected AYN Thor running `Thor_V1.0.0.377_20260206_165408_user`. The full-game port remains incomplete. The user subsequently requested basic build/smoke checks and removed rigid framerate gates; the detailed measurements below remain historical diagnostics.

## Completed implementation

- `CompanionRouter` owns one companion path, including pending activity launches. Repeated display callbacks do not launch duplicate tasks. Generation tokens reject stale activity attachment after stop, removal or reassignment. A denied/redirected activity recovers inline.
- The fallback activity attaches only to its current owner/target and closes when the main activity stops. The companion uses its display context. User assignment applies immediately; the game's display is excluded, and display name/physical dimensions survive logical-ID changes.
- Jump and pause bindings persist. Conflicting assignments swap. Keyboard mappings remain WASD/Space; D-pad and analog movement retain their meanings. Each physical key/device owns held state independently. Device removal/change, remapping and suspension clear held input. Device dead zones and D-pad hat precedence are respected.
- Refresh preference requests both the matching advertised display mode and Surface rate. Requests keep the display's physical resolution. The helper requests 60 Hz. Firmware may override requests; logs report requested rate, advertised/active rate, Choreographer callback frequency and GL submission frequency independently.
- The presentation sampler preserves raw timestamps and reports nominal-period offset, estimated missed measured refresh slots and absolute 8.333 ms compliance independently. A stable slower panel cannot pass the absolute budget through normalization.
- CI runs host policy and presentation-metric tests and compiles the instrumentation APK. Device execution requires the Thor and the privately imported ROM; CI does not claim hardware qualification.

## Validation

| Check | Result and scope |
| --- | --- |
| Reference reconstruction | Pinned `11c906f547edc1b57f5a5923cf977fe7b50a3694`; byte identity with the privately imported original NTSC ROM; regenerated address index unchanged |
| Native Windows fixtures | 2,363 checks passed, including all 262 default rooms and 60/120 callback simulation equivalence |
| Native ARM64 fixtures on Thor | 2,363 checks passed |
| Native ROM-independent CTest | Passed; 41 checks also passed separately on Windows and Thor |
| Portable Android host policy | Seven JUnit tests passed |
| Presentation metric calculation | Four Python tests passed, including slow regular cadence, dropped frames, 60/120 separation and missing samples |
| Android builds | Debug, unsigned release and instrumentation APK builds passed |
| Android lint | 0 errors, 32 warnings; development strings/localization, target-device assumptions and existing platform deprecations remain |
| Android device contracts | Seven instrumentation tests passed on the Thor; boundaries below |
| Bottom-screen visual check | Settings controls, inline controller picker and scrolling to display assignment/footer inspected on the final app |

The Android contracts exercise these real device behaviors:

1. An explicitly injected `InvalidDisplayException` launches the actual `CompanionActivity` on the physical helper display. Twenty repeated refresh requests keep its generation unchanged; returning to Presentation closes the fallback.
2. Explicitly denied activity launch recovers to the inline companion.
3. An app-created display without `FLAG_PRESENTATION` is handled and removed. This firmware accepts Presentation on it; the test accommodates stricter firmware using the allowed activity path.
4. A real virtual display receives the companion immediately. Releasing it produces Android's display-removal callback and restores the physical helper. Paused tick, room, position and pause state remain unchanged.
5. Remapped Android gamepad `KeyEvent`s reach the main dispatch path; jump release, pause repeat/up suppression, device removal, remap clearing and preference reload pass.
6. Home on the game display stops the main activity and closes the fallback. Resume launches one valid new fallback generation.
7. 60/120 mode preferences select the corresponding advertised mode without changing physical resolution.

Portable policy tests additionally cover reconnect with a different logical display ID, movement of the game to another display, stale launch/failure tokens, overlapping keys/controllers and opposing stick/hat input.

**Evidence boundaries:** Presentation rejection and secondary-activity denial are fault injection on this firmware, not a claim that another firmware image was tested. Display removal is an actual OS event from a virtual display, not unplugging a built-in panel. Controller tests dispatch Android events; they do not qualify every external controller model or vendor remapping utility. Cross-firmware, physical connection and demanding gameplay qualification remain in Milestone 5.

## Cadence decision

The original installed build first measured 119.225 fps at the advertised 120 Hz, median 8.387553 ms, p99 8.390886 ms, no missed intervals and 0% absolute 8.333 ms compliance. Its result agrees with the earlier report.

Final capture numbers are recorded in `build/m1/summary.json`, with raw timestamps in `cadence-60.json` and `cadence-120.json`. The table below is the checked-in summary of that private capture.

| Settled firmware mode | Frames / sampled seconds | Effective fps | Median / p99 / worst interval | Missed intervals | Absolute requested-budget coverage |
| --- | --- | --- | --- | --- | --- |
| 60 Hz | 1,922 / 31.983 s | 60.064 | 16.605833 / 16.611926 / 33.216563 ms | 5 | 99.740% of 16.667 ms |
| 120 Hz | 3,647 / 30.565 s | 119.285 | 8.383229 / 8.386562 / 8.388489 ms | 0 | 0.000% of 8.333 ms |

Both modes are verified using SurfaceFlinger's reported period; the harness rejects a capture if that period differs from the tested mode or changes mid-sample. Samples include up to one ring-buffer's initial history; duration comes from the actual timestamp endpoints. They cover Landing Site with the companion active, not a demanding scene or thermal soak.

At 120 Hz, the measured presentation cadence and logged Choreographer callback frequency agree. This supports a firmware/timing-source cadence offset rather than missed application refreshes; it does not prove the panel's electrical scan rate. At 60 Hz, occasional draw stalls/missed intervals are recorded rather than omitted.

**Decision:** retain the current Choreographer / GLSurfaceView backend for this foundation. The 120 Hz sample has no missed intervals; the 60 Hz slice meets 99% interval-budget coverage. Introducing another swap backend is not required by these foundation measurements. This does not qualify input latency, demanding-scene performance, or the strict 120 Hz absolute budget. The exact budget is unmet in this sample but, under the updated validation policy, does not block milestone progression. If later workload tests demonstrate a pacing/queue problem, integrate and compare AGDK before claiming release acceptance. [Android's Swappy integration](https://developer.android.com/games/sdk/frame-pacing/opengl/add-functions) requires owning the per-frame EGL swap, so adding its library alone to GLSurfaceView would not implement frame pacing.

## Reproduction and artifact handling

Use the pinned Java 21 / SDK 35 / NDK 27.3.13750724 / CMake 3.31.6 toolchain from `NATIVE_PORT.md`. From `android/`:

```powershell
./gradlew.bat testDebugUnitTest assembleDebug assembleRelease lintDebug assembleDebugAndroidTest
```

From the repository root, with an authorized Thor and a ROM already imported through the app:

```powershell
python -m unittest discover -s tools -p 'test_*.py'
./tools/test_native_android.ps1 -Serial YOUR_SERIAL -RomPath PATH_TO_YOUR_ROM.sfc
python tools/verify_foundation.py --adb "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe" --serial YOUR_SERIAL --seconds 30
```

The foundation harness installs both debug APKs, runs the device tests, temporarily changes Android's system minimum/peak refresh settings to test both modes, restores their exact original values in `finally`, and relaunches the normal app preference. It does not change saved refresh preferences during captures. `--skip-tests` repeats only cadence measurements. A signing-key mismatch stops installation without clearing private data.

Private ROMs, APKs, test logs, mode dumps, screenshots and JSON captures remain under ignored `build/m1/` or Android build output. The initial APK had a different debug key: its APK and private directories were backed up to `build/m1/pre-update/` before replacement, restored, and every private file matched its original SHA-256. Keep that directory if restoring the former APK is needed. No ROM bytes, user saves, serial numbers or signing keys are checked in. CI is configured but was not executed remotely in this continuation.
