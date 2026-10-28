# MS2 Updater

**Taal:** [English](README.md) · **Nederlands** · [Deutsch](README.de.md)

Onafhankelijke, door de gemeenschap onderhouden updater voor de Märklin Mobile Station 2 (MS2), via Linux SocketCAN.

**Status: [v1.0.0-rc1](https://github.com/Calimerorulez/ms2-updater/releases/tag/v1.0.0-rc1) is beschikbaar als prerelease.** Een statisch gelinkte binary voor SRSEII / Onion Omega2+ is gepubliceerd en op een echte SRSEII getest met een identificatiequery. Deze publieke binary is **niet opnieuw met een volledige firmware-update getest**. Gebruik firmware-updates op eigen risico.

Zie [RELEASE.md](RELEASE.md) voor de gecontroleerde SHA256, installatie-instructies en resterende beperkingen.

## Compileren

```sh
cd src/ms2x-c
make build/ms2-updater
./build/ms2-updater --help
```

Vereist een C11-compiler, `make`, Linux en SocketCAN-headers. Voor een OpenWrt-cross-build voor de Omega2+:

```sh
make -f Makefile.cross all CROSS_COMPILE=<pad-naar-je-toolchain-prefix>
```

`Makefile.cross` bouwt en controleert de offline-tool en maakt ook de echte statisch gelinkte SocketCAN-CLI aan in `cross-build/omega2-static/release/ms2-updater.stripped`.

De volledige historische testset gebruikt afzonderlijk verkregen officiële firmwarebestanden en enkele niet-openbare, geanonimiseerde CAN-opnames. Deze bestanden zijn bewust niet opgenomen. `make check` is daarom nog geen volledig zelfstandige test.

Twee tests kunnen zonder firmware en zonder CAN-hardware worden uitgevoerd:

```sh
make check-profiles
make check-firmware-reject
```

## Gebruik

Passief observeren (geen CAN-berichten verzenden):

```sh
./build/ms2-updater probe --interface can0 --seconds 10
```

Actieve identificatie (één identificatiequery op CAN):

```sh
./build/ms2-updater probe --interface can0 --seconds 10 --query
```

**Waarschuwing:** `update` en `serve-resources` kunnen de toestand van het aangesloten apparaat veranderen. Deze commando's zijn in de broncode ingeschakeld; gebruik ze pas als de doelapparaatselectie, firmware en installatieomgeving onafhankelijk zijn gecontroleerd. De prerelease bevat geen propriëtaire firmwarebestanden.

## Universele apparaatselectie

De CLI is niet afhankelijk van een vooraf vastgelegde MS2-UID. Hij ontdekt de MS2-apparaten tijdens gebruik. Als meerdere geschikte MS2-apparaten aanwezig zijn, selecteer je het bedoelde apparaat expliciet met `--target UID`.

De code bevat firmwareprofielen voor hardwaretypen `0032`, `0033` en `0034`. Het firmwarebestand moet precies bij het gedetecteerde hardwareprofiel passen. Onbekende revisies worden geweigerd. **Alleen type `0033` is met deze publieke binary op echte hardware getest; `0032` en `0034` zijn offline gecontroleerd.**

Historische offline-testfixtures gebruiken fictieve UID's; die dienen niet als vaste doelapparaten.

## Firmware

Firmware, locdatabases en taal- en resourcebestanden moeten afzonderlijk en rechtmatig worden verkregen. Ze worden niet via deze repository verspreid.

## Privacy, licenties en herkomst

De actuele broncode is gecontroleerd op persoonlijke netwerkgegevens en oorspronkelijke test-UID's. De Git-hoofdgeschiedenis is vervangen door één opgeschoonde begincommit. Eerdere objecten kunnen nog in GitHub-caches of externe kopieën aanwezig zijn.

Voor externe broncode gelden de oorspronkelijke licentievoorwaarden en naamsvermeldingen; zie [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) en [src/ms2x-c/LICENSE.gerd](src/ms2x-c/LICENSE.gerd).

Eigen projectbijdragen vallen onder de MIT-licentie. Dit project is onafhankelijk en niet verbonden aan of goedgekeurd door Märklin.
