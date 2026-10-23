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
- The first targeted test (d80364f) changed V1/V2 and the owner observed changes
  near white, not near black. This establishes the wrong visible endpoint was
  targeted. It does not show that near-black calibration cannot affect the defect.
- The existing IPS configuration sends INVON; its optical endpoint mapping must
  not be assumed from gamma register indices. Keep inversion unchanged and use
  the observed endpoint response to choose the next controls.
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

## Why these two fields — corrected endpoint

The owner observed that V1/V2 changed highlights. The active comparison therefore
leaves V1/V2 at their original values and tests the opposite end: V61P/N and
V62P/N, while preserving the V63 endpoint itself. This correction is based on the
device result, not an assumption that a low gamma-table index means dark.

Sitronix ST7789V2 v1.0 pp.145–146 and ST7789V3 preliminary v0.0 pp.106–107 give:

- `VP63 = (VAP - VBP) * (23 - V63P) / 129 + VBP`
- `VP62 = (VAP - VBP) * (64 - V62P) / 129 + VBP`
- `VP61 = (VAP - VBP) * (64 - V61P) / 129 + VBP`

The negative polarity has matching interval magnitudes. With original V63=15,
the endpoint coefficient is 8. Original V62=33 and V61=29 give coefficients 31
and 35: gaps of 23/129 and 27/129 of the reference span. The test uses V62=55 and
V61=53, giving coefficients 9 and 11: gaps of 1/129 and 3/129. The black endpoint
and all supply/reference settings remain fixed.

These are **voltage ratios**, not measured light output or proof of a suitable
calibration. Both V2 and V3 documents give these formulas and field layouts;
the exact installed variant remains unknown.

Only indices 12 and 13 of each original 14-byte table change:

| Condition | V61P/V61N | V62P/V62N | Remaining 24 table bytes |
|---|---|---|---|
| 1: original | `1D` (29) | `21` (33) | Original GFX 1.6.0 values |
| 2: test | `35` (53) | `37` (55) | Exact same original values |

V61 contributes to interpolated points between levels 43 and 61. Thus nearby
shadows can change too; this is not a promise that only two pixel values change.
V63 and the low-index/white-side controls remain unchanged. V1/V2 are explicitly
restored to the original 09/13 in both conditions; no part of the previous
white-end test remains active. No register readback or analog voltage measurement
has been performed.

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
  byte: only V61/V62 may differ, and B restores all 28 original bytes. It also
  checks fixed V63/white-side values and ordered endpoint voltage coefficients.
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
