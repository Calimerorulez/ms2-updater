# SRSEII release candidate

The MS2 updater is a community project for the Märklin Mobile Station 2 using an SRSEII / Onion Omega2+ running an OpenWrt-based image.

## Verified behavior

- Source compiles on Debian using a native C compiler.
- Static MIPS32r2/o32/soft-float build produced by the included cross-build recipe.
- Stripped CLI executed on one real SRSEII. `probe --interface can0 --seconds 10 --query` discovered an MS2, hardware `0033`, application `5.6`, and selected firmware profile `051-ms2.bin`. Unrelated devices were not selected.
- Profile mapping tests pass for `0032` (`050-ms2.bin`), `0033` (`051-ms2.bin`) and `0034` (`052-ms2.bin`); unknown types are rejected.
- 25 offline negative firmware checks reject fabricated or mismatched images.

**Limits:** 0032/0034 were not hardware-tested. The build from the public checkout was NOT proven to complete a firmware flash; only the earlier source-based updater successfully performed a live update. Do not describe the release candidate as fully validated for flashing.

## Release candidate asset

The tested stripped executable was produced at:

```text
src/ms2x-c/cross-build/omega2-static/release/ms2-updater.stripped
```

SHA256 as measured on the Debian build host:

```text
faa2384b10524e7652f417a4d8e4b494c018f4bfcf02e4d0c9a58b1fbd44cdb5
```

Before uploading, verify that **this exact binary** has the digest above. The digest printed on the SRSEII has not yet been independently recorded here.

## Build from source

```sh
cd src/ms2x-c
make check-profiles
make check-firmware-reject
make -f Makefile.cross all CROSS_COMPILE=/absolute/path/to/mipsel-openwrt-linux-
sha256sum cross-build/omega2-static/release/ms2-updater.stripped
```

Never add real firmware images or personal CAN traces to Git.

## Install and probe (no flash)

```sh
scp ms2-updater-linux-mipsel-omega2 root@YOUR-SRSEII:/tmp/ms2-updater
ssh root@YOUR-SRSEII
chmod 755 /tmp/ms2-updater
sha256sum /tmp/ms2-updater
/tmp/ms2-updater probe --interface can0 --seconds 10 --query
```

The default `probe` mode is passive; `--query` emits a single identification ping. Do not run `update` or `serve-resources` as a casual installation check; these are write-capable commands.

## Uploading the GitHub release manually

1. Validate asset SHA256 against the candidate hash above.
2. Resolve historical repository privacy exposure first: some earlier Git commits contain original device UID bytes even though the current source fixtures are synthetic. Merely deleting or rewriting the current files does not remove old objects. A coordinated history rewrite is required; never claim complete removal from forks or caches.
3. Once the release is approved, open the GitHub repository **Releases** page and choose **Draft a new release**.
4. Create a pre-release tag (e.g. `v1.0.0-rc1`) at the reviewed commit. Mark it as **pre-release**.
5. Upload the verified binary renamed `ms2-updater-linux-mipsel-omega2` plus a text file `SHA256SUMS` whose digest was generated from the actual uploaded filename.
6. In release notes, distinguish the successful probe on SRSEII from the untested public-version firmware flash.

The proprietary Märklin firmware images are not included or redistributed.
