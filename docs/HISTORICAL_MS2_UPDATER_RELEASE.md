# Historical MS2 Updater — 2026-10-28

Release: [historical-ms2-updater-2026-10-28](https://github.com/Calimerorulez/ms2-updater/releases/tag/historical-ms2-updater-2026-10-28).
Recovery investigation: [docs/WORKING_UPDATER_RECOVERY.md](https://github.com/Calimerorulez/ms2-updater/blob/historical-ms2-updater-2026-10-28/docs/WORKING_UPDATER_RECOVERY.md),
originally recorded at commit `756a0d7`.

**Historical prerelease: successfully used once, not validated as generally
safe for new firmware updates. The known CRC-ACK freshness risk remains.**

## Preserved result

On 28 October 2026, the owner confirmed a successful update of a Märklin
MS2 60657, hardware `0033`, from firmware `3.112` to `5.6`, through the
SRSEII/Omega2+ and the physically activated bootloader. A separate CAN ping
confirmed application version `5.6`; the Gleisbox remained at `1.47`.

The released executable is the preserved original live-release binary,
copied without changing its contents. Its size is **87,980 bytes** and SHA256 is:

```text
15c54bd5160c7af882f16f4690c284d9b197a965ca4ed61d7da1b5fdce8ae857
```

The original source was reconstructed in the earlier investigation from
preserved files and snapshots and reproduced this executable byte for byte.
The release preparation rechecked those files and the existing reproduction.
The privacy-reviewed publication source also rebuilt to exactly the same
executable. The original files and original binary remain unchanged.

The historical evidence contains the owner's success report and updater
output. A complete live-flash capture and a separate hash recorded on the
executing SRSEII are unavailable. This release preserves that evidence level.
It does not establish recovery after actual application corruption or
hardware success for profiles `0032` and `0034`.

## Assets and source provenance

| Asset | Contents |
| --- | --- |
| `ms2-updater-linux-mipsel-omega2-historical-2026-10-28` | Original static ELF32 little-endian MIPS32r2 executable for Omega2+, 87,980 bytes. |
| `ms2-updater-historical-2026-10-28-source.tar.gz` | Historical CLI build sources, licenses, these instructions, per-file checksums and `PROVENANCE.json`. No firmware, private logs or build outputs. |
| `BUILDING-HISTORICAL.md` | These release/build instructions. |
| `SHA256SUMS` | SHA256 checksums for the three files above. |

The release tag points to the recovery branch's documentation commit. For
the sources matching the original executable, use the explicitly named
**historical source asset**. GitHub's automatic “Source code” archives contain
the repository tree at the documentation tag, whose production code retains
its current safety blocks.

The publication archive contains the 17 preserved source/build files needed
to retain the historical CLI source set. With the owner's explicit approval,
exactly two private fixture-UID occurrences were replaced by synthetic
`12345678`: the legacy `MS2X_UID` macro in `include/ms2x.h` and the offline
inspection output in `offline.c`. These are the only changes to the historical
source files. `PROVENANCE.json` records original and published hashes without
recording the private UID. The other 15 files are byte-identical to their
historical versions. The published CLI still uses runtime target selection;
the privacy edits leave its stripped binary byte-identical.

The archive is scoped to the CLI build sources. Historical test fixtures,
CAN captures, firmware/resources, toolchain and generated objects are not
included. Existing license notices, including `LICENSE.gerd`, are retained.
Firmware filenames and validation hashes in the code are metadata; no
proprietary Märklin firmware contents are distributed.

## Offline build

Verify the downloaded release files first:

```sh
sha256sum -c SHA256SUMS
tar -xzf ms2-updater-historical-2026-10-28-source.tar.gz
cd ms2-updater-historical-2026-10-28
sha256sum -c SOURCE_SHA256SUMS
cd src/ms2x-c
```

The verified toolchain was OpenWrt **25.12.1**, GCC **14.3.0
r32768-b21cfa8f8c**, musl, `mipsel_24kc`, targeting MIPS32r2/o32/soft-float.
Use an existing matching toolchain and set its absolute prefix below. No
firmware files, network access or target execution are needed to build the CLI.

```sh
ms2_cross_prefix=/path/to/toolchain/bin/mipsel-openwrt-linux-
make BUILD=build/omega2-live-update \
  CC="${ms2_cross_prefix}gcc" AR="${ms2_cross_prefix}ar" \
  CFLAGS='-Os -g -EL -mabi=32 -mips32r2 -mtune=24kc -msoft-float -mno-mips16 -mno-micromips -ffunction-sections -fdata-sections -fno-common -fstack-usage' \
  LDFLAGS='-static -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack' \
  build/omega2-live-update/ms2-updater
"${ms2_cross_prefix}strip" --strip-all \
  -o build/omega2-live-update/ms2-updater.stripped \
  build/omega2-live-update/ms2-updater
sha256sum build/omega2-live-update/ms2-updater.stripped
```

Expected result: the 87,980-byte file and SHA256 shown above. The claim of
byte identity applies to the stripped executable; debug paths may change
the unstripped file. The historical `Makefile.cross` targets the separate
offline verifier; use the explicit CLI target above. Its test targets require
additional historical fixtures and are outside this source archive's scope.

## Known risks and historical behaviour

The five-byte CRC-ACK contains only the target UID and `88`: no block number,
CRC echo or session nonce. An old identical ACK arriving during a later
valid CRC-wait phase cannot be distinguished from a fresh response. This
limitation was already documented and reproduced offline before the successful
hardware update. No resulting fault during that successful update was observed;
the missing full capture prevents an ACK-by-ACK retrospective check.

Firmware data parts contain no target UID. Target selection and identity
checks do not establish bus exclusivity or prevent interference from another
updater or active bootloader. The historical success does not remove these
risks. The current public updater's live-write blocks remain unchanged.

This is the pre-completion executable: after confirming application `5.6`,
it opens a 180-second resource phase and can report a failing/nonzero final
status despite a successful firmware phase. Its old help text still says
`DISABLED`, although this historical build has live updates enabled. The later
completion correction and later safety changes have not been folded into
this preserved executable. Do not infer permission or safety for another
flash from the historical success, help text or exit status.

Publication and verification involved no SRSEII/MS2 connection, CAN traffic,
firmware download, flash or execution of a target binary. The historical
release is separate from `main` and `v1.0.0-rc1`.
