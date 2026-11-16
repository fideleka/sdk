# Upstream integration — 2026-11-16

## Scope and branch policy

The owner requested all upstream changes in `main`, followed by a rebase of
`features/stage`. Both repositories use `main`, not `master`.
All configured remotes and tags were fetched. `git rebase --rebase-merges`
preserved the stage merge structure; unrelated feature refs were not rewritten.

Before changing history, the local tips matched origin. Recovery refs:

- `archive/main-before-upstream-20261116`
- `archive/stage-before-upstream-20261116`

SDK upstream: `c61ebfed3a24f8802b158ce6e67567c0bf660a2d`.
Keira upstream: `7fdebd5ee1ae7795c7a75dd3dbadd85b4374491a`.
SDK main retains its older battery-calibration history. Keira main is an exact
fast-forward to upstream. Rebased stages contain their updated main branches.

## Verification limits

Source/host checks only. No PlatformIO firmware build, packaging, flash, device
boot, memory-size measurement, hardware-module test or rendered-device UI check.
Sibling SDK/LilkaModules selection is preserved in source; the resolved
PlatformIO dependency graph has not been build-proven.

The full formatting and cppcheck gates fail on both the saved old stage and the
integrated source. With clang-format 20.1.8: SDK has 141 diagnostics before/after;
Keira has 311 before and 307 after. With cppcheck 2.13.0: SDK has 41 diagnostics
before/after; Keira has 30 before/after. Comparing normalized diagnostics found
no new unique messages. These are not clean full-gate passes.

The newly imported architecture audit also fails: SDK reports 92 violations,
Keira reports 348. There was no corresponding audit target in the old stages.
Broad architecture/format cleanup is outside this rebase.

Final manually adjusted compatibility C++ files pass clang-format. Keira's adjusted Display settings and
restored App submission pass targeted cppcheck. SDK's full display source
still reports inherited drawCanvas/unreadVariable findings.

## Host checks

All 21 initial source/host suites pass, plus the new Display SPI menu test and
a sanitized INI parser smoke check. The metadata test initially lacked cached
ArduinoJson headers; it passed with CPATH pointing at the existing ArduinoJson
copy in the retained LilVideo baseline build. No dependency download was needed
for these host suites.

SDK suites: display_startup, upstream_compatibility, menu, source_radio_compat,
release_diagnostics, display_routing, pixel_dma, battery_calibration,
display_settings and settings_persistence.

Keira suites: upstream_compatibility, release_diagnostics, frame_pacing,
frame_metrics, menu_access, guest_shortcuts, guest_metadata, charge_status,
measurement, volume_select and tools/checklang.py. The new
`tests/display_settings_spi.py` checks the production menu, input consumption,
frequency/mode persistence, Back cancellation and unrelated timeout protection.

## Repeatable rebase checks

`--rebase-merges` recreates merges, but does not automatically preserve all
manual changes made only inside old merge commits. Compare the result against
a whole-tree merge of the archived stage and new upstream, then inspect every
unexpected difference. Retain compatibility-only changes such as public header
exports, literal-format printing, canvas locks and their host tests.

## SDK resolutions

- Preserve the stage battery implementation and seven discharge profiles rather
  than replacing them with the older offset-only implementation from main.
- Keep Sys, including USB-safe restart, and expose both sys.h and partitions.h.
- Preserve both menu regression sets and strict constructor/sequence checks.
- Combine configurable native display SPI with black-before-backlight startup,
  routed displays, native DMA and verified shadow initialization.
- Guard optional NVS reads and fall back from invalid frequency/mode values.
- Keep the merge-only Sys host regression and extend the startup harness for
  config reload, saved SPI settings, missing records and invalid values.
