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
Keira removes those UI entries and automatically requests calibration after a
confirmed Charged → Battery transition and 30 seconds of stabilization.

Run `python3 tests/battery_calibration/run.py` for real-source host regressions
with normal and ASan/UBSan modes, checked save failures, fresh charge-tag/invalid
sample rejection, legacy APIs and v1 no-op behavior. Firmware build/link and
physical calibration accuracy are not certified by these source checks.
