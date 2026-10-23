# Native black-to-first-shade diagnostic

The defect is the sharp jump from black to the first visible shade. The owner
states the original colors are fine. Do not turn this task into color correction.

## What was learned

- The problem occurs in Keira's PNG viewer and direct polling output. PNG/JPEG
  decoding, routing and pixel DMA are not required to reproduce it.
- Reset-default initialization did not resolve it.
- Global GAMSET presets darkened/distorted colors, especially green, with little
  benefit on the reference image. The apparent benefit on a test pattern did not
  carry over. These presets are rejected as a fix.
- The EPF comparison showed a conversion-mode difference, not an accepted color
  defect. The original F0 expansion is preserved. In F0, the green LSB fills the
  missing red/blue bit and permits all 64 neutral gray levels from RGB565 words.
- No exact panel variant or physical defect has been established.

## Scope of this branch

`diagnostic/native-panel-near-black` starts at the unchanged SDK staging revision
`5fa9d59ea5544208510f5dec7dcff777fb12a64b`. It does not inherit the reset-default
initialization, global gamma-preset UI or alternative color-expansion mode.
Original `Display::begin`, GFX initialization, inversion, pixel format, F0,
brightness and the **80-MHz native clock** remain unchanged. No supply-voltage,
external-display or NVS setting is changed. The only startup addition is a
conditional native test after backlight resume, before application/idle drawing.
External-only output skips the test.

## Why these two fields

Sitronix ST7789V2 v1.0 p.144 and ST7789V3 preliminary v0.0 p.105 describe the
first gray-level source voltages through V1P/V1N and V2P/V2N, in E0/E1.
The formula for the first positive interval is:

`VP0 - VP1 = (VAP - VBP) * (1 + V1P - V0P) / 129`.

With the original table's V0=0 and V1=9, this is 10/129 of the reference span.
With V1=0, it is 1/129. The corresponding second point changes from 20/129 to
3/129 of the span when V2 changes from 19 to 2. These are **voltage ratios**, not
measured light output or a guarantee of suitable panel calibration. Both versions
use the same relevant formulas and field layout; actual installed variant is
still unknown.

The test rewrites the original 14-byte positive and negative tables but changes
only indices 1 and 2:

| Condition | V1P/V1N | V2P/V2N | All other table bytes |
|---|---|---|---|
| 1: original | 09 | 13 | Original GFX 1.6.0 values |
| 2: test | 00 | 02 | Exact same original values |

V2 is an input to interpolation for other dark points below level 20. Therefore
this is not a claim that only pixels with codes 1/2 can change. The other register
parameters and the level 20-and-higher voltage anchors remain unchanged. This
specifically tests the near-black portion instead of replacing a full curve.
No register readback or analog panel-voltage measurement has been performed.

## Owner test — existing automation only

```powershell
.\build-all.cmd --interactive --keira-branch features/stage --sdk-branch diagnostic/native-panel-near-black --no-drivers
```

Press N, then 1, then Enter to select only Keira; accept the existing upload prompt.
At boot the upper row contains neutral native levels 0..15 and the lower row
contains levels 0..63. The large centered header number is clear of rounded corners.
The pixel words are identical between conditions and are written only once.

- **A:** toggle 1 (original) and 2 (near-black test), without a timeout.
- **B:** restore the exact original E0/E1 tables and continue into Keira.
- **C:** retain the selected tables in Keira for this boot and continue.

Compare whether the first visible level comes closer to black while remaining
distinguishable. Check levels 2 and above as well: moving the jump to another
step, making all shadows black, or introducing a color problem is not success.
If condition 2 helps, press C and open the same `shadow-reference.png` at unchanged
brightness. Do not infer success solely from a darker image. Rebooting or another
firmware's initialization restores the original setup; nothing is saved.

## Source verification

- `python3 -B tests/native_black_step/run.py`: six ASan/UBSan recorder cases.
  Both orientations, input edge/hold/toggle behavior, B precedence/restoration,
  C retention, centered labels, complete pattern coverage, unchanged input words,
  16/64 neutral frame-RAM levels under F0 and inactive-output skip.
- The command recorder permits only E0/E1 with exactly 14 bytes. It checks every
  byte: only V1/V2 may differ, and B restores all 28 original bytes.
- The two original arrays were independently compared against the installed
  pinned GFX initialization header: all 28 bytes match exactly.
- `python3 -B tests/display_startup/run.py`: 48 startup cases with/without the hook.
- Real ESP32-S3 syntax-only checks pass for display.cpp and SDK startup.
- Changed production files pass clang-format 20.1.2 and scoped cppcheck with the
  previously documented inherited-member/volume-overlay findings suppressed.
- The two source-framebuffer previews were visually inspected at native size.
  They verify layout and neutral source codes, not physical luminance.
- Full repository checks retain exactly the same 143 formatting diagnostics and
  12 normalized static-analysis findings as the unchanged base.
- No assistant firmware build, packaging, flash or physical success claim.

References: [V2 v1.0](https://files.waveshare.com/upload/c/c9/ST7789V2.pdf),
[V3 preliminary v0.0](https://dl.sipeed.com/fileList/MAIX/HDK/Chip_DS/ST7789V3_SPEC_Preliminary_V0.0_200102.pdf).
