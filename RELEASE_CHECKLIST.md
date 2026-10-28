# Release checklist

This repository is a **prerelease source snapshot**, not a verified firmware-updating product.

## Confirmed on real SRSEII hardware (2026-10-28)

- Clean checkout built successfully on Debian.
- OpenWrt MIPS32r2/o32/soft-float statically linked CLI built.
- Offline ELF/ABI audit passed.
- The stripped CLI ran on a real SRSEII and its SocketCAN probe found one MS2 (hardware 0033, firmware 5.6), while declining to select unrelated CAN devices.
- Probe is **not** a firmware-update test. No installation was performed by these public-source tests.

## Release gates

- [x] Firmware-free profile metadata smoke test for 0032/0033/0034 and unsupported types, via `make check-profiles`.
- [x] Source builds and runs on the one tested SRSEII/OpenWrt configuration.
- [ ] Execute the public `make check-profiles` target in a new clean checkout.
- [ ] Verify the target device SHA256 against the identical stripped release file.
- [ ] Complete security review of active `update` and `serve-resources` commands; both currently have live support enabled in source and may modify device state when explicitly invoked.
- [ ] Complete portable, independently reproducible tests of firmware data-plane paths with authorized images and redacted captures.
- [ ] Confirm relevant third-party license and copyright notices.
- [ ] Cross-build the exact tagged source and archive the compiler version and verification report.
- [ ] Publish one checked release binary, a matching SHA256SUMS and accurate platform instructions.

## Proposed release asset

`ms2-updater-linux-mipsel-omega2` is built from `src/ms2x-c/cross-build/omega2-static/release/ms2-updater.stripped`.

Never commit firmware images, real trace captures, private infrastructure paths or personal CAN hardware UIDs.

Do not claim hardware revisions 0032 and 0034 are tested on real devices: only their profile definitions have been checked offline.

## Static source review (2026-10-28)

- The production CLI discovers and selects the runtime UID; `--target` is optional when there is exactly one unambiguous supported MS2.
- Firmware loading checks the detected hardware-specific profile, exact image length, firmware version bytes, hardware bytes, and the profile's expected SHA256 digest before starting the engine.
- The session logic checks device identity consistency and the updater checks protocol acknowledgements.
- **Not yet cleared:** `live_update_enabled=1` and `live_resources_enabled=1` in `linux_main.c`. They expose write-capable commands whenever the caller explicitly chooses them.
- **Not yet cleared:** the fallback explicit bootloader-UID and hardware path needs targeted negative tests proving no firmware data is transmitted for missing, inconsistent, or ambiguous boot identities. Some lower-level test-only convenience constructors still use a synthetic default UID; those are not the CLI's normal target selection.
- **Not yet cleared:** concurrent CAN resource-server behavior, timing and interference risks need dedicated review.
- **Important:** the firmware-free profile test covers *metadata mapping only*. It is not a complete flashing or firmware-image-validation test.

**Release decision:** defer a general-purpose v1.0.0 firmware updater until the above write-path checks pass. A probe-only preview may be published separately if desired.
