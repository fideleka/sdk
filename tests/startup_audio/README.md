# Startup audio GPIO0 regression

Run from the SDK root: python3 tests/startup_audio/run.py

The runner compiles real current audio.cpp/controller.cpp and the real immutable
ping.h source, not a reimplementation of welcome playback. It calls Audio.begin,
then Controller.begin, then executes the task captured from playStartupSound.
It checks native IDF install/config/zero/write/uninstall calls. Arduino
begin/write/end and any direct audio GPIO0 configuration fail the test.
Both normal and AddressSanitizer + UndefinedBehaviorSanitizer runs are mandatory.
The existing U8g2 dependency is used read-only; no PlatformIO/build/download step.
Set U8G2_CLIB to an existing clib directory if needed.

Coverage:
- Explicit MCLK=-1, DATA_IN=-1, and exact BCLK/LRCK/DOUT; master TX,
  22050 Hz, signed 16-bit duplicated stereo, bounded 128-frame chunks.
- Every real ping sample, /4 welcome headroom, zero/50/100% master gain,
  out-of-range master clamps, and a live gain change between chunks.
- Byte-exact continuation after odd-sized partial writes, zero progress,
  oversized result, write error (including after a partial write and during
  final signed-zero silence), pin error and DMA-zero error.
- Exactly one release of a successfully installed driver before task deletion;
  install failure, a preexisting app driver and an app acquisition during the
  400 ms delay cause no pins/writes/uninstall. App acquisition while startup owns
  the port fails without preemption.
- Controller Select GPIO0 remains INPUT_PULLUP, idle and visibly unpressed in
  deterministic scans interleaved at delay/config/write/release/task deletion.

## Ownership and limits

Startup uses the already exposed esp_i2s namespace from installed Arduino I2S.h.
It makes one legacy i2s_driver_install attempt after its delay. Only success
establishes ownership; failure skips startup entirely. Pin configuration occurs
only afterward and explicitly disables MCLK and input routing. There is no
Arduino transmitter, retry/preemption, or background GPIO0 restoration.
The installed ESP32-S3 libdriver.a was inspected read-only: i2s_driver_install
calls i2s_priv_register_object, whose occupied-port check and registration are
inside xPortEnterCriticalTimeout/vPortExitCritical. Its header documents
ESP_ERR_INVALID_STATE for an occupied port. The host model covers either
acquisition ordering; it is not a multicore hardware stress test.

Ordinary native-driver callers must respect ownership: raw uninstall/reconfigure
of someone else's port is not safe and cannot be prevented through the legacy
port-number API. A driver installed by startup temporarily prevents an app
install; startup never uninstalls an already-owned app port. No GPIOs are restored
after teardown, avoiding a background pin-restore race.

I2S.setAllPins is retained as the public compatibility default. External apps
that use the installed Arduino I2S.begin/write wrapper remain hazardous: its
_applyPinSetting initializer omits mck_io_num, implicitly selecting GPIO0.
This patch does not repair that third-party wrapper or arbitrary app playback.
LILKA_NO_AUDIO_HELLO compiles out the hello task as well as its scheduling;
the existing shortcut suite does not need fake native-driver declarations.
This separate suite enables and tests the real startup path.

This establishes a concrete source-level GPIO0 hazard and its startup fix, not
that it caused the particular Doom phantom Select report. Device boot, GPIO
capture, real DMA tail drain/audibility, multicore stress, firmware compilation,
stack/heap measurements and Doom repro remain unverified. No firmware build,
package, flash or dependency installation is part of these tests.
