# Display startup regression

Run `python3 tests/display_startup/run.py` from the SDK root.

The host harness executes production `lilka.cpp`, `board.cpp`, and the extracted
production `Display::begin()` body. It models retained, nonblack LCD RAM and
checks that initialization clears it before driver discovery. Modified hardware
must start with GPIO/PWM off. Routing must finish before output resumes.

The 24 ASan/UBSan cases cover Internal, External, Both, Select-held recovery,
missing drivers, and worker failure, on stock and modified v2 hardware.
`tests/backlight/run.py` separately exercises the actual PWM code: initialization
at zero, route changes while suspended, idempotent initialization, brightness
restoration without persistence or feedback, and External keeping native PWM off.

All six staging consumers use this SDK entry point and the sibling SDK:
Keira, Doom, LaserTank, LilOBD, Lilplayer, and Lilvideo. No application-specific
startup change or SDK pin update is required.

The external ILI9341 driver already attaches configured BLK with output inactive,
resets and clears the panel with scanout off, then enables the selected route.
Firmware cannot suppress a powered module's default-white interval before GPIO
control begins. Instant power-on darkness requires a hardware default-off BLK
circuit or switched display power. Stock amplifier-shared GPIO behavior remains
unchanged; the native boot backlight hold requires independent-backlight hardware.

These tests verify source behavior, not physical power-on timing or a confirmed
cause of the reported English overlay. Device boot and firmware linking remain
separate checks.
