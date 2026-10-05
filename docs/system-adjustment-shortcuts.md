# Global system adjustment shortcuts

The controller initialized by lilka::begin() enables these shortcuts automatically;
no application callback is installed or replaced. Applications can opt out with
lilka::controller.setSystemShortcutsEnabled(false).

- Hold **Select first**, then press **Up / Down**. Up uses +1 below 5, then +5:
  0→1→2→3→4→5→10→15. Down uses -1 at current volume 4 or below, otherwise -5, clamped at mute:
  10→5→0 and 4→3→2→1→0 (5 does not descend to 4).
- One immediate step; first repeat at 400 ms, then every 100 ms. This timing is
  independent of application auto-repeat and emulator per-axis precision delays.
- Volume clamps to 0..100, including actual mute. Opposite simultaneous directions
  cancel their adjustment; each is still consumed.
- A simultaneous Select/direction press, or direction-first press, stays ordinary.
  Select must already have been observed held in an earlier physical scan,
  and both raw and debounced Select must still be held on capture. Raw Select
  release stops repeats immediately, even before its debounced release.
  Suppression/cancellation never serves as physical history.
- **Select + Start has priority**, including a raw Start press during debounce.
  It cancels active adjustment repeats for that hold. Start remains visible;
  higher-priority pause/exit consumers use State::selectHeld even after volume
  has consumed Select.
- A consumed direction is invisible to getState/peekState and both per-button and
  global callbacks, including its release. Suppression lasts until physical release,
  even if Select releases first, shortcuts are disabled, or Start cancels the hold.
  A fresh direction press works normally afterward.
- Plain Select presses are still immediate, with ordinary release semantics and
  no new delay. Once a volume chord captures, Select becomes invisible for the
  remainder of its hold and release, with pending press/release/repeat flags cleared.
  Its already-delivered callback press is paired with exactly one false callback
  at capture; peekState() in that callback sees the complete canceled snapshot.
  State::selectConsumed remains true through release and getState/resetState until
  the next physical Select press. Deferred-release consumers must cancel pending
  Select actions on this flag, not synthesize a tap from !select.pressed.
  State::selectHeld exposes the debounced physical modifier for Select+Start only.
  Existing callback signatures and ordinary controls are unchanged; callbacks cannot
  undo actions that an application already executed on the initial Select press.

## Raw Select diagnostic boundary

On v2 Select is active-low GPIO0, configured INPUT_PULLUP by Controller::begin.
Boot/launch/menu Up or Down with raw Select high must never adjust volume; host
tests cover boot-only navigation, stale debounced Select during raw release, and
fresh navigation after shortcut release. A launch-held or electrically stuck-low
GPIO0 is still indistinguishable from a real held Select in software. If a WAD
picker adjusts volume with the physical button released, measure/log raw GPIO0
(high when released, low when pressed) and compare State::selectHeld; inspect
switch/wiring/pull-up/pin ownership and actual flashed SDK selection. Host masks
cannot prove GPIO health or the deployed binary. No Doom workaround, shortcut
disabling, or fabricated GPIO-fault fix is applied.

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
white border, black background, cyan 22px bar and unscaled regular FONT_10x20 percent/localized mute text (10px advance) are
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
not protect arbitrary external writes. Presentation state occupies 656 bytes on
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
gentle taps/holds, bounds, descending, Select cancellation, opposite cancellation and timeout/wrap)
and matching Keira python3 tests/volume_select.py --sdk ../sdk (actual Controller,
NES OSD, GB modifier/mapping, shared menu update; both release orders, delayed
readers, next tap and Start priority) and
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

### Localized mute label

VolumeOverlaySnapshot owns a 32-byte muteLabel array, defaulting to normal-case
"Mute" for standalone SDK apps. Supply at most 31 UTF-8 bytes plus NUL in a copied
snapshot before prepareSystemOverlay; no pointer lifetime dependency, heap, NVS,
or application font/cursor changes. Keira uses K_S_VOLUME_MUTE through its existing
compile-time keira_lang.h selector: default/LANG_UK "Без звуку", LANG_EN "Mute".
The existing regular Cyrillic FONT_10x20 asset decodes BMP UTF-8 and centers using
actual glyph advances. Invalid/truncated/non-BMP sequences stop safely, unsupported
font glyphs retain U8g2's behavior, and over-wide labels clip inside the panel.
Label-only changes at mute repaint even with unchanged volume; they never renew
the 1200ms timeout. Non-mute percentage rendering is unaffected. The owned label
adds 32 bytes per snapshot; persistent host Display state is 656 bytes (was 624).
No firmware size, device stack watermark or SPI timing is claimed.
