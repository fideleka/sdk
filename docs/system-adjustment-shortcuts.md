# Global system adjustment shortcuts

The controller initialized by lilka::begin() enables these shortcuts automatically;
no application callback is installed or replaced. Applications can opt out with
lilka::controller.setSystemShortcutsEnabled(false).

- Hold **Select first**, then press **Up / Down**. Up uses +1 below 5, then +5:
  0→1→2→3→4→5→10→15. Down remains -5, clamped at mute.
- One immediate step; first repeat at 400 ms, then every 100 ms. This timing is
  independent of application auto-repeat and emulator per-axis precision delays.
- Volume clamps to 0..100, including actual mute. Opposite simultaneous directions
  cancel their adjustment; each is still consumed.
- A simultaneous Select/direction press, or direction-first press, stays ordinary.
  Select must already have been observed held in an earlier physical scan.
- **Select + Start has priority**, including a raw Start press during debounce.
  It cancels active adjustment repeats for that hold. Select and Start remain
  visible normally, preserving existing pause/exit handling and Select semantics.
- A consumed direction is invisible to getState/peekState and both per-button and
  global callbacks, including its release. Suppression lasts until physical release,
  even if Select releases first, shortcuts are disabled, or Start cancels the hold.
  A fresh direction press works normally afterward.

## Brightness: concrete Lilka v2 hardware limitation

Select-first **Left / Right remain ordinary application controls** and are not
consumed: **brightness adjustment is unavailable on the supported board**. There is no existing SDK
brightness API, independent backlight GPIO, brightness scale, or persisted brightness
key. config.h exposes only LILKA_SLEEP (GPIO46), and
Board::enablePowerSavingMode() / disablePowerSavingMode() explicitly use it to
switch the display backlight **and I2S module together**. PWM of that shared sleep
line would also gate the MAX98357 amplifier; this implementation deliberately does
not invent incompatible PWM, a fake brightness value, or an NVS key. The lower-bound
requirement cannot be implemented as physical dimming with this hardware interface.
A separately controllable backlight (and board-qualified safe minimum) is needed.
The shared sleep/power-saving API is untouched.

## Render-owned centered volume overlay

Audio::stepVolumeShortcut(direction) is the additive shortcut API; positive and
negative direction perform the steps above, zero does nothing. Public
changeVolumeLive(delta) retains exact bounded delta semantics (including +5 from
mute). Step calculation and value/snapshot updates share one RAM critical section,
so a concurrent setter cannot split the gentle-step decision from its update.
Every nonzero shortcut adjustment, even at 0/100, publishes a coherent
VolumeOverlaySnapshot; limit feedback does not dirty NVS or prolong its save timer.
Audio::getVolumeOverlay() reads only RAM. Its visible(now) expires 1200ms after the
last shortcut step and handles unsigned millis wrap. Cancellation does not renew it.

volume_overlay.h exposes reusable geometry and drawVolumeOverlay(target, snapshot,
width, height, now). The panel is centered, 75% of display width and 76px high; its
white border, black background, cyan 22px bar and unscaled regular FONT_10x20 percent/MUTE text (10px advance) are
rotation-independent. Targets smaller than 96x80 are omitted. A private stack-local U8g2 decoder uses the existing
u8g2_font_10x20_t_cyrillic asset and a RAM-only span callback. Scanlines clip
font work to their own row and skip it outside the 20px text band. There are
no heap allocations, LCD font primitives or application font/cursor mutations.
The decoder context is 248 bytes on the 64-bit test host; persistent overlay
state remains unchanged. The existing 6979-byte asset is referenced, not copied.
Embedded flash/stack deltas and device timing are not measured.

Display::drawCanvas automatically protects feedback during complete-screen canvas
presentation. Source canvases are never modified. Keep presenting complete frames
for expiry restoration. Multi-layer owners use prepareSystemOverlay,
presentCanvasOutsideOverlay/clearOutsideOverlay, and finishSystemOverlay. The
opaque panel is excluded from background transfers; its final pixels are rasterized
in bounded RAM scanlines and sent through one LCD window only when changed. Expiry
composes retained source layers and black margins before restoring each panel pixel
once. Display::presentCanvas and legacy drawSystemOverlay are raw helpers and do
not protect arbitrary external writes. Presentation state occupies 624 bytes on
the tested host layout, including 560-byte scratch; there is no heap allocation.
There is no universal task-safe composition/event hook: partial canvases,
drawCanvasInterlaced, raw bitmaps/writePixels, direct drawing, and SDK applications
that stop presenting while paused are not automatically covered. Their single
render owner must schedule presentation and restoration explicitly, using the
snapshot/API. Do not call drawing APIs from controller/audio/settings tasks or add
another SPI-writing task. Matching Keira AppManager implements this scheduler for
menus, NES/GB/GBC, framebuffer apps and paused screens; see its integration docs.

## Audio and persistence

Audio::getVolume() loads the existing unsigned sound/volumeLevel key once
(default 100), then reads RAM. Existing setVolume(int) still writes immediately,
using the same key/type and preserving its existing unrestricted setter behavior.
Startup-sound keys/setters are unchanged. Direct writes to that NVS key outside the
public Audio setter are not reflected until reboot.

The additive Audio::changeVolumeLive(delta) performs bounded RAM-only relative
adjustment after audio initialization. A small critical section protects its value,
revision, and change timestamp; it never does NVS I/O or waits on an NVS mutex.
A separate 2048-byte settings task checks every 50 ms and persists the latest value
after 600 ms without a value change. Successful saves coalesce repeats; changes
concurrent with a save remain dirty; failed writes remain pending with 1s retry backoff. An explicit
public setter supersedes a pending save unless a later RAM change occurs.
Immediate power loss before the settling save can lose the most recent adjustment.

The controller scans ten GPIOs every 5 ms, arbitrates one complete debounced
snapshot, and applies the shortcut step after releasing its controller mutex. It does
not allocate, read NVS, or write NVS during adjustment scanning. Existing application
callbacks remain synchronous as before; applications must avoid heavy work there.
SDK startup PCM rereads RAM volume, and 32-bit scaling uses a widened product to
avoid signed overflow. Arbitrary firmware using raw I2S still needs to call
adjustVolume(buffer, bytes, bits, audio.getVolume()) before writing PCM; the SDK
cannot intercept third-party I2S drivers. Keira's matching integration supplies this
for NES, Game Boy, tracker and AudioPlayer output.

## Source-only regression

Run python3 tests/system_shortcuts/run.py (C++11 normal and ASan/UBSan, including
gentle taps/holds, bounds, descending, opposite cancellation and timeout/wrap) and
python3 tests/menu/run.py --sanitize. Binaries and source mocks live only in a
temporary host directory. Matching Keira tests/volume_overlay.py executes SDK
presentation and Keira render paths under pixel stubs with the real maintained
U8g2 decoder and FONT_10x20 asset (normal, ASan and UBSan), including centered geometry,
bar fractions, source immutability and clean expiry/screenshot restoration.
Host tests use the existing read-only ../lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib;
set U8G2_CLIB (or Keira's --u8g2) to another existing clib directory if needed.
No dependency download or .pio creation occurs.
No PlatformIO, dependency resolution, firmware build,
flash, ROM/save write, or cache creation is involved. Hardware audio, task-stack
headroom, persistence across reboot and release readiness remain device/build gates.
