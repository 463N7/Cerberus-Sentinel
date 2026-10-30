# Wardriving radio plan and expansion adapter

`s3_main/main/wardrive_plan.{c,h}` builds a deterministic assignment for the
onboard C5 (R0) and optional expansion slots R1-R6. The planner is linked into
the S3 firmware so the field survey can convert and apply R1-R6 profiles. The
live Wardriving service does **not** call this adapter yet. No expansion profile
has been accepted on hardware; a planned channel is only a request until the
radio acknowledges it and its scan driver accepts the channel.

- Regions: US channels 1-11, ETSI 1-13, JP 1-14; 5 GHz uses the preserved
  36-64 and 100-144 sets (ETSI excludes 144), with US 149-165.
- Bands: mixed, 2.4 only, 5 only. Each allowed channel belongs to exactly one
  available radio in the plan. Missing expansion slots have no allocation;
  rebuilding after a presence change redistributes the whole region.
- Timing: Fast 100/180 ms, Balanced 120/220 ms, Thorough 140/240 ms for active
  versus passive DFS dwell. The planner balances estimated dwell load, while
  actual scan time also depends on driver and observed traffic.
- `cerberus_wardrive_plan_expansion_config` produces the existing protocol-v3
  `cerberus_scan_config_t` for R1-R6 only. The expansion scanner rebuilds its
  channel list from masks, so `channels[14]` is a legacy preview and
  `channel_count` can be larger. Never send this config to onboard Sentinel-v2.

The host check `python tools/check_wardrive_plan.py` covers every valid R0-plus-
optional-slot mask for all three regions, three bands, and three timing presets:
1,728 plan combinations. It checks exact region union, no duplicate or missing
channels, empty absent slots, weighted dwell bounds, expansion config fields,
and rejection of invalid inputs. `tools/check_wardrive_profile.py` checks
CONFIG framing, canonical profile fingerprints, old-image fallback, Stop
profiles, region mask validation and driver rejection metadata.
`tools/check_wardrive_profile_lifecycle.py` extracts the actual field survey
setter to verify that replan is refused until Stop/drain and stream close. These
are host checks and do not exercise a C5 driver.

## R1-R6 adapter API

1. Initialize field survey, then read `field_survey_snapshot().present_mask`.
   Idle slots are probed promptly at startup and then at most once per 15
   seconds. Bit 0 names R1, bit 5 names R6. A missing slot is optional.
2. Build a plan with R0 bit 0 set and each present expansion shifted to its
   planner bit: `available_mask = 1U | (present_mask << 1U)`. The caller owns
   the onboard R0 configuration separately; expansion protocol-v3 cannot
   configure Sentinel-v2.
3. Call `field_survey_set_expansion_plan(&plan)` only after the previous survey
   has stopped and drained, with the Wardriving stream closed. It returns
   `ESP_ERR_INVALID_STATE` while a scan, uncertain Stop, pending worker
   exchange or stream is active. A momentary idle probe may cause a bounded
   `ESP_ERR_TIMEOUT`; retry before starting. `NULL` restores the existing
   manual field survey after Stop/drain.
4. Open the stream and call `field_survey_set_running(true)`. Each assigned
   slot receives CONFIG_ARM then CONFIG. The scanner's versioned status proof
   reports whether the exact channel masks, dwell, region and enable state
   were applied. `profile_verified_mask` alone confirms a running assignment.
   `profile_unverified_mask` identifies an older image with no proof; it stays
   off. `profile_rejected_mask` reports a rejected or mismatched CONFIG.
   `profile_driver_rejected_mask`, `last_rejected_channel[]` and
   `last_scan_error_by_slot[]` report scan-start failures after this plan was
   installed. The error code is needed because a driver failure does not by
   itself prove a regulatory channel rejection.
   For onboard-only Wardriving, call
   `field_survey_set_idle_probe_enabled(false)` before acquiring the onboard
   C5 scan. It closes the probe gate and waits up to one second for a current
   idle probe to release the shared SPI mutex. `ESP_ERR_TIMEOUT` leaves the
   gate closed so the caller can retry the drain. Call with `true` after
   Wardriving teardown. This gate does not delay R1-R6 scans or pending STOPs.
5. On Stop, wait for `worker_seen_epoch == request_epoch` and zero scanning and
   uncertain masks, drain the stream, close it, then clear or replace the plan.
   A slot lost after a possible Start remains uncertain until Stop is
   acknowledged; the caller must not silently reassign its channels mid-scan.
   To exclude a rejected or missing slot, stop and rebuild the whole plan.

The status proof occupies previously reserved bytes in protocol-v3's 64-byte
STATUS response. Older expansion firmware ignores the extension and still
works in manual fixed-US field survey mode. Current expansion build profiles
identify the physical R1-R6 slot through S3 chip select, while their firmware
default node ID is zero. The applied fingerprint verifies the requested slot
ID. No GPS is required by this adapter.

## Remaining integration

1. Add an explicit profile input to the current Wardriving Start flow, while
   keeping no-GPS startup valid. Persist region/band/timing only after the
   existing settings schema and migration are reviewed.
2. Wire the stopped-state R1-R6 adapter sequence above into Wardriving after
   the current no-GPS startup timeout is resolved. Keep rejected or missing
   slots visible as incomplete coverage, then rebuild only after Stop/drain.
3. Provide a separate onboard Sentinel-v2 C5 scan-plan API or retain its full
   dual-band scan as an explicit fallback. The protocol-v3 expansion config
   cannot configure R0. Do not claim the planner's exact one-channel-per-radio
   allocation is active until the R0 adapter and optional R1-R6 adapters both
   apply it.
4. Check driver/country admission of each regional channel and the passive
   DFS dwell behavior on actual hardware. Then test 1-7 radios, radio loss,
   profile changes, Stop/USB SD handoff, sustained logs, and S3 BLE concurrency
   before marking the plan accepted.
