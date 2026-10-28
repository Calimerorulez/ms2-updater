# MS2 Updater

Independent, community-maintained tooling for Märklin Mobile Station 2 (MS2) firmware updates over a compatible CAN interface, without requiring a CS2 or CS3.

> **Project status: preparing the public source release.** This repository does **not yet contain the verified updater source or a downloadable release**. Do not attempt firmware flashing from this repository until the code, safety checks, and build instructions have been reviewed and published.

## Design requirements

- **No fixed device IDs:** Discover the target MS2 dynamically at runtime. A UID from an author's test setup must never be embedded in the program.
- **No hard-coded hardware revision:** Detect the reported hardware identity and select the *matching* firmware image and protocol path. Never treat an unknown device type as an assumed compatible one.
- **No accidental flashing:** Require an explicit update operation; probing must be read-only. Verify the detected UID, hardware type, firmware image, image integrity and update eligibility before changing device state.
- **Safe CAN traffic:** Ignore unrelated devices, check acknowledgements and abort safely on timeouts, errors or ambiguous responses.
- **No bundled proprietary images:** Users must obtain firmware/resource files lawfully; Märklin is a trademark of its respective owner.
- **No personal or infrastructure-specific configuration:** Never publish private IP addresses, usernames, hostnames, real device identifiers, dumps or logs without redaction.

## Compatibility

The project aims to support supported MS2 device families by *runtime identification* and validated firmware mapping, not by fixed serial numbers or assumptions about revisions. Exact supported combinations will be documented and tested with the code release. An unfamiliar revision must be rejected safely rather than flashed speculatively.

## Scope

The MS2 updater is the primary project. Experimental recovery utilities and third-party firmware images are **not** part of the first public release.

## Licensing

Project-original contributions are intended to be MIT licensed (see [LICENSE](LICENSE)). Code derived from third parties retains its original terms; review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before importing any such source.

## Safety

Firmware updates can render equipment unusable. Do not flash without a backup/recovery plan, a stable power supply, the exact matching image and a verified device identity. No warranty is provided.

This project is not affiliated with or endorsed by Märklin.
