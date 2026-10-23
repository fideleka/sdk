# Native ST7789 tonal-response diagnostic

**Isolated experimental source branch, not a verified fix.** The normal SDK
checkout, staging refs and external ILI9341 driver are unchanged. This branch
is published for owner-built device tests; no assistant firmware build or flash.

## Current test: A-controlled gamma presets

The owner’s EPF comparison photo shows reduced alternating tint in the E0
green row, but the large initial brightness jump persists in both halves.
Treat the conversion issue separately; the main tonal defect remains unresolved.

Owner result with GAMSET: presets 2 and 4 improve the transition; 4 is best but
the darkest overall. This is evidence of an output-curve effect, not proof of a
complete fix or measured panel gamma. Check retained detail, not darkness alone.

The current diagnostic draws one unchanged raw RGB565 pattern, with a visible
larger preset number centered in a 28-pixel black header, away from the rounded
corners. **A cycles; B restores and exits; C keeps the selection and exits.** There
is no timeout or automatic advancement. Holding A does not repeat. The order is
1 → 2 → 3 → 4 → 1, and each curve stays active until another button press.

| Number | GAMSET parameter | Datasheet nominal curve |
|---|---|---|
| 1 | `01` | 2.2 (reset-default selector) |
| 2 | `02` | 1.8 |
| 3 | `04` | 2.5 |
| 4 | `08` | 1.0 |

Both ST7789V2 v1.0 p.194 and ST7789V3 preliminary v0.0 p.153 document these
standard GAMSET (26h) masks. These are controller preset names, not measured
panel gamma. The test does not write another module’s E0/E1 analog calibration
or alter voltage, inversion, brightness, pixel format, or the **80-MHz clock**.
F0 expansion stays fixed throughout, so only the gamma selector changes.

The dark/full-gray/red/green/blue pixel pattern is written once through the
native polling bus. Later stages change only GAMSET and the number in the header.
No decoded media, routing or pixel DMA is used. On B, the test restores selector
01 and F0, clears black, and continues into Keira. No NVS settings are written.
On C, it does not restore the gamma selector: the chosen curve remains active
in Keira for this boot. It still restores F0 and clears the diagnostic pattern.
The next panel reinitialization resets the selection; no persistent key exists.
Launching a separately built Doom firmware may reinitialize the panel, so this
Keira test does not prove the curve will remain active in Doom.
The external screen is not drawn to. External-only output skips the test.

### Exact command and observations

```powershell
.\build-all.cmd --interactive --keira-branch features/stage --sdk-branch diagnostic/native-panel-defaults --no-drivers
```

Select only Keira with N, then 1, then Enter; accept its existing upload prompt.
At startup use A to select 4, then C to enter Keira with curve 4 active. Open
`shadow-reference.png` from SD and check whether the first dark steps separate
better without losing the remaining shadow detail. Keep brightness and viewing
conditions unchanged. B instead exits with the original selector restored.
The labels now sit at the top center, not under the rounded corner. Nothing is
saved, so restarting repeats the diagnostic from preset 1.

If curves visibly change the image, curve selection affects the panel output;
judge the first step separately from midtone changes. If none changes it,
register acceptance or factory preset programming remains unknown: a visual
no-change alone is not a measured register readback or proof of panel damage.

### Current verification

The production-method host recorder tests both orientations, inactive output,
immediate B exit and C retention of preset4. It checks initial held-A consumption, no hold-repeat,
1→2→3→4→1 wrap, B priority over simultaneous A/C, correct centered labels, unchanged
pattern words between selections, exact GAMSET masks, fixed F0, and restoration
of selector01/F0/black on exit. Input polling releases SPI and yields between
reads; there is no new worker, heap allocation or settings write.

All 59 host cases pass (48 startup, five initialization, six interactive
framebuffer cases). The ESP32-S3 display translation unit passes real-header
syntax-only checking. Changed production files pass clang-format and scoped
cppcheck with the documented existing findings suppressed. All four numbered
source-framebuffer previews were inspected; this verifies layout, not the
physical gamma response. Full repository checks retain the same 143 formatting
diagnostics and 12 normalized static-analysis findings as the baseline. No
firmware build or device-button test was performed.

The following sections retain the reference investigation and earlier tests as
history; their timed/stacked pattern procedures are superseded by this section.

## Evidence and scope

Anton supplied Keira PNG photographs on 2026-10-23. The internal screen jumps
sharply from black to the first nonzero gray step, with weak separation across
the remaining dark steps. The external screen shows a gradual ramp. Lilvideo
video had the corresponding apparent black/visible transition. Tilt and maximum
brightness did not restore the missing tonal separation.

The PNG comparison establishes that the symptom is not exclusive to Lilvideo,
MJPEG or streaming. Host checks preserve all 65,536 RGB565 words through the
native/external byte conversion; they do not verify the physical SPI wire or
controller register state. No specific bad register or hardware defect is proven.

## Reference findings

References were read on 2026-10-23:

1. [Lilka BOM supplier page](https://arduino.ua/prod6568-tft-displei-1-7-spi-240x280-rgb)
   currently calls its 1.69-inch 240×280 module **ST7789V3**. This does not identify
   the chip in Anton's particular unit; the hardware CAD uses a V2-named model.
2. Sitronix [ST7789V2, version 1.0, 2016/11](https://files.waveshare.com/upload/c/c9/ST7789V2.pdf):
   gamma controls E0/E1, pp. 297–300; four-wire SPI timing, p. 44.
3. Sitronix [ST7789V3 preliminary version 0.0, 2020/01](https://dl.sipeed.com/fileList/MAIX/HDK/Chip_DS/ST7789V3_SPEC_Preliminary_V0.0_200102.pdf):
   gamma formulas, pp. 105–107; E0/E1, pp. 255–258. This preliminary document lists
   its serial timing values as TBD, so it cannot establish a numerical V3 limit.
4. [Waveshare 1.69-inch module](https://www.waveshare.com/wiki/1.69inch_LCD_Module),
   identified as V2, and its linked [Raspberry Pi reference archive](https://files.waveshare.com/upload/8/8d/LCD_Module_RPI_code.zip),
   `RaspberryPi/c/lib/LCD/LCD_1in69.c` and `python/lib/LCD_1inch69.py`.
5. [Adafruit ST7789 source](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST7789.cpp)
   uses a minimal reset/interface/display sequence without E0/E1 or analog-drive
   overrides. Its code explicitly supports centered 1.69-inch geometry.
6. Current [Arduino_GFX ST7789 source](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST7789.h)
   now offers multiple initialization types, including a minimal type 3. The
   pinned 1.6.0 dependency lacks that constructor option; no upgrade is needed
   for the isolated override below.

The installed GFX 1.6.0 sequence uses fixed gamma and drive values. These differ
from both the V3 controller's listed defaults and the currently downloaded
Waveshare module sample:

| Field | Installed GFX | V3 documented default | Waveshare sample |
|---|---|---|---|
| E0 V0P | `00` | `00` | `00` |
| E0 V1P | `09` | `00` | `06` |
| E0 V2P | `13` | `02` | `0B` |
| E1 V0N | `00` | `00` | `00` |
| E1 V1N | `09` | `00` | `04` |
| E1 V2N | `13` | `02` | `08` |
| BB VCOM | `19` | Not used as a calibration recommendation | `35` |
| C3 VRH | `12` | Not used as a calibration recommendation | `0D` |

The V3 formula makes the V0-to-V1 positive voltage interval proportional to
`(1 + V1P - V0P) / 129`. With these fields, that is `10/129` for the current
table versus `1/129` for the listed V3 default, relative to the positive gamma
reference span. This is a **voltage-interval comparison, not a luminance ratio**
or proof of the installed panel's required calibration. It gives a concrete
reason to test initialization rather than alter decoded media colors.

RGB565 mode `COLMOD=55` is valid. Waveshare uses `05`; that does not make `55`
incorrect for the MCU/SPI interface. RAMCTRL `00 F0` also matches documented
defaults. Do not substitute a byte-order or color-format change without evidence.

Both datasheets specify two parameters for C2 (`CMDEN`, `FF`), while GFX and the
Waveshare example send only `01`. The reset default is already `01 FF`. This
protocol discrepancy alone is not sufficient evidence for this symptom; the
diagnostic avoids changing C2 entirely.

## Branch usage

This diagnostic branch enables reset-default initialization for the internal
v2 ST7789. Use this SDK checkout as Keira's sibling SDK dependency. No extra
build flags are required. Build and install Keira through the existing workflow.
No firmware build or device test was performed here.

The native display stays at **80 MHz**, exactly as on the base branch.
The existing breadboard profile remains unchanged. The external display is unchanged.
There is no added SPI-clock override.

The only experiment is initialization: software reset, sleep-out, RGB565,
existing IPS inversion, normal mode, display-on, and unchanged rotation/offsets.
Gamma and analog drive stay at reset defaults. No persistent settings are added.

Compare the existing firmware with this branch using the same grayscale PNG,
brightness and orientation. An improvement implicates the initialization profile,
not gamma alone. The cause remains unverified until the device test.

Set `LILKA_NATIVE_PANEL_RESET_DEFAULTS=0` to compile the original initialization,
or return to the original SDK branch.

## Verification and branch status

`python3 -B tests/native_panel_init/run.py` checks startup commands, ordering,
delays, the unchanged 80-MHz native clock, disabled diagnostic behavior,
breadboard behavior and v1 compatibility under ASan/UBSan.

Changed production files pass clang-format 20.1.2. Static analysis of display.cpp
reports the same existing volume_overlay.h unreadVariable warning on this branch
and its unchanged base. Earlier full-repository checks also failed on the base:
formatting in six unchanged files and existing static-analysis findings.
These failures are not repaired or represented as passing here. This is an
experimental source branch, not a production-ready release. Publication proceeds
under the owner's instruction to continue preparing the isolated branch rather
than stop for unrelated baseline fixes.

No firmware was built or flashed. The active SDK checkout and staging refs remain
unchanged.

## Previous direct raw-pattern follow-up (superseded by EPF A/B below)

Owner result with the original reset-default diagnostic: no visible change;
black still jumps to light stripes. Do not call the original gamma table the
confirmed cause or ask for the same PNG comparison again without a new test.

The branch now enables `LILKA_NATIVE_PANEL_RAW_DIAGNOSTIC=1`. After normal
backlight resume, but before application drawing and idle dimming start, it
shows five horizontal rows for 20 seconds, then clears and continues into Keira.
Rows run from dark to light, left to right:

1. Sixteen dark RGB8 levels: 0, 4, 8, ... 60, quantized to RGB565.
2. Thirty-two grayscale levels: RGB8 0, 8, 16, ... 248.
3. Thirty-two red levels, R5=0..31.
4. Thirty-two green levels, G6=0,2,...62.
5. Thirty-two blue levels, B5=0..31.

The unequal RGB565 channel precision can tint the darkest nominal grayscale
steps; this test does not promise continuous or perfectly neutral gradients.
Look for an abnormally large first jump, not invisible quantization boundaries.

The pattern uses native `writeAddrWindow` and polling `writeRepeat` directly.
It bypasses PNG/JPEG decoding, retained canvases, output routing and pixel DMA.
It retains the reset-default initialization and 80-MHz clock from the preceding
test, so changing initialization is not an additional variable in this follow-up.
No brightness level, stored settings, external driver, app code or staging ref
is changed. If native output is inactive, the test logs a skip and does nothing;
use the existing Internal or Both output selection, not External-only.

If the raw top row has the same black-to-light jump, the defect also exists
without the viewer, routing or pixel-DMA path. That narrows investigation to the
remaining common path: polling transfer, controller configuration/response and
panel electronics. It is not proof of panel damage. If the raw pattern separates
dark values but the PNG does not, investigate the skipped pixel paths next.
If the boot pattern never appears, do not interpret the test as a negative;
verify the selected output and the runner's recorded SDK commit first.

### Exact automated handoff

From the folder containing the existing Windows runner:

```powershell
.\build-all.cmd --interactive --keira-branch features/stage --sdk-branch diagnostic/native-panel-defaults --no-drivers
```

Press N, then 1, then Enter to select only Keira. Use the existing upload prompt.
Photograph the five-row boot pattern during its 20-second display. No PNG is
needed for the raw test. Normal startup resumes automatically.

### Follow-up verification

- `tests/native_panel_init/raw_pattern.py`: production-method framebuffer recorder
  under ASan/UBSan checks both orientations, full single-coverage tiling, all
  16/32 distinct words, native virtual-dispatch bypass, black restoration and
  inactive-output skip.
- `tests/display_startup/run.py`: 48 startup cases with/without the raw hook;
  the diagnostic starts only after backlight resume.
- `tests/native_panel_init/run.py`: five original initialization cases.
- Generated framebuffer preview inspected at native resolution; this verifies
  pattern geometry/content, not the physical panel's tonal response.
- Changed production files pass clang-format 20.1.2. Changed display.cpp cppcheck
  passes with the existing inherited-member and volume-overlay findings suppressed.
- ESP32-S3 syntax-only check against installed real headers passes for display.cpp
  and the SDK startup translation unit.
- Full repository checks retain exactly 143 formatting diagnostics and the same
  12 normalized static-analysis findings as the base; no unrelated fixes included.
- No firmware build or flash.

## Previous test: RGB565 expansion A/B (EPF)

The first raw-pattern test retained the leap above black. Anton subsequently
checked Doom and no longer considers the apparent tonal improvement reliable.
The cause remains unresolved; no improvement is claimed.

A more specific reference lead is RAMCTRL (B0h), EPF[1:0]. The installed GFX
sequence writes `00 F0`; controller reset defaults are also `00 F0`. Thus the
original-versus-reset-default test did not distinguish this conversion setting.

The ST7789 converts 16-bit input pixels to 18-bit panel RAM. EPF=11 can use the
least-significant green bit as the missing red/blue bit. A documented ST7789V
case found unwanted gray on odd green levels and fixed it by changing EPF.
Maintained TFT_eSPI explicitly uses `00 E0` (EPF=10), copying each channel's own
most-significant bit instead. This is a concrete candidate, not proof that it
explains Anton's larger jump or identifies his controller variant.

Sources:

- [ST7789V3 preliminary v0.0, RAMCTRL pp. 219–221](https://dl.sipeed.com/fileList/MAIX/HDK/Chip_DS/ST7789V3_SPEC_Preliminary_V0.0_200102.pdf).
- [Reported odd-green issue and solution, post 13](https://forum.arduino.cc/t/problem-with-st7789v-displays-green-value-1-and-every-other-uneven-is-gray/700584/13).
- [TFT_eSPI ST7789 initialization](https://github.com/Bodmer/TFT_eSPI/blob/master/TFT_Drivers/ST7789_Init.h),
  main sequence RAMCTRL `00 E0`. The forum's author initially selected `00 C0`;
  this test selects the maintained driver's `00 E0`, not a copied voltage table.

### What this branch now shows

For 20 seconds at startup the internal screen shows **two stacked copies of
the same five-row pattern**:

- **Upper half: A, RAMCTRL `00 F0`**, the existing/default conversion.
- **Lower half: B, RAMCTRL `00 E0`**, the alternative conversion.

Each half has dark gray, full gray, red, green and blue rows. Corresponding
source RGB565 words are identical. Green now steps through **0..31 by one**,
including odd levels omitted by the previous full-green ramp. Only bit4 of
RAMCTRL's second parameter differs. Endianness, interface selection, gamma,
drive values, brightness and the **80-MHz clock remain unchanged**.

EPF converts incoming data when it is written to frame RAM. This permits both
conversions in one captured frame. Before normal Keira startup the test restores
`00 F0` and clears black. The alternative does **not** remain active in Doom or
normal Keira use. No NVS values are written. External output remains untouched.
If native output is inactive, the test skips without sending these commands.

Use the exact automated command in the preceding handoff section, select only
Keira, and photograph the **whole screen** during the 20-second A/B display.
Compare the first dark steps in the top and bottom halves and odd green levels.
No separate flashing or manual branch switching is required.

### Interpretation

- B improves the dark steps: EPF conversion contributes to the symptom; determine
  whether it corrects the full symptom before promoting a production change.
- B only removes tint/alternating odd-green steps: this is a separate conversion
  issue; the large black-to-light jump still needs investigation.
- A and B look the same: the test shows no visible benefit from the alternative;
  register writes may also be unavailable on some module configurations, so a
  no-change result alone does not prove which EPF mode the panel accepted.

### Verification

The host recorder exercises the production method for both orientations and
inactive output. It checks identical input words between halves, per-pixel
F0/E0 write-time mode, exact `F0 → E0 → F0` command sequence, full coverage,
16/32 distinct source levels and black restoration. It does not simulate analog
panel brightness or prove physical register acceptance. The source-framebuffer
preview verifies tiling only, not the predicted color difference on glass.

EPF A/B verification: all 56 host cases pass; the real ESP32-S3 display
translation unit passes syntax-only checking. The changed production file
passes clang-format 20.1.2 and scoped cppcheck with the same documented baseline
suppressions. The two-half source pattern was visually inspected. Full checks
retain the 143 baseline formatting diagnostics; the same 12 normalized static-analysis findings remain on both revisions. Firmware build, physical register acceptance and device
appearance remain unverified until the owner runs the automated test.
