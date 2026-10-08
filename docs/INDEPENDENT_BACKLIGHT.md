# Independent backlight — first integrated iteration

## Hardware gate

Enable **`-DLILKA_INDEPENDENT_BACKLIGHT=1`** only on modified Lilka v2: the amplifier module's SD header connection must be physically isolated from GPIO46 and independently biased. Stock builds default to zero and retain the original shared backlight/amplifier behaviour. Lilka v1 remains unsupported/no-op even if the flag is supplied.

Anton device-confirmed brightness control in the earlier Keira test after isolating SD and installing the bias resistors. The integrated implementation described here has source/host verification, not new device qualification.

## Application API

Included by `<lilka.h>` and initialized automatically by `lilka::begin()` through `board.begin()`:

```cpp
lilka::brightness.isEnabled();
lilka::brightness.getBrightness();           // Selected brightness, RAM only, 5..100
lilka::brightness.setBrightness(40);         // clamp, apply, deferred save
lilka::brightness.changeBrightnessLive(-5); // bounded relative change
lilka::brightness.stepBrightnessShortcut(1);// +5 and feedback
```

Setters return false on unsupported hardware or failed hardware updates. Disabled builds report 100 without accessing brightness NVS or LEDC. PWM percent is duty cycle, not a calibrated perceptual scale.

## Global controls and feedback

- Hold Select first, then Left/Right: -/+5 percentage points.
- Repeat starts after 400 ms and proceeds every 100 ms, with no catch-up bursts.
- Consumed directions and Select cancellation follow the existing SDK volume arbitration; Start takes priority and opposing brightness directions cancel.
- Existing `setSystemShortcutsEnabled(false)` disables both volume and brightness system chords.
- Selected brightness is clamped to 5..100%; down at 5% remains 5%. Full display-off may still drive PWM to zero, separately from the selected value.
- Feedback shares the SDK final-presentation overlay transaction. Most recently adjusted visible control wins, shows a speaker or sun icon with the numeric percentage, including a crossed speaker at mute, and expires after 1.2 seconds. No writes to application/screenshot canvases, no SPI work from the input task.
- Existing full-screen `display.drawCanvas()` and layered `prepareSystemOverlay()/finishSystemOverlay()` integrations inherit brightness feedback. Applications that bypass SDK presentation APIs must integrate those APIs themselves.

## Persistence and boot safety

- Namespace `backlight`, uint32 key `level`, independent of audio settings.
- Hardware updates occur immediately. A 3072-byte-stack settings worker coalesces saves after 600 ms without a changed value. No NVS calls while handling input or holding the hardware mutex; failed writes retry with a 1-second delay. Concurrent later changes remain dirty.
- Missing settings default to 100; invalid high values clamp to 100.
- **Minimum selected brightness is 5% in RAM and NVS.** Legacy saved 0..4 loads as 5 and is normalized by the deferred worker. Temporary idle dimming never changes the selected or saved brightness.
- PWM is configured during board initialization, before the welcome screen and controller task, not attached by a later application.
- LEDC/task-creation failure restores GPIO46 HIGH and leaves the feature disabled.

## Sleep, audio and peripheral ownership

- Reserves ESP32-S3 low-speed LEDC **timer 3/channel 7**, 20 kHz, 8-bit. Arduino channels **6 and 7** share that timer and must not be reconfigured by other code.
- Keira's GPIO manager and Lua/mJS PWM wrappers reject use of the reserved timer when enabled; wrappers also reject attaching/detaching the backlight pin. Raw application GPIO/LEDC access remains the application's responsibility.
- Board power-saving calls suspend/resume PWM while preserving the requested level. Stock semantics remain unchanged. Those calls sleep the LCD, not the ESP32; independently biased amplifier SD is not controlled by them.
- Brightness never changes I²S pins, clocks or ownership. Continued audio and EMI/flicker still require physical testing.

## Shared Display settings and idle-off

`lilka::displaySettings.begin()` is called by Keira and Lilplayer after `lilka::begin()`. Doom does not opt into automatic idle-off.

- Public NVS constants live in `display_settings.h`: namespace `backlight`, brightness key `level`, off-timeout key `timeoutSeconds`, dim-timeout key `dimSeconds`. Both applications use these exact SDK APIs and keys, not private copies.
- Both timers default to **Never (0)** for missing/invalid settings; valid previously saved choices are retained. Menu presets: Never (0), 30 sec, 1/2/5/10 min. API values are clamped to 0..3600 seconds; invalid stored values fall back to Never.
- `getTimeoutSeconds()` reads RAM; `setTimeoutSeconds()` updates RAM and a separate 3072-byte-stack worker saves after 600 ms quiet, retrying failures. No NVS work on input/render paths.
- Keira: Settings -> Display, brightness, Auto-off and Idle dim controls. Up/Down selects; Left/Right or D/A adjusts. The menu disables its usual horizontal paging so adjustment cannot also switch rows.
- Lilplayer: Settings -> Display with the same three values and controls. English/Ukrainian labels are provided in both applications.
- Automatic off is allowed only on Keira's Launcher, or Lilplayer stopped/paused/finished/error with no load, scan, resume prompt, retry or seek pending. Playing/connecting and foreground Keira applications inhibit the timer. Background services continue; this is not a claim that all RTOS tasks stop.
- Physical input (including held/consumed keys) renews activity. The first wake gesture is consumed until **all** its buttons are released, preventing late chord keys from activating a setting or launching something.
- `serviceIdle(eligible)` runs only on the LCD owner task. It invokes board LCD/backlight sleep/wake, never MCU sleep. Render loops skip LCD transfers while off and repaint after wake; audio, storage and services continue.
- Optional dim uses the same presets and inactivity/foreground rules, but temporarily applies hardware 5% without changing the selected value, NVS or LCD sleep state. A key gesture or a foreground app restores the selected brightness. Full off has priority when its timeout is reached; waking off also clears the temporary dim override. Disabling dim restores brightness. Input arriving during dim application is recovered on the next owner frame.
- The idle comparison treats a concurrently newer input timestamp as future activity, not an unsigned timeout overflow; rollover and the exact timeout boundary are tested.
- Settings-task allocation failure disables auto-off safely and presents unavailable controls/logging. Stock boards can use idle-off while idle; independent PWM remains modified-hardware-only.

## Staging-based consumers

Feature branch `feature/independent-backlight` in SDK, Keira, Lilplayer and Doom. SDK brightness is based directly on general SDK `features/stage` at `3566281`. It contains no stage-lilplayer-specific constructor/menu or serial-backpressure changes. Intended integration path: brightness feature -> general stage -> stage-lilplayer merges general stage, preserving its own compatibility changes. The externally added `.idea` ignore is preserved. Consumer branches start from their own staging branches, not the standalone brightness-test branch.

Each consumer adds opt-in environment **`v2-modified-backlight`** with inherited standard flags plus the hardware gate. Lilplayer also adds **`v2-radio-modified-backlight`** for the existing source-radio environment. Default profiles remain stock-safe. Keira/Doom may use the matching SDK feature revision in their sibling `../sdk` checkout. Lilplayer source-radio builds must retain their compatibility SDK branch; brightness reaches that branch through general stage, not by basing the generic brightness feature on stage-lilplayer.

Keira uses its normal launcher, status bar and services. The earlier test intentionally omitted the panel, while AppManager has panel dereferences during resume/feedback presentation: a source-proven unsafe path, plausibly related to the reported intermittent dark startup. The integrated branch does not contain that test startup. This is not a device-confirmed diagnosis of every dark reset.

## Verification

Passed:

- `python3 tests/backlight/run.py`: actual brightness source with host GPIO/LEDC/NVS/task stubs, modified/stock/v1 gates, PWM/task failure fallback, clamp and overflow, zero recovery, save coalescing/retry/concurrent revisions, sleep restoration and chord tests; normal and ASan/UBSan runs.
- `python3 tests/system_shortcuts/run.py`: real Controller/Audio regressions, compound wake-gesture consumption, and real-font speaker/sun full-frame/scanline equivalence at 0/50/100. Native-size icons and both application Display menus visually inspected (host HAL).
- `python3 tests/display_settings/run.py`: actual shared settings code; inactivity eligibility, boundaries, concurrent newer input, held keys, wake, disabled timeout, persistence/retry/concurrent revisions, allocation failure and rollover, normal and ASan/UBSan.
- `python3 tests/menu/run.py --sanitize`: horizontal-paging opt-out preserves default behaviour and does not page on setting adjustment. The independently reviewed `fix/menu-page-up-safety` commit `60da2f2` is now merged into this brightness branch. Both opt-out and paging-safety suites are retained: 556 checks pass with the sequence-point warning promoted to an error, normally and under ASan/UBSan.
- `python3 tests/upstream_compatibility.py`. Source-radio compatibility tests were run before the base correction; the compatibility changes belong to stage-lilplayer, not this generic SDK feature.
- Installed ESP32-S3 toolchain/header **syntax-only** checks of changed SDK sources and Keira PWM wrappers with the gate both off/on; no object/link outputs.
- Keira localization script, Doom presentation/volume host regressions, Lilplayer dependency/radio-profile and production UI host regressions.
- Whitespace checks.

`make clang-format` and `make cppcheck` were attempted but tools are absent; these static gates remain unperformed. No tools/dependencies installed. No firmware build, link, packaging, upload or new device test has been performed. Do not treat this source-level result as hardware release certification.
