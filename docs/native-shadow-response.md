# Native-panel shadow response

Anton verified condition 2 of SDK diagnostic commit
`240894094604c1ed3179d49106dc03aebfe9cf31` fixes the sharp jump from black to the
first visible shade. The production change extracts that adjustment only.

## Production behavior

The v2 Display initializer first calls the original Arduino_ST7789 initializer.
It then writes the positive/negative gamma tables from pinned GFX 1.6.0, changing
only the last two bytes of each table:

| Field | Original | Verified value |
|---|---|---|
| V61P / V61N | `1D` | `35` |
| V62P / V62N | `21` | `37` |

All remaining 24 bytes, including V63 and the white-side controls, are unchanged.
The visible black end on this IPS configuration is the high-index table end;
the earlier V1/V2 experiment affected highlights and is not included.

The implementation preserves the existing 80-MHz clock, inversion, F0 expansion,
pixel format, brightness and drive references. The external driver and v1 are
unchanged. No experimental init profile, boot test, button handler, GAMSET curve
selection, settings key, image transform, worker or per-frame processing is added.
The 28-byte temporary arrays exist only during startup. Revisit this override
when upgrading the pinned GFX dependency; do not silently change other table fields.

The two gamma-register commands each require their full 14-byte parameter list;
there is no separate command to update just the last two entries.

## Verification

- `tests/native_shadow_init/run.py` executes production startup/override methods
  with a bus recorder under ASan/UBSan. It checks original-init ordering, exact
  accepted table values, unchanged other fields and clocks, and no v1 override.
- `tests/display_startup/run.py` checks existing display/backlight startup modes.
- The production tables are compared with the accepted diagnostic condition 2,
  not a new calibration. Device acceptance applies to that exact register result.
- Changed-source formatting/static analysis and real ESP32-S3 header syntax pass.
- All 27 host startup cases pass (three calibration cases plus 24 existing modes).
- Full checks retain the same 143 formatting diagnostics and 12 normalized static
  findings as the base; no unrelated fixes are mixed into this change.

## Rebuild every application with the shared SDK

```powershell
.\build-all.cmd --app-branch features/stage --sdk-branch features/stage --no-drivers
```

Without `--interactive`, the existing runner selects all six apps. It fetches
staging, cleans each build and uses one pinned SDK revision for all outputs.
No manual checkout or PlatformIO steps are needed. SD drivers do not need rebuilding
for this native-panel initialization-only change.
