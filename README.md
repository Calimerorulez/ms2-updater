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

## Firmware

Firmware, locomotive database and language/resource images must be obtained separately from authorized sources. They are not redistributed in this repository.

## Privacy and attribution

The current sources were checked for personal network details and original test-device UIDs. The main Git history was rewritten as one sanitized root commit; older Git objects may still exist in caches or external copies. No firmware images, private captures or local build toolchains are intentionally included. Source files derived from third parties retain their existing attribution; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [src/ms2x-c/LICENSE.gerd](src/ms2x-c/LICENSE.gerd).

Licensed under MIT for project-original work; third-party terms remain applicable.

Independent project, not affiliated with or endorsed by Märklin.
