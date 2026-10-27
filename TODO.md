# Remaining firmware and flashing work

## Firmware release

- [ ] Confirm the production S3 flash/quad PSRAM profile and C5 module memory.
- [ ] Validate radio switching, cancellation, recovery, and extended Watch operation.
- [ ] Confirm supported device signatures and false-positive behavior using actual devices.
- [ ] Inspect display, touch response, alert scenes, and battery status on the production LCD.
- [ ] Confirm lesson/settings persistence, SD ownership, and local web start/stop.
- [ ] Resolve outstanding board issues before labeling a shipping release.

## Flashing and manufacturing

- [ ] Complete a fresh-board browser install through bridge, C5, and S3, including startup checks.
- [ ] Validate USB reconnection and interrupted-write recovery with BOOT/RESET fallback.
- [ ] Document the production programming sequence and supported board revisions.
- [ ] Record a known working matched firmware package for recovery.
- [ ] Define future updates around the current single factory slot on each processor.
