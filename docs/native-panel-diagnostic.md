# Native ST7789 tonal-response diagnostic

**Source-only diagnostic draft, not a verified fix.** Normal SDK behavior is
unchanged. No firmware was built, packaged or flashed, and no staging/remote ref
was advanced. The external ILI9341 driver is unchanged.

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
