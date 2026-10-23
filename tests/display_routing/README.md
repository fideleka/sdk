# Display routing regressions

- `python3 tests/display_routing/run.py` exercises the production routing code with
  deterministic native-LCD/Canvas peripherals (normal and ASan/UBSan).
  It also compiles the public idle-policy setter and checks that automatic frames
  honor the game's exemption, including nested frames and long loading gaps.
- `python3 tests/display_routing/text.py` compiles the installed, read-only
  Arduino_GFX implementation and a real U8g2 Cyrillic font with production
  `Display::drawChar`. Only pixel sinks and Arduino hardware/Print primitives
  are simulated. It checks ASCII/UTF8, two text scales, transparent/opaque text,
  native-plus-external equivalence and external-only native-write suppression.
  Both normal and ASan/UBSan executables run; no firmware build or downloads.

The text test deliberately poisons the retained Canvas's unprepared U8g2 decoder
pointer. Calling its `drawChar` directly reproduces the device panic: U8g2's
`write` prepared the glyph on Display, not Canvas. Display must decode once and
route the resulting virtual pixel spans, rather than decode independently on an
unprepared graphics instance. `--source /path/to/old/display_graphics_routing.cpp
--sanitized-only` permits checking the negative control without editing source.

These tests do not prove panel timing, SPI transfer behavior or device playback.

- `python3 tests/display_routing/memory.py` fault-injects the production Canvas
  placement constructor: 16-byte aligned PSRAM-only allocation, zero-size and
  failed allocations, no internal fallback, and unchanged opt-in default policy.
