# MS2 Updater

See [RELEASE.md](RELEASE.md) for the verified Omega2+ release-candidate hash, deployment procedure, and remaining release restrictions.

Community-maintained, independent Märklin Mobile Station 2 (MS2) updater for Linux SocketCAN.

**Status: source preview — no v1.0.0 release yet.** The source has been privacy-sanitized and the native CLI build was verified. Full public test/release verification and target-device testing of the sanitized revision remain outstanding. Use firmware flashing at your own risk.

## Build

```sh
cd src/ms2x-c
make build/ms2-updater
./build/ms2-updater --help
```

Requires a C11 compiler, `make`, Linux and SocketCAN headers. For an OpenWrt Omega2+ cross-build, use `make -f Makefile.cross all CROSS_COMPILE=<your-toolchain-prefix>`. The included `Makefile.cross` builds and verifies the offline tool and also cross-compiles the real statically linked SocketCAN CLI to `cross-build/omega2-static/release/ms2-updater.stripped`. The target CLI still requires on-device read-only probe verification before a tagged release.

The full historical test harness depends on externally supplied official firmware and some non-public, redacted CAN captures. Those are deliberately excluded. Do not treat `make check` as a completely self-contained test at this stage.

## Usage

Read-only passive observation:

```sh
./build/ms2-updater probe --interface can0 --seconds 10
```

Active identification:

```sh
./build/ms2-updater probe --interface can0 --seconds 10 --query
```

The `update` and `serve-resources` commands can modify target state. They are enabled in this **source snapshot** and must not be used until the device/image selection and deployment are independently verified. No binary or firmware image is distributed here.

## Device-independent targeting

The production CLI does **not** depend on an author's device UID: it discovers MS2 identities at runtime. If more than one compatible MS2 is present, use `--target UID` to select the intended device. Profile definitions cover MS2 hardware IDs `0032`, `0033`, and `0034`; firmware images must exactly match the detected hardware profile. Unsupported revisions are rejected instead of guessed. Supporting these IDs in the source does not claim successful hardware testing on each revision.

Historical *offline test fixtures* use synthetic IDs and are not target selection.

## Firmware

Firmware, locomotive database and language/resource images must be obtained separately from authorized sources. They are not redistributed in this repository.

## Privacy and attribution

No personal LAN addresses, original test-device UIDs, private captures, firmware images, generated artifacts, or local build toolchains are intentionally included. Source files derived from third parties retain their existing attribution; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [src/ms2x-c/LICENSE.gerd](src/ms2x-c/LICENSE.gerd).

Licensed under MIT for project-original work; third-party terms remain applicable.

Independent project, not affiliated with or endorsed by Märklin.
