> Historical design assessment, preserved from 2026-10-27. Hardware assumptions,
> implementation status, workspace-relative paths and acceptance milestones below
> describe that earlier investigation. They are not current build or device status.
> See the [rev81 contribution overview](CONTRIBUTION_REV81.md) and
> [validation instructions](VALIDATION.md) for the current checkpoint. Original
> source trees, local receipts and other parent-workspace paths are not included
> in this contribution.

# Cerberus unified firmware: initial engineering assessment

Date: 2026-10-27. Status: build foundation, shared radio transport, first expansion
survey, and SD vendor database implemented; full parity and hardware validation
remain pending. See `Cerberus-Unified/docs/FIELD_KIT_PROGRESS.md`,
`Cerberus-Unified/INTEGRATION.md` and `integration/baseline-summary.json` for
implementation and measured build status.

## Objective

Combine Sentinel and Wardriving into one user-facing application and matched release package. Preserve their existing capabilities, support the optional C5 expansion bank, and use SD storage for appropriate data and assets. Separate processor images remain necessary: S3 application, onboard C5 services, and expansion C5 images with the correct pin profile.

## Source baselines

- `Cerberus-Sentinel-main/Cerberus-Sentinel-main`: proposed S3 application foundation; touchscreen, Wi-Fi/BLE/802.15.4 tools, Watch, Field Guide, settings, storage, USB mass storage, local web, recovery and packaging.
- `Cerberus-Wardriving-main/Cerberus-Wardriving-main`: GPS, R0-R6 scanning, channel/timing presets, WiGLE CSV, deduplication, WiGLE/WDGWARS uploads, remote dashboard and Ghost-compatible USB commands.
- Keep both imported source trees and ZIPs intact as references. Establish reproducible baseline builds before moving code into a unified working tree.

## Confirmed integration conflicts

| Area | Source evidence | Required design |
| --- | --- | --- |
| SPI ownership | Both `s3_main/main/c5_spi_link.c` implementations initialize SPI3. Wardriving manually selects seven radios. | One bus owner with serialized exchanges, bounded waits and explicit device routing. Do not initialize both drivers independently. |
| Wire protocol | Sentinel protocol 2 uses 32-byte frames and 64-byte event batches; Wardriving protocol 3 uses its own 64-byte request/footer protocol. | Separate namespaced codecs initially. Version numbers do not imply mutual compatibility. Reject unsupported responses. |
| Clock and handshake | Sentinel requests 40 MHz and READY on S3 GPIO44; Wardriving uses 250 kHz and onboard IRQ GPIO39. Expansion uses settle delays. | Preserve per-device timing and handshake behavior. Verify shared-bus electrical behavior on hardware before raising expansion speed. |
| Memory | Sentinel enables 2 MB quad PSRAM with explicit allocations; Wardriving disables PSRAM. | Confirm actual S3 module. Preserve Sentinel quad-PSRAM configuration for verified N16R2 hardware; never enable octal PSRAM on the documented SD pin layout. |
| Storage | Both mount/manage SD; Sentinel also hands the card to USB mass storage. | One storage service. Stop and flush all file users before USB ownership; remount and invalidate caches after eject. |
| USB | Wardriving expects native USB Serial/JTAG identity and Ghost commands; Sentinel has USB mass storage and console support. | Define explicit USB modes and test host compatibility; command compatibility alone does not preserve USB identity. |
| Networking | Independent setup, remote-control, authentication and upload implementations. | One network lifecycle and authentication policy; port functionality behind it. Preserve explicit user-triggered uploads. |
| Application ownership | Both have their own main, UI, settings, display, touch, battery and storage code. | Use one implementation per shared peripheral. Port features as services rather than concatenate source directories. |

## Proposed radio allocation

Initially retain Sentinel's onboard C5 service image and Wardriving's expansion scanner image. The S3 adapts both protocols through one transport owner. Expansion R1-R6 can supply wardriving observations while R0 provides Sentinel tools, subject to measured bus and power limits.

This intermediate allocation is not full feature parity: Wardriving's configurable R0 scanning still needs implementation in the onboard service protocol. Add capability-gated channel/dwell/configuration and streamed results to the onboard firmware in a later milestone. One C5 cannot simultaneously run conflicting Wi-Fi, BLE and 802.15.4 jobs; a scheduler must arbitrate them. With no expansion attached, the onboard C5 must still offer a bounded wardriving mode.

Track per-radio presence, firmware capability, job owner, timeout, queue depth and drops. Missing expansion radios must not stall the UI or cause repeated long blocking polls. Distinguish absent, incompatible and temporarily unresponsive radios.

## SD and memory strategy

SD increases file capacity and can free firmware flash by externalizing data. It does not directly enlarge executable flash or provide transparent heap/PSRAM. Keep native firmware, drivers, recovery and a minimal usable UI in flash. Use internal RAM for time-sensitive queues and required transfer buffers; use verified PSRAM for suitable display and asset caches.

Candidate SD content: capture files, reports, upload ledgers, optional images, lesson text, reference data and web assets. Inventory actual linked read-only data before estimating savings: procedural drawing code and C source size are not equivalent to movable image bytes.

Use versioned asset manifests, bounded reads and caches, validated lengths, and built-in fallback assets. Removing the card or presenting an incompatible asset pack must not prevent boot or recovery. Essential settings remain in NVS; exportable backups can live on SD. Never format a card automatically after a mount failure.

Both S3 partition files allocate a 7 MiB factory application despite targeting 16 MB flash. Sentinel also reserves 256 KiB for coredumps. Measure actual ELF sections and binary sizes before changing partitions. Decide recovery and future update strategy before consuming the remaining address space. The board reference documents 4 MB C5 hardware, while the onboard software layout currently uses 2 MB and expansion application slots use 1 MiB; SD does not enlarge those executable partitions.

References: [Espressif memory types](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/memory-types.html), [external RAM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/external-ram.html), [SPI ownership and thread safety](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_master.html). Validate implementation details against the pinned project's IDF version.

## Implementation milestones and acceptance

1. **Reproduce baselines.** Record source hashes, framework versions, S3/C5 image sizes and map files. Run available host checks. Record the currently working device firmware and hardware profile. No flashing needed for this step.
2. **Create unified build foundation.** Start from Sentinel in a separate working tree. Preserve its recovery package. Introduce board profiles and a shared SPI owner; namespace expansion protocol and driver code. Acceptance: existing Sentinel builds, unchanged onboard commands, missing expansion handled without freezes.
3. **Integrate expansion and GPS.** Add read-only radio inventory and bounded GPS service, then expansion scan configuration and records. Acceptance: all fitted radios identified; no false GPS fixes; UI stays responsive with one radio disconnected or timed out.
4. **Integrate wardriving end to end.** Add unified navigation, radio scheduling, GPS-gated CSV, deduplication, stop/flush behavior, presets and remote controls. Acceptance: no concurrent ownership of R0; correct coordinates, CSV escaping and bounded memory; stopping closes the capture cleanly.
5. **Complete capability parity.** Add onboard configurable scanning, uploads and ledgers, USB compatibility modes, and settings migration. Maintain a detailed feature checklist from both READMEs and code. Acceptance: onboard-only operation, expansion operation and Sentinel tools all pass; unsupported combinations are visible in UI.
6. **Externalize measured assets.** Move the largest suitable data assets to a versioned SD pack. Acceptance: document measured flash savings and RAM peaks; exercise missing/corrupt/full/removed SD, USB export and fallback UI.
7. **Package and hardware acceptance.** Produce matched images, checksums, asset pack and recovery instructions. Exercise sustained capture, radio mode switching, upload interruption, power cycling and card ownership. Build success alone is not hardware acceptance.

## Hardware information still needed

- Exact S3 module marking and board revision (N16R2 versus legacy N16R8).
- Expansion module type (WROOM versus XIAO), count and wiring revision.
- GPS is currently absent. Confirm module and RX1/TX2/PPS46 wiring when it is added.
- The running S3 identifies itself as Cerberus Prototype Wardriving Expansion 1. SD and all fitted C5 radios still need hardware acceptance checks.

These determine pin and PSRAM profiles. Source analysis and baseline builds can proceed before these answers; selecting a flash target cannot be inferred from the folder names.
