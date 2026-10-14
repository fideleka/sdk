SD display drivers and routing
=============================

On Lilka v2, ``lilka::begin()`` discovers bounded, freestanding display modules
in ``/sd/drivers/<package>/`` and starts the SDK-owned display worker. Keira,
Doom, LilOBD, Lilplayer and LaserTank use the same registry, settings and driver
ABI. After installing this SDK integration in each firmware once, a compatible
new display module needs only an SD-card package and restart, not a firmware
recompile. This is an SPI/RGB565 display ABI, not arbitrary peripheral support.

Packages contain ``manifest.json`` and ``driver.so``. The canonical C ABI is
``lib/lilka/src/lilka/device_driver.h``; example/controller-specific drawing,
rotation, margin clearing, initialization and panel power live in
``drivers/ili9341/driver.c``. Build packages with ``tools/build_drivers.py``.
Modules declare their own stable setting IDs, limits, choice labels and schema;
Keira's Settings -> Devices renders them generically. Drivers default disabled.
Only explicitly enabled packages may claim hardware. Hold Select at boot to
skip all module activation. Invalid/missing modules leave the native LCD usable.

ABI-1 modules and old eight-field device records remain accepted. Existing
UUIDs, settings schema, SPI/pin choices and enabled preference are retained.
A larger twelve-field record is not readable by older firmware, which may fall
back to device defaults. Drivers have bounded ELF/heap/state budgets and no
external imports; an SD module is trusted native code, not a sandboxed program.

Modes and presentation
----------------------

The saved display mode is restored after reset. Without a saved choice the mode
is **Internal**. Hold Select at boot to skip SD drivers and recover Internal mode.
The external panel is initialized black/off before a complete frame is presented.
**Select + A** cycles Internal -> External -> Both -> Internal. Hold Select first,
then press A.
There is one action per press, no held-button repeating, and consumed keys are
not sent to the game. Select + Start and wake-gesture suppression retain priority.
The input scanner queues the request; SPI, allocation and NVS run off that task.

``lilka::devices`` exposes device/settings/mode APIs. ``Display::drawCanvas``
and the SDK graphics paths route automatically. Direct/incremental renderers
must bracket each completed logical frame on their existing LCD owner::

    lilka::DisplayFrame frame(lilka::display);
    // Draw rectangles, text, RGB565 bitmaps, or streamed scanlines here.
    // Scope exit publishes one completed frame, including on early return.

Doom brackets its scanline owner; LilOBD brackets its UI loop; Lilplayer keeps
the scope inside its existing bus lock; LaserTank services retained canvases.
Mode changes restore retained native pixels on that owner, never from the
external worker. Native SPI begin/end routing is latched for the transaction. Native panel
scanout is switched on the LCD owner, so stock External-only mode does not
leave the previous internal image visible or PWM the amplifier-shared pin.
Raw writes to an application's privately obtained hardware bus bypass this API.

Keira supplies locked, completed app/status layers directly via
``devices.capture()`` and ``presentInternal()``; it disables the redundant
standalone retained canvas with ``display.setLayeredPresentation(true)``.
Its app scheduling and choice of layers remain application responsibilities.

Frames are copied, not borrowed from mutable emulation canvases. Two leased
PSRAM presentation buffers retain latest-wins updates and accumulated dirty rows;
the worker never mutates an in-flight source. Standalone incremental drawing
also retains one logical canvas, allocated only when a valid module exists.
Keira does not retain that third canvas. The driver sends changed row bands,
with the existing coverage/band-count full-update fallback. There is no forced
whole-frame mode and no claim that software buffering eliminates panel tearing.

Brightness and idle-off
-----------------------

The existing selected brightness, Select + Left/Right, dim and idle-off settings
control every active **configured** BLK output. Inactive output backlights stay
off; dim/sleep do not overwrite the selected brightness. Wake restores the mode
and selected level and consumes the complete waking gesture. Background audio
and emulation need not stop. ``DisplaySettings::begin()`` runs in SDK startup;
frame scopes service idle timers on the existing LCD/SPI owner. Retained menus
must keep servicing frame scopes even when their pixels do not change.

For the tested ILI9341 module, GPIO47/P4 connects through 220 ohms to BLK, with
common ground. In Settings -> Devices -> ILI9341 select **Backlight pin -> GPIO47** (open drain). The safe default is Disconnected. A stock v2 firmware may
control this external pin without PWM-driving the amplifier-shared internal
pin. Independent native backlight dim/off still requires the independent-backlight
hardware modification and existing build flag; stock native scanout can be
switched off without switching the amplifier/backlight supply. Open drain sinks LOW or releases HIGH;
it never drives the module's transistor-base input high.

ILI9341 module 1.1.6 clears all 240x320 GRAM pixels before enabling scanout.
A configured BLK is held off before reset/initialization. A module with no BLK
connection cannot have its physical LEDs switched off by software, although its
LCD scanout can be off/black. Firmware cannot guarantee darkness during the
power-on/reset interval before GPIO ownership; that needs appropriate hardware
biasing. Disabled/unknown modules are never touched merely to blank a screen.

ILI9341 fixed-image SPI diagnostic (1.1.7)
----------------------------------------

In Keira, use Settings -> Devices -> ILI9341 -> **SPI test**:

* **Off** (default): normal application frames and partial updates.
* **Fixed image + writes**: each pending application update rewrites the same
  full-panel color bars, gray checkerboard and alternating gray lines. This is
  a deliberate transfer stress test, not a normal refresh/performance mode.
* **Fixed image, hold**: draw that identical pattern once, then acknowledge
  subsequent application frames without sending SPI commands or pixels, even
  if source geometry changes. Panel scanout and BLK stay controlled normally.

Use **Both** mode so the internal display remains interactive. Set brightness
100%, idle dim Never and auto-off Never before comparing. Leave panel drive,
SPI speed, wiring and other settings unchanged. In the write mode, move the
cursor repeatedly on the internal screen to generate changed frames; a static
application otherwise need not produce any transfers. In hold mode repeat the
same cursor movements, then leave the unit untouched for 20-30 seconds. Judge
with your eyes; camera aliasing cannot measure the true shimmer frequency.

Settings changes reset/reinitialize the panel, so disregard that brief transition.
Do not switch output mode, change device settings, sleep/wake or restart during
an observation: those operations legitimately send initialization/power commands.
Hold suppresses this driver's frame writes, not every other bus or CPU activity.
It does not turn off the controller, detach SPI pins or disconnect power.

Shimmer surviving hold excludes ongoing frame transfers as a necessary cause;
it favors panel drive, backlight/power or another interference source, without
proving which. Shimmer disappearing only in hold implicates transfer activity,
including scan interaction or transfer-induced electrical noise, not necessarily
framebuffer correctness. Different pattern regions may show different behavior.

The test selection is saved like other device settings. Return **SPI test** to
**Off** after testing (on the internal screen); restarting alone does not clear
it. Existing device records restore the new field to Off when absent. Existing
ABI-1 firmware accepts this SD-driver-only addition; install the matching
``driver.so`` and ``manifest.json`` package and restart to discover the field.

Verification boundary
---------------------

Host regressions cover ABI/NVS compatibility, bounds, DMA source leases,
startup black/off, queued mode changes, sleep transfer suppression, clipped
incremental drawing, streamed Doom scanlines, routing transactions and safe stock
external PWM. Target-header syntax checks are separate from full builds/links.
Neither host pixels nor source checks establish physical shimmer, PWM quality,
frame rate or long-run simultaneous audio behaviour. Display aggregate logging
is optional through ``LILKA_MEASURE_DISPLAY``; no per-frame serial output is added.

The SDK retained direct-drawing framebuffer uses explicit PSRAM-only allocation
(134400 bytes at 280x240). If PSRAM allocation fails, routing falls back to the
native display instead of consuming internal RAM needed by task stacks/DMA.
Existing application Canvas constructors retain their default allocation policy.
