# Offline testontwerp en uitvoering voor MS2-bootloaderherstel

Datum: 2026-10-30. Fase 2 bouwt voort op `f0c0551` en vervangt de daarin
beschreven tijdelijke driver door zelfstandige Make-targets. Alle bytes,
apparaatidentiteiten en CAN-events zijn synthetisch. Zie ook
[de analyse en resultaten](BOOTLOADER_RECOVERY_ANALYSIS.md#11-fase-2--uitgevoerde-offline-recovery-tests).

## 1. Uitvoeren

Vanuit `src/ms2x-c`, met alleen hostcompiler, make en Python 3:

```sh
make BUILD=/tmp/ms2-recovery-phase2 check-recovery
```

Dit draait `check-discovery`, `check-profiles`, `check-firmware-reject`,
`test-recovery` en de onafhankelijke cataloguscontrole. Geen downloads,
firmwarebestanden, private modules of capturedata vereist. De productie-CLI
wordt voor de cataloguscontrole gebouwd en als bytes gelezen, nooit gestart.
De RAM-testprogramma's linken geen `socketcan.c` of `linux_main.c`.

Ook uitgevoerd met ASan/UBSan en LeakSanitizer:

```sh
make BUILD=/tmp/ms2-recovery-phase2-sanitize CC=clang CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-fsanitize=address,undefined' WARN='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wwrite-strings -Wno-newline-eof' check-recovery
```

`-Wno-newline-eof` betreft ontbrekende eindnewlines in bestaande bestanden;
alle andere bestaande waarschuwingen blijven fouten. LeakSanitizer kan niet
onder ptrace draaien; deze uitvoering slaagde buiten de sandbox na expliciete
toestemming. Leakdetectie is niet uitgeschakeld.

## 2. Catalogus en validatie

De productiecatalogus en selector zijn zonder inhoudswijziging verplaatst
naar `profiles.c`. De Makefile linkt uitsluitend in `test-recovery` een
synthetische catalogus (`tests/recovery_profiles.c`) met dezelfde geometrie.
`ms2x.c`, `sha256.c`, `session.c`, `can_sim_loop.c` en `can_offline.c` zijn
voor tests en productie dezelfde objecten; er is geen afwijkende validator.

`tests/synthetic_image.h` genereert in RAM een patroon met een vaste header.
Lengte, versie, beide hardwaretypebytes en SHA256 moeten alle kloppen.
`tests/check_recovery_catalog.py` berekent de drie hashes onafhankelijk met
hashlib en leest de binaries: productie moet alle oorspronkelijke hashes
bevatten en geen synthetische hashes of profielnamen. De bestaande
productie-afwijzingstest weigert bovendien precies deze synthetische images.
Er is geen hash-mock, private firmwarestructuur, runtime-switch of force-optie.

## 3. Peer en persistentie

`tests/test_recovery.c` levert een kleine dynamische peer aan de vijf bestaande
`ms2x_sim_*`-seams. De echte sessie/loop/engine produceren alle hostframes.
De peer herkent normale/bootqueries, reset, selector, datadelen, CRC en start-11.
Hij controleert doel-UID, blok-/partvolgorde en CRC onafhankelijk bitgewijs.
De uiteindelijke applicatiebytes worden volledig vergeleken, inclusief padding.

De onderbreking is een EIO na de helft van de blok-ACK's en halverwege het
volgende blok. Daarna blijven applicatiebytes en geaccepteerde bloktellers
behouden. De oude engine moet terminaal blijven. Alleen een expliciet nieuwe
engine en bootidentificatie mogen een volledige nieuwe poging starten.
Alle eerder geaccepteerde blokken moeten nogmaals worden geschreven.

De test kruist commit bij correcte CRC versus wissen bij selector/direct
partial writes, en geen peerreset versus koude boot met lege vluchtige buffer.
In alle vier modelvarianten accepteert de peer herselectie van een blok.
Dit is een aanname; geen hardware-instructie of bewijs van fysieke flashatomiciteit.

## 4. Uitgevoerde scenariomatrix van fase 2

| Scenario | Uitkomst |
|---|---|
| Normale applicatie, 0032/0033/0034 | Volledige overdracht, passende runtime-identiteit/profiel, gerichte reset en appversie 5.6 gecontroleerd |
| Bootloader na onderbroken update | 12 combinaties geslaagd: drie profielen × twee flashmodellen × twee resetvarianten |
| Herstartpositie | Blok 95, 159 of 127; geen resume vanaf oude ACK-teller; alle bytes exact inclusief FF-padding |
| Verkeerde firmware | Lengte, hash, versie, hardware, kruisprofiel en onbekend type geweigerd; geen TX bij imageafwijzing |
| Time-out tijdens data/CRC | Afbreken zonder automatische retry of start-11 |
| Ontbrekende boot/selector/CRC-ACK | Geen data zonder identiteit en selector-ACK; terminale fout |
| Meerdere CAN-apparaten | Bestaande discoverytests slagen; twee bootloaders geven automatische ambiguïteit, expliciete UID kiest het bedoelde profiel |
| Onbekende identiteit | Nul/onbekend type, ontbrekend/kort bootantwoord en verkeerde UID/type geweigerd |
| F2-NACK | Afbreken, geen automatische blokretry |
| Applicatie keert niet terug | Laatste ACK/start wordt geen volledig succes; appverificatie faalt |

Resultaat: **19 discovery-scenario's / 278 controles**,
**28 productie-firmwareafwijzingen**, productieprofielcontroles en
**49 recoverytestgevallen / 1.755.540 controles** geslaagd, zowel native
als met de genoemde sanitizers. Geen falende testcase binnen `check-recovery`.

De oorspronkelijke discovery-fixture16 had per ongeluk dezelfde UID voor de
MS2 en een los apparaat van type0011. Alleen die tweede UID is gecorrigeerd.
De conflictcontroles voor dezelfde UID met verschillende typen blijven bestaan;
de veiligheidslogica is niet veranderd. Ook scenario's17–18 zijn nu uitgevoerd.

## 5. Grenzen en vervolg

Deze suite bewijst hostgedrag onder de gekozen peer-aannames. De historische
`make check` blijft afhankelijk van niet meegeleverde firmware, modules,
captures en een ontbrekende testheader; die bredere suite is niet uitgevoerd.
De oude negen tijdelijke probe-tests blijven als historisch resultaat in
`f0c0551` beschikbaar, niet als actuele testdriver.

Nog nodig: onafhankelijke bootloaderreacties en herprogrammeersemantiek,
SRSEII/SLCAN/PIC-timing en buffering, aanpak van oude identieke CRC-ACK's,
exclusiviteit bij UID-loze datadelen, expliciete autorisatie en runtimeconflicten.
De tests emuleren geen volledige MCU, flashcontroller, watchdog, onverwachte
bootloaderdefecten of busconcurrentie tijdens programmeren. Resources worden
niet aangeboden of geïnstalleerd. Een synthetische appidentiteit bewijst geen
functioneel herstel op een echte MS2. Hardwaretoegang en een schrijvende
recoverybediening zijn in deze fase niet uitgevoerd of toegevoegd.


## 6. Fase 3: gerichte protocolaudit

Vanaf `ff5a1c9` is alleen de bestaande RAM-peer uitgebreid. De bovenstaande
49 basisgevallen blijven bestaan. `check-recovery` voert nu twintig extra
gevallen uit: nulhash, F1–F6 als DLC5 en DLC8, CRC-ACK op4999/5000/5001 ms,
verkeerde selector, premature88, afwijkende hoge typebyte en een oude identieke
CRC-ACK in een latere wachtfase. Alle overdrachten controleren ook de logische
partpacing en de bestaande startpaden hun500-ms-wachttijden.

Actueel resultaat: **69 gevallen /2.372.864 controles**, plus de19 discovery-
gevallen,28 firmwareafwijzingen en cataloguscontrole; native en met de genoemde
sanitizers geslaagd. Gebruik voor deze fase bijvoorbeeld buildpaden
`/tmp/ms2-recovery-phase3` en `/tmp/ms2-recovery-phase3-sanitize`.

De oude-ACK-test slaagt door een beperking te reproduceren: na één echte
peercommit telt de host door een gereplayde ACK twee bevestigde blokken en
stuurt de derde selector. De test stopt daarna bewust met een I/O-fout,
zonder appstart. Dit is **geen veiligheidsfix of veilige-herstelclaim**.
Zie [de protocolaudit en bronvergelijking](BOOTLOADER_RECOVERY_ANALYSIS.md#13-fase-3--can-protocolaudit-vanaf-ff5a1c9).


## 7. Fase 4: fail-closed live grens en busconflicten

De69 gevallen van `1c61210` zijn eerst ongewijzigd herhaald en slagen.
Dezelfde gevallen blijven vervolgens met de strengere sessiecontrole slagen;
een vreemde boot-UID is nu een directe protocolfout. Veertien extra RAM-
gevallen toetsen dubbele ACK's, vreemde/lokale ACK's, meerdere MS2's,
hash-/versieconflicten inclusief cache, concurrerende data, late ACK na abort
en een gewone niet-MS2-peer die geen conflict veroorzaakt.

Actueel:83 RAM-gevallen /2.487.366 controles,19 discoverygevallen /335 controles,
28 firmwareafwijzingen, cataloguscontrole en709 identificatie/gate-controles
slagen, native en met ASan/UBSan/LeakSanitizer. De volledige bestaande wrapped
SocketCAN/CLI-harness slaagt eveneens (1191 controles). De ontbrekende
`linux_test_entry.h` is als declaratieheader hersteld; geen apparaten geopend.

Voer de gecombineerde suite uit met het eerdere commando en buildpad
`/tmp/ms2-recovery-phase4`. Voor alleen de nieuwe identificatiegrens:

```sh
make BUILD=/tmp/ms2-recovery-phase4 check-identification
```

De productie-CLI en de Linux UPDATE-adapter zijn nu zonder runtime-override
geblokkeerd, wegens onbewezen CRC-ACK-versheid en exclusiviteit. De bootprobe
blijft beperkt tot passief observeren of één expliciet gevraagde lege query.
De ononderscheidbare oude ACK kan in de offline parser nog steeds voortgang
simuleren; de live gate voorkomt gebruik van die parser op hardware via onze
adapter. De suite bewijst geen protocolreparatie en geen wereldwijde buslock.
Zie [de actuele garanties en beperkingen](BOOTLOADER_RECOVERY_ANALYSIS.md#14-fase-4--ack-validatie-en-bus-exclusiviteit-vanaf-1c61210).
