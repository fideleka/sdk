# Global system adjustment shortcuts

The controller initialized by lilka::begin() enables these shortcuts automatically;
no application callback is installed or replaced. Applications can opt out with
lilka::controller.setSystemShortcutsEnabled(false).

- Hold **Select first**, then press **Up / Down** for volume +5 / -5 percentage points.
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
The shared sleep/power-saving API and display drawing are untouched.

There is no universal task-safe display compositing hook, so no overlay is drawn
from the controller or settings tasks.

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
snapshot, and applies the RAM delta after releasing its controller mutex. It does
not allocate, read NVS, or write NVS during adjustment scanning. Existing application
callbacks remain synchronous as before; applications must avoid heavy work there.
SDK startup PCM rereads RAM volume, and 32-bit scaling uses a widened product to
avoid signed overflow. Arbitrary firmware using raw I2S still needs to call
adjustVolume(buffer, bytes, bits, audio.getVolume()) before writing PCM; the SDK
cannot intercept third-party I2S drivers. Keira's matching integration supplies this
for NES, Game Boy, tracker and AudioPlayer output.

## Source-only regression

Run python3 tests/system_shortcuts/run.py (C++11 normal and ASan/UBSan) and
python3 tests/menu/run.py --sanitize. Binaries and source mocks live only in a
temporary host directory. No PlatformIO, dependency resolution, firmware build,
flash, ROM/save write, or cache creation is involved. Hardware audio, task-stack
headroom, persistence across reboot and release readiness remain device/build gates.
