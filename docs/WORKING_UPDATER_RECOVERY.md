# De bewezen werkende MS2-updater teruggevonden

Onderzoeksdatum: 30 oktober 2026. Uitgangspunt: `fd684bb`.
Onderzoeksbranch: `recovery/working-updater-2026-10-28`.

**De oorspronkelijke live-updatebinary en de volledige bijbehorende bronset
zijn lokaal teruggevonden. Een geïsoleerde herbouw levert exact dezelfde
87.980 bytes op, bevestigd met SHA256 én een volledige bytevergelijking.**
De huidige productiecode, live-flashblokkades en README's zijn ongewijzigd.
Deze branch bevat uitsluitend dit onderzoeksrapport; de historische code is
niet over de huidige code heen gezet.

## 1. Bewijs van de succesvolle update

De oorspronkelijke projectmap bestaat nog: `/root/ms2-updater`. Deze map
heeft geen Git-repository. De bestanden, builddirectories, snapshots en
rapporten zijn behouden gebleven naast `/root/ms2-updater-public`.

De lokaal bewaarde sessie van 28 oktober bevat de volgende opeenvolging.
Tijden hieronder zijn UTC, overgenomen uit het sessielog; Amsterdam was UTC+1.

| Tijd op 28 oktober | Vaststelling en bewijs |
| --- | --- |
| 08:46:15 | Gebruiker bevestigt vooraf normale identificatie, fysieke bootloaderactivering, bootidentiteit met hardware `0033`, versie `0.1`, hash `0D61`, en normale herstart. Dit is nog geen firmwareflash. |
| 08:55:05 | Expliciete opdracht om alleen `live_update_enabled=0` naar `1` te wijzigen, bestaande controles te behouden en de Omega2+-binary te bouwen. De echte proef zou daarna handmatig plaatsvinden. |
| 08:56:56–08:57:19 | Bouwverificatie en oplevering van `omega2-live-update/ms2-updater.stripped`, 87.980 bytes, met de hieronder vermelde SHA256 en het beoogde pad `/tmp/ms2-updater-live` op de SRSEII. |
| 09:06:57 | Gebruiker bevestigt de eerste geslaagde echte update: MS2 60657, hardware `0033`, firmware `3.112` naar `5.6`, via de bestaande bootloaderroute. Firmware `5.6` is onafhankelijk bevestigd met een nieuwe CAN-ping. Gleisbox bleef `1.47`. |
| 09:16:38 | Oplevering van een **latere** completion-build, met kortere resourcefase en aangepaste eindstatus. Dit is niet de binary van de eerste geslaagde flash. |

De succesmelding van de gebruiker bevat deze updateruitvoer, na Ctrl+C:

```text
firmware_local=1
protocol_blocks_ack=1
start_local=1
app5.6=1
resources_offered=0
streams=0
installed_confirmed=0
CAN I/O failed or interrupted
```

De MS2 was toen alweer normaal opgestart. Ctrl+C onderbrak de aansluitende
resourcefase, niet de al geslaagde firmwareoverdracht. De oude CLI wachtte
180 seconden op resources en rapporteerde geen normale successtatus zolang
resource-installatie onbevestigd bleef. Dat verklaart de verwarrende afloop.

Lokale bewijsbronnen:

- `/root/.codex/sessions/2026/10/28/rollout-2026-10-28T08-07-25-01a18155-c815-7142-a882-c1acca3342ae.jsonl`, records 604, 626, 640, 645, 652 en 750. Vooral record 652 is de oorspronkelijke gebruikersbevestiging, inclusief bovenstaande uitvoer.
- `/root/ms2-updater/reports/31-ms2-live-update-readiness.md`.
- `/root/ms2-updater/reports/evidence/live-update-release/verification.json`, `source-change.diff` en `mips-build.log`.
- `/root/ms2-updater/reports/32-ms2-update-completion-and-resources.md` en `reports/evidence/update-completion/verification.json`.

**Bewijskracht:** de geslaagde hardware-update is expliciet door de gebruiker
bevestigd, destijds en in de huidige opdracht. De opgeleverde binary is
ondubbelzinnig geïdentificeerd en reproduceerbaar. In de gevonden stukken
staat geen afzonderlijke SHA256-meting van het daadwerkelijk op de SRSEII
uitgevoerde bestand en geen volledige CAN-opname van die flash. De koppeling
met de uitgevoerde binary berust daarom op de opeenvolgende oplevering en
gebruikersbevestiging. Een volledige reconstructie van ieder live ACK is
niet mogelijk. Het lokale sessielog blijft privé; apparaat-UID en adressen
zijn hier niet overgenomen.

## 2. Gevonden binaries

De belangrijkste bestanden staan onder `/root/ms2-updater/src/ms2x-c/build/`.
Alle onderstaande hashes zijn tijdens dit onderzoek opnieuw lokaal berekend.

| Bestand onder die buildmap | Bytes | SHA256 | Betekenis |
| --- | ---: | --- | --- |
| `omega2-live-update/ms2-updater.stripped` | 87980 | `15c54bd5160c7af882f16f4690c284d9b197a965ca4ed61d7da1b5fdce8ae857` | Oorspronkelijke vrijgegeven live-updater; teruggevonden en exact herbouwd. |
| `omega2-live-update/ms2-updater` | 515508 | `083f63ff3893e204bf76deb80cd0933243e9a69d5c80aef706d1e253df1baec8` | Bijbehorend origineel met debuginformatie; ook objecten en `libms2x.a` zijn aanwezig. |
| `omega2-completion/ms2-updater.stripped` | 87984 | `349b128c381a33835e078fe3cc5093c32f1cd9f394214ca92cea0db1f5c6d92d` | Correctie ná de geslaagde flash; geen gevonden bewijs van een nieuwe flash met deze build. |
| `omega2-boot-probe/ms2-updater.stripped` | 83856 | `bd90190ff1ed91cb32296f07ab425ec695f016aa2b12299635f7ba6c07012002` | Eerdere bootloaderdiagnostiek; niet de live-updatebuild. |

Beide eerstgenoemde stripped builds zijn statische ELF32 little-endian
MIPS32r2-executables voor de Omega2+, geen native programma's voor deze
onderzoeksmachine. Ze zijn niet uitgevoerd, ook niet via emulatie.

Overige bewaarde Omega2+-builds, steeds `ms2-updater.stripped`:

| Builddirectory | Bytes | SHA256 |
| --- | ---: | --- |
| `omega2-integrated` | 79620 | `39489ff13d6cbe2bae4d5277dd8bf62715a626a1f8e9f6702b0248ff5a6df86f` |
| `omega2-universal` | 80272 | `256d87d04b595b0911bb64c7160d8ea09277767ce39040960530a7645e48f19a` |
| `omega2-probe-filter-fix` | 80272 | `b484014482c295cbb7f8fe6b7a4db3ff2330fc819f804a0b8875d5b6d01963f6` |
| `omega2-resource-service` | 83856 | `f52ef9036c10cd2a979ea6b671f5648896b90f59794f5716a4506b06794b7e1f` |
| `omega2-resource-control` | 83856 | `d897b87c7ad4fac49a72f108765247f4e5f1de74e7568f6fc49390778fc668e8` |
| `omega2-resource-query` | 83856 | `27a2863edbbb6696f4ad36f6bc131de1534d345ab2ab9c47c6f41a3312fd40fa` |
| `omega2-resource-live` | 83856 | `b387134d99402e0357ed34fe8551ba1bd9d28e7d0e820326f1eee929ccc08e91` |

Er is bovendien een bestaande, ongetrackte publicatiebuild:
`/root/ms2-updater-public/src/ms2x-c/cross-build/omega2-static/release/ms2-updater.stripped`.
Deze is ook 87.984 bytes, maar heeft SHA256
`faa2384b10524e7652f417a4d8e4b494c018f4bfcf02e4d0c9a58b1fbd44cdb5`.
Hij is dus niet bytegelijk aan het oorspronkelijke live-origineel of de
completion-build. Een achtergebleven binary vertegenwoordigt niet automatisch
de huidige broncode of blokkades in `fd684bb`. Deze bestaande builddirectory
was al ongetrackt en is niet aangepast of meegenomen in de commit.

## 3. Exacte broncode en geslaagde herbouw

De bronset van de live-release wordt vastgelegd door
`/root/ms2-updater/reports/evidence/update-completion/before-sha256.json`.
Dit manifest is gemaakt vóór de completion-correctie. Alle **54** vermelde
bestanden komen nog overeen: 40 projectbestanden en 14 reeds aanwezige
externe firmware-/resourcebestanden. Voor vier projectbestanden is de
bewaarlocatie onder `reports/evidence/update-completion/before/` gebruikt.
Geen firmwarebestand is gedownload, gekopieerd naar de reconstructie of
opgenomen in Git.

De oorspronkelijke productiebron is de bestaande map
`/root/ms2-updater/src/ms2x-c/`, met precies deze drie snapshotbestanden:

| Bestand | Te gebruiken bron onder `/root/ms2-updater/` | SHA256 |
| --- | --- | --- |
| `linux_main.c` | `reports/evidence/update-completion/before/src/ms2x-c/linux_main.c` | `18bd25d5bc598e62c0a9b0519d9e20b269e50af207f5419bb2e167d3c789ab82` |
| `session.c` | `reports/evidence/update-completion/before/src/ms2x-c/session.c` | `4a6d4cbc69faedb49689080f85ad2c9a4ab632cbee67a0d385a1ca5f931b36d3` |
| `include/ms2x_session.h` | `reports/evidence/update-completion/before/src/ms2x-c/include/ms2x_session.h` | `12c7b1743f36e764f32df474514111e795ed81bcf898ab79286ca42c3dd42372` |

Voor de oorspronkelijke tests is daarnaast de snapshot van
`tests/test_can_sim_loop.c` beschikbaar; SHA256
`ea06b775a646b91fb568d52c2304880eb4d0c0df161f1f6122527c3ac1ae0584`.
Het huidige bestand in de oorspronkelijke projectmap bevat al de latere
completion-correctie. Blind die hele map opnieuw bouwen zou daarom een
andere versie opleveren.

De overige productie-/buildbestanden zijn op hun bestaande locatie behouden:

| Bestand onder `src/ms2x-c/` | SHA256 |
| --- | --- |
| `ms2x.c` | `e3cb2a0897b7b647b9d93ffdc4df04495986ada43788443fe9c859618109d5fc` |
| `sha256.c` | `4db09e23784455a41f217b3d20fefffe14bb1e75d7f56aa207a8dedbe5277032` |
| `socketcan.c` | `c141e22549bb184fc15ead1a2ef974c565b42330fde5f1b4870a63fa0fb6dca9` |
| `can_sim_loop.c` | `ed612b7a42a453c2b612a313fd26269630c4d1436890f85668540939dc46ea7b` |
| `can_offline.c` | `d79a36ce53a15606883871566fc47c25a27e7c626b654e3ed0975780fde0364d` |
| `resource_server.c` | `567ddcea3cc86b81c71e1d73f35c44a5339a664952acd7c29f7ae6369c0b99ce` |
| `offline.c` | `d64799316cb6288eab5f0326f5e5e71f7c43f3c496dd9992e83e4ae8db4289b1` |
| `include/ms2x.h` | `9ab983797884ad954cc8b3c84e72623a2b7f47c12217317a7954c5e6cce4d67f` |
| `include/ms2x_linux.h` | `d7f7d9ceba00ba562cca11c6ca8490958dd85ab8556b33e13a4acf42b9e3eef0` |
| `include/ms2x_resources.h` | `b554d56235a4740efd8f40c6a8e8d3d5e01e0a7bc2dba6c846c896e9924f7f4d` |
| `include/ms2x_can_sim_loop.h` | `56978c1451533c58b781b1b1c09b3483910dab6d772a213613eae6766f9dc240` |
| `include/ms2x_can_offline.h` | `775746a7fb07368872324c40c190ffcb4f64fa17b3c38179899cbba132366d8c` |
| `Makefile` | `67b1aae1a0cc30d4200f6cc1d58cc8beb1a1b0874c30613d730b45c55df88be6` |
| `Makefile.cross` | `ef857f9ff6f6cd098067b3d4384514bc096f4704d60e04dd0d2239ba02b4ea9a` |

Tijdens dit onderzoek zijn uitsluitend deze 17 productie-/buildbestanden
naar een nieuwe tijdelijke bronmap gekopieerd en tegen het historische
manifest gecontroleerd:

`/tmp/ms2-working-updater-20261028-caldpuyr/src/ms2x-c/`.

De oorspronkelijke compiler staat nog op het in het oude bouwlog genoemde
pad. De versie komt overeen met het bewaarde `compiler-version.txt`:
OpenWrt GCC **14.3.0 r32768-b21cfa8f8c**, toolchain **25.12.1**, musl,
`mipsel_24kc`. Vanuit uitsluitend die nieuwe bronkopie is dezelfde opdracht
als op 28 oktober uitgevoerd:

```sh
ms2_cross_prefix=/root/openwrt-sdk-omega2/openwrt-toolchain-25.12.1-ramips-mt76x8_gcc-14.3.0_musl.Linux-x86_64/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-
make BUILD=build/omega2-live-update \
  CC="${ms2_cross_prefix}gcc" AR="${ms2_cross_prefix}ar" \
  CFLAGS='-Os -g -EL -mabi=32 -mips32r2 -mtune=24kc -msoft-float -mno-mips16 -mno-micromips -ffunction-sections -fdata-sections -fno-common -fstack-usage' \
  LDFLAGS='-static -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack' \
  build/omega2-live-update/ms2-updater
"${ms2_cross_prefix}strip" --strip-all \
  -o build/omega2-live-update/ms2-updater.stripped \
  build/omega2-live-update/ms2-updater
```

Het historische `Makefile.cross` bouwde alleen `ms2x-offline`; de werkende
live-CLI werd met bovenstaand expliciet `Makefile`-target gebouwd. De
unstripped binary kan door andere debugpaden verschillen. De hier bewezen
reproduceerbaarheid betreft de **stripped executable**:

| Controle | Resultaat |
| --- | --- |
| Bouw- en stripcommando | Geslaagd, zonder downloads of targetuitvoering. |
| Herbouwde bestandsgrootte | 87.980 bytes. |
| SHA256 origineel, registratie van 28 oktober en herbouw | Alle drie `15c54bd5160c7af882f16f4690c284d9b197a965ca4ed61d7da1b5fdce8ae857`. |
| Volledige bytevergelijking origineel/herbouw | Gelijk. |
| Bestaande bestanden van het historische manifest | Ongewijzigd; alle 54 gecontroleerd. |

Nieuwe lokale bouwbewijzen staan in
`/tmp/ms2-working-updater-20261028-caldpuyr/build.log` en
`/tmp/ms2-working-updater-20261028-caldpuyr/reconstruction.json`.
Een tijdelijke map is geen duurzame back-up; de originele bronnen,
snapshots, binaries en oude bewijsstukken staan nog op hun oorspronkelijke
locaties. De tijdelijke uitvoer wordt niet gecommit.

## 4. Welke overdracht werkte en hoe werd die gebruikt?

Het bewezen geval is één MS2 60657 met hardware `0033`, vooraf fysiek in
bootloadermodus gezet. Het was een expliciete bootloaderupdate, geen bewijs
van herstel van een werkelijk beschadigde applicatie. De oude oplevering gaf
onderstaande opdracht; dit is uitsluitend historische documentatie en geen
opdracht om nu uit te voeren. De concrete privé-UID is vervangen:

```text
/tmp/ms2-updater-live update --interface can0 --target <doel-UID> --hardware 0033 --bootloader --firmware /opt/ms2-updater/resources/051-ms2.bin --resources /opt/ms2-updater/resources
```

De broncode gebruikt achtereenvolgens `ms2x_firmware_load_for`,
`ms2x_engine_new_target`, `ms2x_start_bootloader`, `ms2x_receive` en
`ms2x_tx_processed`; `ms2x_session_receive` controleert vervolgens de
teruggekeerde applicatieversie. Het profiel valideert `051-ms2.bin`,
159.219 bytes, header `05 06 00 33`, SHA256
`cf2d277e38f6f982ffd3b40dd1c372c594cd8b8592fb5234f3b4da6c6ccd2439`.
Dit bestand bestaat nog lokaal en komt overeen met het oude manifest.

Het pad vraagt een volledige bootidentiteit op en controleert UID en hardware.
Daarna volgen 156 blokken van 1024 bytes, aflopend `159..4`, met `FF`-padding.
Bootblokken `0..3` worden niet geschreven. De selector gebruikt `44,N`,
de datadelen `003B` en de blok-CRC `88,CRC_hi,CRC_lo`; het gerichte
applicatiestartcommando `11` volgt na de blokbevestigingen. De eigen zenderhash
is `4711`; de waargenomen bootloaderhash `0D61` is daarvan onafhankelijk.
Het profiel, de identiteit, firmware-SHA256, fasecontroles en time-outs waren
al aanwezig in deze oorspronkelijke liveversie.

De terugmelding `app5.6=1` en de afzonderlijke CAN-ping ondersteunen daadwerkelijk
functionerend opstarten na die overdracht. Ze vormen geen volledige readback
van alle flashbytes en bewijzen geen onbeperkte herstelbaarheid of werking
van de profielen `0032` en `0034` op echte hardware.

## 5. Git-geschiedenis en latere wijzigingen

De volledige lokaal beschikbare geschiedenis is onderzocht: **53 commits**
via alle refs en reflogs vóór deze rapportcommit, inclusief de bronimports
van 28 oktober. Er waren alleen `main` op `fd684bb` en de remote-trackingrefs
op `9dcbd05`, geen extra tags, stash, oude herstelrefs of extra worktrees.
`git fsck --full --no-reflogs --unreachable` eindigde succesvol zonder
onbereikbare objecten te melden. De reflog gaat terug tot de clone van
28 oktober. Geen gevonden Git-commit bevat de complete toestand van vóór de
eerste succesvolle flash: die toestand is behouden via de lokale snapshots.

Ook bestaande project-/backuplocaties onder `/root`, `/tmp`, `/opt`, `/srv`
en `/var/backups` zijn lokaal doorzocht op relevante projecten, Git-metadata,
updaterbestanden en bewijsstukken. De aanvullende SRSEII-projectmappen
leverden geen beter aan de geslaagde flash gekoppeld artefact op. Er is niet
gefetchd of van externe bronnen gelezen. De lokale projectmap en het
historische sessielog leveren de doorslaggevende vondsten.

| Moment/commit | Verschil ten opzichte van de werkende liveversie |
| --- | --- |
| Vrijgave op 28 oktober | Alleen de bestaande vrijgaveconstante in `linux_main.c` van `0` naar `1`; 54 overige beschermde inputs ongewijzigd. De helptekst vermeldde ten onrechte nog `DISABLED`. |
| Completion-correctie, direct na succes | Resourcevenster van 180 naar 30 seconden; help/eindrapportage en `ms2x_update_exit_status` aangepast. Applicatieverificatie wordt eerder vastgelegd bij fouten in de resource-opzet. Blokengine, firmwareprofielen, CAN-filters en hashverwerking blijven gelijk. |
| Eerste publieke bronimports, o.a. `91b959b`, `323962a`, `d4b5373`, `0b4f380` | Importeren de **completion-toestand**. `linux_main.c` krijgt resource-default `.` in plaats van het lokale absolute firmwarepad; de vergeleken overige modules verschillen alleen in de afsluitende newline. Dit is geen compleet nieuwe firmware-engine. |
| `1680680`, 28 oktober | Publieke cross-build bouwt naast de offline verifier ook de echte SocketCAN-CLI. De aangetroffen publicatiebinary is een afzonderlijke build met andere hash. |
| `f0c0551`, 30 oktober | Recovery-onderzoek en testontwerp. |
| `ff5a1c9`, 30 oktober | Recoverytests met synthetische RAM-peer; profielcatalogus uit `ms2x.c` naar `profiles.c` verplaatst. Geen bewijs van een nieuwe geslaagde hardwareflash. |
| `1c61210`, 30 oktober | Protocolaudit en extra offline tests, waaronder expliciete reproductie van ACK-ambiguïteit. |
| `fd684bb`, **30 oktober 20:20:58 +01:00** | Live-CLI-vrijgave terug naar `0`, exit `77` vóór discovery/bestand/interface; `ms2x_linux_open(...UPDATE)` weigert met `EPERM` en de schrijfroute weigert updatemodus. Extra conflictcontroles in sessielogica en tests. |

De blokkade is toegevoegd wegens niet bewezen ACK-versheid en bus-exclusiviteit,
zoals vastgelegd in de commit en
[de fase-4-analyse](BOOTLOADER_RECOVERY_ANALYSIS.md#14-fase-4--ack-validatie-en-bus-exclusiviteit-vanaf-1c61210).
Er is geen bijbehorend verslag van een mislukte echte flash gevonden.

Wat kan behouden blijven zonder het oorspronkelijke firmwareverkeer te
veranderen:

- Documentatie, gescheiden privé-/publieke bestanden, synthetische tests en vastgelegde build-/hashcontrole kunnen naast de exacte historische versie blijven bestaan.
- De completion-correctie raakt de fase ná de firmwareoverdracht en de eindstatus. Dit is de kleinste al bestaande bruikbaarheidsverbetering, maar produceert bewust een andere binary. De toenmalige gerichte native en sanitizer-tests zijn bewaard; een latere echte flash met deze build is niet aangetoond.
- De gepubliceerde buildvoorzieningen zijn nuttig voor latere verpakking; voor een exacte reconstructie worden de oorspronkelijke bronset en het oorspronkelijke bouwrecept gebruikt.
- Catalogussplitsing en nieuwe busconflictcontroles zijn niet nodig om het bewezen origineel terug te krijgen. Ze worden nu niet teruggeplaatst, teruggedraaid of samengevoegd. De huidige productieblokkades blijven staan.

## 6. Status van het CRC-ACK-probleem

**Er is geen aangetoonde invloed op de oorspronkelijke succesvolle flash.**
Er is wel een bestaande protocolbeperking, die al vóór die flash bekend en
getest was. Rapport 31 beschrijft expliciet dat een vijfbyte-CRC-ACK alleen
de UID en `88` bevat, zonder bloknummer, CRC-echo of sessienonce.

In de bewaarde native én ASan/UBSan-uitvoer van de live-release staat op regel
48 van respectievelijk `firmware-session-tests.log` en
`firmware-session-sanitize.log`:

```text
case 48 old-crc-ack-still-ambiguous: error=0 tx=20284 receipts=20284 rx=314 background=0 pings=0 max_rx=2 old_ack=1
```

De oorspronkelijke test `tests/test_can_sim_loop.c` injecteert daar een oude
CRC-ACK in een volgende geldige wachtfase en verwacht `MS2X_OK`. Dit is een
**bewuste beperkingstest**, geen bewijs van gewaarborgde ACK-versheid.
Diezelfde oude release registreerde native en onder ASan/UBSan 65
transportscenario's, 16 geïntegreerde sessies, core-/profieltests en tests
van de CAN-geheugenadapter als geslaagd. Deze historische tests zijn hier
gelezen, niet als nieuwe hardwaremetingen of nieuwe testruns gepresenteerd.

De latere RAM-recoverytest maakt de mogelijke gevolgschade explicieter:
een oude identieke ACK kan hostvoortgang bevestigen terwijl de gesimuleerde
peer het huidige blok niet heeft gecommit. Dat bevestigt een mogelijke fout
onder geïnjecteerde omstandigheden. De vijfbyte-ACK kan die situatie niet
onderscheiden van een echte nieuwe bevestiging in dezelfde wachtfase.

Voor de echte overdracht is geen oude/gedupliceerde CRC-ACK, verkeerd
bevestigd blok of concurrerende updater aangetoond. Zonder volledige live
trace kan ook niet achteraf worden bewezen dat ieder ontvangen ACK vers was.
De juiste conclusie is dus: **de updater werkte op hardware; de
protocolbeperking blijft bestaan; een daardoor veroorzaakte storing tijdens
die geslaagde update is niet waargenomen.** Er is geen grond om op basis van
de offline beperkingstest te stellen dat de succesvolle hardware-update
niet heeft plaatsgevonden of dat een nieuwe engine nodig is.

## 7. Minimaal herstelvoorstel en resterende stappen

1. Gebruik de teruggevonden live-release met SHA256 `15c54b…8ae857` als exacte
   historische referentie. Originele binary, bronmap, snapshots en bewijslogs
   blijven onaangeroerd. De reeds gemaakte geïsoleerde bronkopie en identieke
   herbouw zijn beschikbaar; opnieuw ontwikkelen is niet nodig.
2. Bewaar bij een volgende afzonderlijke overdracht naar een duurzame
   back-uplocatie de binary, de 17 bron-/buildbestanden, het historische
   manifest, de oude bouw-/testlogs en dit rapport samen. Neem privélogs en
   propriëtaire firmware niet op in de publieke repository. Deze opdracht
   commit uitsluitend documentatie; de bronset zelf wordt hier niet als
   nieuwe productieversie ingecheckt.
3. Vraag afzonderlijke toestemming voor daadwerkelijke heringebruikname van
   de historische livebinary of een eventuele latere wijziging van de
   huidige blokkades. Dit rapport heft geen blokkade op en autoriseert geen
   installatie, CAN-actie of flash. Voor de exacte historische binary is
   geen aanpassing van de huidige productiecode nodig.
4. Leg vóór zo'n apart geautoriseerde proef het gekozen artefact en de hash
   vast, verifieer de hash ook op het uitvoerende systeem, gebruik het juiste
   `0033`-profiel en de actuele volledige doelidentiteit, sluit een tweede
   updater/actieve bootloader uit en accepteer het beschreven ACK-restrisico
   expliciet. Bewaar uitvoer en een volledige CAN-trace en controleer de
   applicatieversie opnieuw. Dit zijn gerichte voorwaarden voor hergebruik,
   geen voorstel voor een nieuw recovery-framework.
5. Kies de bestaande completion-correctie alleen als de foutieve eindstatus
   en het lange resourcevenster vooraf moeten worden verbeterd. Die variant
   is volledig beschikbaar en offline getest, maar wordt afzonderlijk
   gelabeld; hij vervangt niet het bewijs van de oorspronkelijke binary.

**Wat ontbreekt?** Geen productiebron, executable of compiler om de
oorspronkelijke updater terug te krijgen: die zijn gevonden en de herbouw
is exact. Wel ontbreken een historische hashmeting op het uitvoerende
systeem, een volledige live-flashtrace en bewijs van echte recovery na een
beschadigde applicatie. Voor opnieuw gebruiken ontbreken op dit moment
uitsluitend de afzonderlijke vrijgave en de daarbij gekozen concrete
test-/gebruiksvoorwaarden; die worden niet door deze onderzoeksopdracht
vervangen. De huidige broncode blijft live-flashen weigeren.

## 8. Drie concrete antwoorden

1. **Oorspronkelijke werkende updater teruggevonden? Ja.** De live-release,
   de volledige bronset en de oorspronkelijke gebruikersbevestiging zijn
   aanwezig; de beperking van het ontbrekende uitvoeringshash is benoemd.
2. **Exacte versie nog bouwen of uitvoeren? Bouwen: ja, bewezen bytegelijk.**
   De bestaande en herbouwde MIPS-executable zijn beschikbaar voor de
   oorspronkelijke doelomgeving. Uitvoeren op de SRSEII is nu niet gedaan
   en vereist afzonderlijke toestemming.
3. **Kortste veilige route naar een bruikbare updater?** Stel de geïdentificeerde
   historische versie centraal en geef uitsluitend die bestaande binary
   afzonderlijk vrij voor gecontroleerd hergebruik, met hashcontrole en
   bewust geaccepteerd ACK-restrisico. Laat de huidige productiecode intact.
   Nieuwe functionaliteit of herbouw vanaf nul is hiervoor niet nodig.
