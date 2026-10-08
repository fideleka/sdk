# Independent backlight — first integrated iteration

## Hardware gate

Enable **`-DLILKA_INDEPENDENT_BACKLIGHT=1`** only on modified Lilka v2: the amplifier module's SD header connection must be physically isolated from GPIO46 and independently biased. Stock builds default to zero and retain the original shared backlight/amplifier behaviour. Lilka v1 remains unsupported/no-op even if the flag is supplied.

Anton device-confirmed brightness control in the earlier Keira test after isolating SD and installing the bias resistors. The integrated implementation described here has source/host verification, not new device qualification.

## Application API

Included by `<lilka.h>` and initialized automatically by `lilka::begin()` through `board.begin()`:

```cpp
lilka::brightness.isEnabled();
lilka::brightness.getBrightness();           // RAM only, 0..100
lilka::brightness.setBrightness(40);         // clamp, apply, deferred save
lilka::brightness.changeBrightnessLive(-10); // bounded relative change
lilka::brightness.stepBrightnessShortcut(1);// +10 and feedback
```

Setters return false on unsupported hardware or failed hardware updates. Disabled builds report 100 without accessing brightness NVS or LEDC. PWM percent is duty cycle, not a calibrated perceptual scale.

## Global controls and feedback

- Hold Select first, then Left/Right: -/+10 percentage points.
- Repeat starts after 400 ms and proceeds every 100 ms, with no catch-up bursts.
- Consumed directions and Select cancellation follow the existing SDK volume arbitration; Start takes priority and opposing brightness directions cancel.
- Existing `setSystemShortcutsEnabled(false)` disables both volume and brightness system chords.
- At 0%, Select + Right restores light; the controller remains active and the LCD is not put to sleep by brightness adjustment.
- Feedback shares the SDK final-presentation overlay transaction. Most recently adjusted visible control wins, shows `Light N%` (compact `L N%` on small displays), and expires after 1.2 seconds. No writes to application/screenshot canvases, no SPI work from the input task.
- Existing full-screen `display.drawCanvas()` and layered `prepareSystemOverlay()/finishSystemOverlay()` integrations inherit brightness feedback. Applications that bypass SDK presentation APIs must integrate those APIs themselves.

## Persistence and boot safety

- Namespace `backlight`, uint32 key `level`, independent of audio settings.
- Hardware updates occur immediately. A 3072-byte-stack settings worker coalesces saves after 600 ms without a changed value. No NVS calls while handling input or holding the hardware mutex; failed writes retry with a 1-second delay. Concurrent later changes remain dirty.
- Missing settings default to 100; invalid high values clamp to 100.
- **Saved 0 loads as 5% on boot**, so a powered-on device does not look dead. Setting 0 during use still fully darkens the backlight.
- PWM is configured during board initialization, before the welcome screen and controller task, not attached by a later application.
- LEDC/task-creation failure restores GPIO46 HIGH and leaves the feature disabled.

## Sleep, audio and peripheral ownership

- Reserves ESP32-S3 low-speed LEDC **timer 3/channel 7**, 20 kHz, 8-bit. Arduino channels **6 and 7** share that timer and must not be reconfigured by other code.
- Keira's GPIO manager and Lua/mJS PWM wrappers reject use of the reserved timer when enabled; wrappers also reject attaching/detaching the backlight pin. Raw application GPIO/LEDC access remains the application's responsibility.
- Board power-saving calls suspend/resume PWM while preserving the requested level. Stock semantics remain unchanged. Those calls sleep the LCD, not the ESP32; independently biased amplifier SD is not controlled by them.
- Brightness never changes I²S pins, clocks or ownership. Continued audio and EMI/flicker still require physical testing.

## Staging-based consumers

Feature branch `feature/independent-backlight` in SDK, Keira, Lilplayer and Doom. SDK brightness is based directly on general SDK `features/stage` at `3566281`. It contains no stage-lilplayer-specific constructor/menu or serial-backpressure changes. Intended integration path: brightness feature -> general stage -> stage-lilplayer merges general stage, preserving its own compatibility changes. The externally added `.idea` ignore is preserved. Consumer branches start from their own staging branches, not the standalone brightness-test branch.

Each consumer adds opt-in environment **`v2-modified-backlight`** with inherited standard flags plus the hardware gate. Lilplayer also adds **`v2-radio-modified-backlight`** for the existing source-radio environment. Default profiles remain stock-safe. Keira/Doom may use the matching SDK feature revision in their sibling `../sdk` checkout. Lilplayer source-radio builds must retain their compatibility SDK branch; brightness reaches that branch through general stage, not by basing the generic brightness feature on stage-lilplayer.

Keira uses its normal launcher, status bar and services. The earlier test intentionally omitted the panel, while AppManager has panel dereferences during resume/feedback presentation: a source-proven unsafe path, plausibly related to the reported intermittent dark startup. The integrated branch does not contain that test startup. This is not a device-confirmed diagnosis of every dark reset.

## Verification

Passed:

- `python3 tests/backlight/run.py`: actual brightness source with host GPIO/LEDC/NVS/task stubs, modified/stock/v1 gates, PWM/task failure fallback, clamp and overflow, zero recovery, save coalescing/retry/concurrent revisions, sleep restoration and chord tests; normal and ASan/UBSan runs.
- `python3 tests/system_shortcuts/run.py`: existing real Controller/Audio regressions and real-font overlay tests; added brightness full-frame/scanline equivalence at 0/50/100 and native-size previews visually inspected.
- `python3 tests/upstream_compatibility.py`. Source-radio compatibility tests were run before the base correction; the compatibility changes belong to stage-lilplayer, not this generic SDK feature.
- Installed ESP32-S3 toolchain/header **syntax-only** checks of changed SDK sources and Keira PWM wrappers with the gate both off/on; no object/link outputs.
- Keira localization script, Doom presentation/volume host regressions, Lilplayer dependency/radio-profile and production UI host regressions.
- Whitespace checks.

`make clang-format` and `make cppcheck` were attempted but tools are absent; these static gates remain unperformed. No tools/dependencies installed. No firmware build, link, packaging, upload or new device test has been performed. Do not treat this source-level result as hardware release certification.
