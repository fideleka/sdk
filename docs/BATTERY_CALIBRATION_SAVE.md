# Checked battery calibration saving

`Battery::calibrateFullLevel()` retains its public API and persisted
`battery/fullRawAdc` key. It takes a fresh 32-reading ADC median, validates the
existing minimum of 3.5 V plus the supported charge-status normal-band upper
bound of 4.6 V, checks NVS open/write results, and publishes the new reference
only after a successful write. Failure returns false and preserves the old RAM
and saved reference.

The full reference is atomic; estimated-percentage reads use one coherent
snapshot while a background consumer updates it. `readLevel()` and raw-voltage
semantics, discharge profiles and existing saved calibration values remain
unchanged. Manual calibration/reset APIs remain available to other consumers.
The SDK now owns the confirmed Charged → Battery transition and 30-second
stabilization timer; Keira removes the manual UI entries and only displays the
SDK snapshot.

Run `python3 tests/battery_calibration/run.py` for real-source host regressions
with normal and ASan/UBSan modes, checked save failures, fresh charge-tag/invalid
sample rejection, legacy APIs and v1 no-op behavior. Firmware build/link and
physical calibration accuracy are not certified by these source checks.

## Shared charging-state monitor

Define `LILKA_ADC_CHARGE_STATUS=1` only for v2 hardware with the existing
33 kΩ/10 kΩ optocoupler tags. The default is disabled; v1 remains a no-op.
`lilka::begin()` calls `battery.begin()`, which loads existing settings and starts
one 3 KiB-stack, 1 Hz worker when enabled. Task creation is checked and repeated
initialization does not create duplicate workers.

`lilka::battery.getChargeSnapshot()` is a bounded, coherent RAM-only read:

```cpp
const auto state = lilka::battery.getChargeSnapshot();
// state.status: Unknown / Battery / Charging / Charged / Absent (debounced)
// state.sampleStatus: instantaneous classification, for freezing pending UI
// state.rawVoltage: normal-divider reconstructed volts, including USB tags
// state.batteryVoltage / estimatedLevel: last normal values, never tags
// estimatedLevel is -1 until the first normal sample
// state.updatedAt: timestamp of the latest poll
```

The worker shares one existing 32-reading median between detection and percentage
estimation. Getters/drawing never sample ADC or write NVS. Automatic calibration
uses a fresh median after 30 seconds; only this worker attempts automatic saves.
Raw/legacy battery APIs remain available and retain their behavior, but consumers
on tagged hardware should use snapshots for UI, not convert a tag into a percent.

Three consecutive valid samples confirm state changes. Unknown/guard readings
retain the last confirmed state, while their sampleStatus tells UI to freeze
previous battery values. Last-normal voltage/percentage are retained in the
snapshot so a newly opened UI can bootstrap without displaying a tag or 0V. Unknown on cold startup means measuring; only confirmed
Absent should be presented as a missing battery. No snapshot expiry is implied.

Charged → Battery grants one automatic-calibration opportunity. Startup directly
on Battery or unplugging from Charging does not. Reconnection, invalid/absent/guard
samples or a normal voltage below 3.5 V cancel the wait. Eligibility is RAM-only.
The worker operates while an application's display/UI is hidden or asleep; each
guest firmware must opt into the hardware flag to get the same SDK behavior.

SDK owns `ChargeStatusFilter`, `AutoFullCalibration`, polling, snapshot publication,
calibration persistence and their regressions. Consumers own icons/localization,
layout and visual smoothing. Keira's existing charge-status build profiles define
the shared SDK hardware flag for both sampling and charging presentation.
