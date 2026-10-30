# MS2 Updater

**Languages:** **English** · [Nederlands](README.nl.md) · [Deutsch](README.de.md)

See [RELEASE.md](RELEASE.md) for the verified Omega2+ release-candidate hash, deployment procedure, and remaining release restrictions.

Community-maintained, independent Märklin Mobile Station 2 (MS2) updater for Linux SocketCAN.

**Status: [v1.0.0-rc1](https://github.com/Calimerorulez/ms2-updater/releases/tag/v1.0.0-rc1) is published as a prerelease.** The statically linked SRSEII / Omega2+ executable was tested on real hardware for device identification. A complete firmware update with this public binary has **not** been retested. Firmware flashing is at your own risk.

## Build

```sh
cd src/ms2x-c
make build/ms2-updater
./build/ms2-updater --help
```

Requires a C11 compiler, `make`, Linux and SocketCAN headers. For an OpenWrt Omega2+ cross-build, use `make -f Makefile.cross all CROSS_COMPILE=<your-toolchain-prefix>`. The included `Makefile.cross` builds and verifies the offline tool and also cross-compiles the real statically linked SocketCAN CLI to `cross-build/omega2-static/release/ms2-updater.stripped`. The stripped CLI has been tested on an actual SRSEII using an identification probe.

The full historical test harness depends on externally supplied official firmware and some non-public, redacted CAN captures. Those are deliberately excluded. Do not treat `make check` as a completely self-contained test at this stage. Firmware-free checks: `make check-profiles` and `make check-firmware-reject`.

## Usage

Read-only passive observation:

```sh
./build/ms2-updater probe --interface can0 --seconds 10
```

Active identification:

```sh
./build/ms2-updater probe --interface can0 --seconds 10 --query
```

The `update` and `serve-resources` commands can modify target state. They are enabled in the **source code** and must not be used until device/image selection and deployment have been independently verified. A prerelease binary is distributed, but no proprietary firmware images are included.

## Device-independent targeting

The production CLI does **not** depend on an author's device UID: it discovers MS2 identities at runtime. If more than one compatible MS2 is present, use `--target UID` to select the intended device. Profile definitions cover MS2 hardware IDs `0032`, `0033`, and `0034`; firmware images must exactly match the detected hardware profile. Unsupported revisions are rejected instead of guessed. Only hardware type `0033` has been probed with the public binary on real hardware; `0032` and `0034` were checked offline.

Historical *offline test fixtures* use synthetic IDs and are not target selection.

## Firmware source, safety and recovery FAQ

**Do I need a CS2 or CS3 to obtain MS2 firmware?** Märklin MS2 firmware is normally distributed as part of CS2/CS3 update packages. The required MS2 image may be extracted from a legitimately obtained official update package; owning a CS2 or CS3 is not inherently required just to obtain the file. This project does **not** include an extractor or host Märklin firmware, and extraction procedures and package formats have not been validated here. Use an official, authorized source and verify the extracted image before attempting an update.

**Which file does this updater expect?** The detected MS2 hardware ID determines the profile: `0032` → `050-ms2.bin`, `0033` → `051-ms2.bin`, `0034` → `052-ms2.bin`. These are profile filenames, **not** a promise that any similarly named file is valid. Never rename a mismatched image to force compatibility. Unsupported hardware and rejected firmware must not be bypassed. See [RELEASE.md](RELEASE.md) for the actual validation coverage.

**How safe is it with only one SRSEII and one MS2?** The current public prerelease has performed identification on a real SRSEII with MS2 hardware type `0033`, but **has not completed a full firmware flash using the published binary**. Type `0032` and `0034` have only offline coverage. A failed flash could leave the MS2 unusable. If you cannot tolerate that risk, use only the read-only probe and wait for independently documented end-to-end flash results.

**What if power, USB/network access, or the CAN connection is interrupted?** A reliably tested resume or recovery procedure is **not documented or guaranteed** for this release. Do not assume that rerunning `update` will recover a partially programmed MS2. Keep power and CAN wiring stable; do not interrupt an update in progress. If one fails, preserve logs and the precise device/image information and seek device-specific recovery guidance before retrying. The SRSEII and MS2 should not be treated as automatically recoverable.

## Firmware

Firmware, locomotive database and language/resource images must be obtained separately from authorized sources. They are not redistributed in this repository.

## Privacy and attribution

The current sources were checked for personal network details and original test-device UIDs. The main Git history was rewritten as one sanitized root commit; older Git objects may still exist in caches or external copies. No firmware images, private captures or local build toolchains are intentionally included. Source files derived from third parties retain their existing attribution; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [src/ms2x-c/LICENSE.gerd](src/ms2x-c/LICENSE.gerd).

Licensed under MIT for project-original work; third-party terms remain applicable.

Independent project, not affiliated with or endorsed by Märklin.
