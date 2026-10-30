# MS2 bootloader recovery — offline analyse

Datum: 2026-10-30. Repository: `Calimerorulez/ms2-updater`.
Oorspronkelijke analyse: `9dcbd05`, vastgelegd in `f0c0551`.
Fase 2 bouwt voort op `f0c0551`; de resultaten daarvan staan in §11.
Fase 3 bouwt voort op `ff5a1c9`; de protocolaudit en het toenmalige testadvies
staan in §13. Advies: **nog niet klaar voor een schrijvende hardwaretest**.
Fase 4 vanaf `1c61210` staat in §14: live firmware-updates worden nu vóór
interfacegebruik geweigerd; passieve identificatie blijft mogelijk. De
oudere implementatiebeschrijvingen zijn historische snapshots, geen override
van deze actuele schrijfgrens.
De protocolanalyse en oorspronkelijke regelnummers in de overige paragrafen
verwijzen naar de eerste onderzoeksrevision. De catalogus is in fase 2
ongewijzigd verplaatst van `ms2x.c` naar `profiles.c`.

## 1. Conclusie en onderzoeksgrens

**Conclusie: gedeeltelijk ondersteund.** De huidige updater heeft al een
expliciet bootloaderpad, hardwareprofielen en een gedeelde blokoverdracht.
Een werkende applicatiefirmware is voor dat pad niet noodzakelijk **mits de
bootloader een volledige, passende CAN-identiteit teruggeeft**. De lokale
CS3-help documenteert bovendien MS2-herstel na een onderbroken update.
**Fase 2 toont nu met de echte hostengine en een synthetische RAM-peer aan
dat een nieuwe bootloadersessie alle blokken opnieuw kan aanbieden na een
halverwege afgebroken overdracht, voor 0032, 0033 en 0034.** Dit blijft bewijs
van hostgedrag onder modelaannames, geen gemeten herstel van een echte MS2.
De audit in fase 3 bevestigt de basisframes vanuit referentiesoftware, maar
geen volledige 0033/0034-recoverydialoog. Een aanvullende test reproduceert
de bekende oude-CRC-ACK-ambiguïteit; groene tests betekenen geen opgeloste
veiligheidsbeperking. Productiecode is in fase 3 niet aangepast.
In fase 4 is het operationele risico afgeschermd met een gesloten live-updatepad
en is zichtbare businterferentie een terminale fout. De byte-identieke oude
CRC-ACK blijft in de offline parser ononderscheidbaar van een nieuwe ACK;
dit rapport claimt niet dat het protocol zelf is gerepareerd.

Een betrouwbare, veilig vrij te geven recoveryfunctie via SRSEII/Omega2+ is
hiermee nog niet aangetoond. Vooral daadwerkelijk opnieuw programmeren na
een onderbreking, identiteit en bus-exclusiviteit tijdens de overdracht,
transporttiming en controle na afloop missen bewijs of aanvullende maatregelen.
Een expliciete recoverybediening kan waarschijnlijk met beperkte aanpassingen
op de bestaande engine worden gebouwd. De omvang van het ontbrekende
apparaatbewijs kan niet uit de omvang van die codewijziging worden afgeleid.

Alle werkzaamheden voor dit rapport waren offline: bronanalyse, lezen van
reeds lokaal aanwezige documentatie/referentiebron en native hosttests met
synthetische data. Geen netwerkverbindingen, firmwaredownloads, firmwarebuilds,
hardwaretoegang, CAN-interfaces, resets, flashes, releases of publicaties.
Fase 1 wijzigde geen productiecode. Fase 2 verplaatste alleen de vaste
profielcatalogus en selector naar een apart bronbestand om tests afzonderlijk
te kunnen linken. In fase 4 veranderen de live gates, transportweigering en
sessieconflictcontroles; firmwarevalidatie en wire-protocol blijven behouden.
README's zijn niet gewijzigd. Bestaande
untracked cross-buildartefacten zijn niet uitgevoerd of opgenomen.

## 2. Bewijsklassen en geraadpleegde bronnen

| Label | Betekenis | Wat het niet bewijst |
|---|---|---|
| **CODE** | Gedrag rechtstreeks afgeleid uit de huidige publieke C-bron | Dat echte MS2-hardware dezelfde antwoorden produceert |
| **TEST** | Tijdens dit onderzoek uitgevoerd met synthetische RAM-input | Flashsemantiek, elektrische werking of herstel op een apparaat |
| **DOC** | Reeds aanwezige oorspronkelijke CS3-help | Dat dezelfde procedure via onze SRSEII al werkt |
| **REF** | Lokaal aanwezige upstreambron of statische CS3-analyse | Succesvolle uitvoering op iedere hardware-/bootloaderversie |
| **AANNAME / OPEN** | Gevolgtrekking of ontbrekend bewijs | Geen ondersteund protocolcontract |

De gehele tracked bestandsinventaris is bekeken: hoofdbron, headers,
transporten, resourceserver, CLI, build-/cross-auditcode, tests en rootdocumenten.
De relevante concrete bronnen zijn:

1. [ms2x.c](../src/ms2x-c/ms2x.c), [ms2x.h](../src/ms2x-c/include/ms2x.h),
   [session.c](../src/ms2x-c/session.c),
   [ms2x_session.h](../src/ms2x-c/include/ms2x_session.h),
   [linux_main.c](../src/ms2x-c/linux_main.c),
   [socketcan.c](../src/ms2x-c/socketcan.c),
   [can_offline.c](../src/ms2x-c/can_offline.c) en
   [can_sim_loop.c](../src/ms2x-c/can_sim_loop.c).
2. Oorspronkelijke lokale CS3-help, logisch pakketpad
   `hilfe/en/29-rescuedevice/bootloaderupdate.html`, regels 24–45,
   titel *Device Recovery*. SHA256:
   `2df4314fcc73e27ead285ac78cbcff628d0d49c4fc04193d9675554736998432`.
   Deze bestaande lokale documentkopie is gelezen en gehasht; niet gedownload,
   gewijzigd of aan deze repository toegevoegd.
3. Reeds lokaal aanwezige
   [Gerd Bertelsmann, gb2-update.c, gepinde upstreamrevision](https://github.com/GBert/railroad/blob/a427e6528922e7f17e4eebfb9c9af68599bb2268/can2udp/src/gb2-update.c).
   SHA256 van de gelezen bronkopie:
   `5b0568e95555391eaf2927d5c08f56b3c8e1109099511ce495cb70de643d8377`.
   Relevant: regels 120–129, 382–431 en 435–527. De link is provenance;
   tijdens dit onderzoek is de website niet bezocht. Beer-Ware-attributie:
   [LICENSE.gerd](../src/ms2x-c/LICENSE.gerd).
4. Reeds aanwezige lokale onderzoeksrapporten 02, 03, 04, 07, 09, 10, 11,
   19 en 30 uit de eerdere onderzoekskopie. Daarvan zijn gerichte bestaande
   CS3-disassemblyuitsneden gecontroleerd: `phase09/boot-start.txt`,
   `boot-option-read.txt`, `boot-option-copy.txt` en `identity-boot.txt`.
   Ze verwijzen naar CS3 `event`, SHA256
   `4baf1b751d92cc62e2b67d1c235d84be0118209c4447856ac9eeb741a32fe2a2`.
   De oude rapporten beschrijven andere ontwikkelstadia en zijn geen actuele
   beschrijving van deze checkout. Het CS3-programma is niet uitgevoerd.

De externe lokale documenten en disassembly zijn niet meegeleverd in de
publieke repository. Een lezer kan hun inhoud hier dus niet zelfstandig
reproduceren; hashes en locaties maken de bewijslimiet expliciet. Hun private
apparaatidentiteiten, opnamen en infrastructuurgegevens zijn niet overgenomen.
Geen online protocoldocumentatie of forumclaims zijn als nieuw geverifieerd
bewijs gebruikt. Er is geen lokaal aangetroffen officiële CAN-specificatie
die alle hieronder gebruikte recoverydetails normatief vastlegt.

## 3. Huidige ontdekking en selectie

**CODE — `ms2x_identify`, ms2x.c:72–85.** De decoder accepteert extended
klassieke CAN-frames. Een `0031hhhh`-antwoord met DLC8 geldt als normale
identiteit; `0037hhhh` met DLC8 als bootidentiteit. Payload:

`UID[4, big endian] | gerapporteerde major | minor | hardwaretype[2, big endian]`.

UID nul en `FFFFFFFF` zijn ongeldig. Het lage CAN-ID-veld `hhhh` is een hash,
geen apparaat-UID of transactienummer. Er wordt geen UID-prefix geraden of
vereist. Bootversie is niet noodzakelijk applicatieversie 5.6 of versie 0.0;
app- en boothash hoeven niet gelijk te zijn. Korte boot-ACK's tellen niet als
identiteit. F1..F6 worden ook bij DLC8 van identiteitsherkenning uitgesloten.
Dit is syntactische herkenning, geen authenticatie of sluitend bewijs dat een
ander CAN-apparaat dit formaat nooit kan gebruiken. Andere DLC8-statusvormen
zijn niet afzonderlijk gedocumenteerd of allemaal uitgesloten.

**CODE — `remember` en `ms2x_select_device`, session.c:19–67.** Discovery
bewaart maximaal 16 apparaten en koppelt normale en bootidentiteit via UID.
Verschillende typen voor dezelfde UID, gewijzigde hash/versie binnen dezelfde
modus en gelijke hashes bij verschillende UIDs veroorzaken ambiguïteit.
Ongeldige identiteiten en tabeloverflow blokkeren selectie. Zonder doel-UID
wordt precies één ondersteund, eenduidig apparaat automatisch geselecteerd.
Met doel-UID wordt alleen dat apparaat gekozen; een conflict wordt niet
overruled. Niet-ondersteunde apparaten worden overgeslagen bij automatische
selectie; expliciet kiezen ervan geeft `MS2X_SELECT_UNSUPPORTED`.

**CODE — CLI, linux_main.c:53–162.** `probe` is standaard passief.
`probe --query` zendt eenmaal een normale ping. Een expliciete combinatie
`probe --bootloader --query` produceert eenmaal een lege bootquery met een
afzonderlijke diagnostische hash `5A22`. Dit beschrijft broncode, geen advies
om nu een hardwarecommando uit te voeren.

Bij `update` duurt de eerste discovery standaard 10 seconden en wordt normaal
een ping verzonden. Bij `update --bootloader` is die discovery juist **passief**
(`config.query=!config.bootloader`, regel 115). Daarom:

- Een spontaan zendende bootloader met volledige identiteit kan worden gevonden.
- Een bootloader die alleen op een query antwoordt wordt in die eerste fase
  niet actief gevonden. De aparte bootprobe kan hem wel bevragen.
- Zonder selectie is alleen de combinatie bootloader + expliciete doel-UID +
  ondersteund handmatig hardwaretype een toegestane vervolgroute
  (regels 145–147). Daarna wordt firmware gevalideerd en stelt de engine een
  bootquery. Een **nieuw volledig passend antwoord blijft verplicht**.
- Deze fallback is expliciet binnen bootloadermodus; er is geen automatische
  overgang van normale update naar recovery bij een ontbrekende ping.

Een geslaagde probe-exit betekent alleen dat het observatievenster zonder
transportfout is geëindigd. `selection`, gevonden identiteiten en conflicten
moeten afzonderlijk worden bekeken. Geen antwoord is geen diagnose
'bootloader beschadigd'.

## 4. Firmwareprofielen en schrijfbereik

**CODE — `ms2x_profile_for`, oorspronkelijk ms2x.c:10–30, nu
[profiles.c](../src/ms2x-c/profiles.c); firmwarevalidatie in `ms2x.c`.**

| Hardware-ID | Profielbestand | Originele bytes | Versie-/typeoffset | Aflopende blokken | Blokken × 1024 | FF-aanvulling |
|---|---|---:|---:|---|---:|---:|
| `0032` | `050-ms2.bin` | 93500 | 252 | 95..4 | 92 = 94208 bytes | 708 bytes |
| `0033` | `051-ms2.bin` | 159219 | 0 | 159..4 | 156 = 159744 bytes | 525 bytes |
| `0034` | `052-ms2.bin` | 126092 | 0 | 127..4 | 124 = 126976 bytes | 884 bytes |

Ieder profiel heeft zijn eigen exacte SHA256; de waarden staan in de genoemde
bron, niet in een extern te kiezen manifest. De loader controleert exact
bestandsgrootte, versiebytes 5 en 6, beide hardwaretypebytes en volledige hash.
Een bestandsnaam of gebruikerskeuze alleen is onvoldoende. Gevalideerde bytes
worden in een eigen buffer gekopieerd; vervolgoverdracht leest die buffer.
Dit is integriteitscontrole tegen een vaste catalogus, geen aangetoonde
vendorsignatuur of onbeperkte ondersteuning voor andere firmwareversies.

Blok N leest vanaf `(N-4)*1024` in het image. De eerste vier imageblokken
worden dus **niet** overgeslagen; logische apparaatblokken 0..3 vallen buiten
deze applicatieoverdracht. Er is geen procedure om de bootloader te vervangen.
Dat het profiel vier bootblokken reserveert bewijst geen fysieke
schrijfbeveiliging, exacte MCU-adressen of overleving van ieder stroomincident.

Profielen bevatten geen doel-UID (`uid=0`, legacy/reserved). De operationele
constructor `ms2x_engine_new_target` vereist een runtime-UID en passend type.
De convenienceconstructor `ms2x_engine_new` en `offline.c` gebruiken nog een
synthetische fixture-UID; die mogen geen bron voor recoveryselectie worden.

## 5. CAN-protocol en huidige normale update

Onderstaande ID's zijn 29-bit CAN-identifiers zonder Linux EFF-vlag. De
code herkent responses met masker `0x1fff0000`; dit zijn concrete wire-ID's,
niet een tweede set protocollogische commandonummers. `H=4711` is de huidige
engine-zenderhash; `U` is de geselecteerde runtime-UID.

| Stap | CAN-ID | DLC | Payload / controle | Bronfunctie |
|---|---|---:|---|---|
| Normale ping | `00304711` | 0 | Leeg; antwoord `0031hhhh`, DLC8, U/versie/type | `ms2x_start`; discovery in session |
| Reset naar bootpad | `00004711` | 6 | U, `80 FF` | `ms2x_receive`, `WAIT_PING` |
| Bootquery na 500 ms | `00364711` | 0 | Leeg; eventuele identiteit `0037hhhh`, DLC8 | `ms2x_advance`, `RESET_DELAY` |
| Blokselectie na nog 500 ms | `00364711` | 6 | U, `44 N` | `select_block` |
| Selector-ACK | `0037hhhh` | 6 | U, `44 N`, exact huidig blok | `ms2x_receive`, `WAIT_SELECT` |
| 128 datadelen | `00360300`..`0036037F` | 8 | Acht imagebytes per deel; **geen UID** | `data_part` |
| Blok-CRC | `00364711` | 7 | U, `88 CRC_hi CRC_lo` | `crc_request` |
| CRC-ACK | `0037hhhh` | 5 | U, `88`; geen bloknummer of nonce | `ms2x_receive`, `WAIT_CRC` |
| Applicatie starten, na laatste ACK | `00364711` | 5 | U, `11` | `ms2x_tx_processed` / `ms2x_receive` |
| Negatieve status | `0037hhhh` | ≥5 | U, F1..F6 → afbreken | `ms2x_receive` |

CRC is CRC16/CCITT-FALSE: init FFFF, poly 1021, geen reflectie/eind-XOR, over
alle 1024 bytes inclusief FF-padding. Eerste datadeel direct, vervolgens
maximaal vijf delen per logische 10 ms-stap. Dit is hostplanning; geen bewijs
dat alle delen dan al via SLCAN/PIC op CAN zijn verzonden.

De CLI hergebruikt de normale discovery-identiteit via `ms2x_start_known`;
er is dan geen tweede ping vóór de reset. De reguliere engine vereist een
passende normale identiteit, maar wacht na reset en query **niet verplicht
op een bootidentiteit**. Een ontvangen bootidentiteit wordt wel gecontroleerd;
de timer start anders toch de eerste selector. Pas de selector-ACK laat data
toe. Dit verschil met het striktere directe bootpad is essentieel.

Na de laatste CRC-ACK volgt gericht start-11. De engine bereikt dan
`MS2X_COMPLETE_UNVERIFIED`: lokale zending en blok-ACK's, geen bewezen
werkende applicatie. De sessielaag wacht maximaal 15 seconden op een normale
identiteit met dezelfde UID/type en versie 5.6. Na 500 ms kan één ping worden
verzonden als die identiteit nog ontbreekt. Daarna begint in de huidige CLI
automatisch een resourcevenster van maximaal 30 seconden.

`resource_server.c` behandelt op aanvraag taal/database/mfx en opgeslagen
updatebestanden via `0040` (request), `0041` (echo) en `0042` (stream). Dat
loopt via de applicatie en is geen bootloaderblokprotocol. Deze server heeft
geen gelijkwaardige verplichte volledige firmwarehashvalidatie voor al zijn
catalogusbestanden. Recovery mag hem daarom niet gebruiken als alternatief
voor de gevalideerde engine. `resources_confirmed` levert geen bewezen
installatiebevestiging; appversie 5.6 bewijst ook niet alle functies/resources.

## 6. Time-outs, fouten en ACK-beperkingen

**CODE — `check_time`, `fail`, `ms2x_tx_processed`, `ms2x_receive`.**
Standaard fasebudget: 5000 ms. Deadlines gebruiken monotone logische tijd;
een antwoord op of na de deadline is te laat. De datadelen delen één absoluut
budget dat niet door ieder deel wordt vernieuwd. Een teruglopende klok of
overflow leidt tot afbreken. `ABORTED` verwijdert de pending TX en is terminaal.

Er zijn geen automatische protocolretries voor F1..F6, ontbrekende ACK's of
een volledige update. Alleen een nog niet geaccepteerde lokale write met
EAGAIN/EWOULDBLOCK mag hetzelfde pending item opnieuw aanbieden binnen de
bestaande deadline. Korte writes, ENOBUFS, write-EINTR en overige onzekere
schrijffouten zijn fataal; read/poll-EINTR wordt met deadlinecontrole hervat.
RX-flood krijgt maximaal acht records vóór een TX-kans. CAN-errorframes,
pollfouten, verbindingverlies en SIGINT/SIGTERM stoppen het lokale procespad.
Dit annuleert geen reeds in kernel/bridge/controller gebufferde frames en
herstelt geen al gewiste of geprogrammeerde bytes op het apparaat.

Lokale receipts controleren owner, sequence en exacte frame-inhoud. Een
succesvolle SocketCAN-`write` bewijst acceptatie door de kernel, geen fysieke
CAN-verzending, CAN-link-ACK of bootloader-programmering. Alleen geldige
protocollaire selector- en CRC-antwoorden laten de engine verdergaan.

Een CRC-ACK `U,88` bevat geen bloknummer, CRC of sessienonce. Directe
duplicaten en onverwachte ACK's worden geweigerd, maar een oude identieke
ACK die precies in een latere `WAIT_CRC` valt is niet te onderscheiden.
De bron benoemt deze beperking op ms2x.c:366–367. De senderhash als fictief
transactie-ID behandelen lost dit niet op. Queue-draining en een rustige bus
verkleinen risico, maar leveren geen bewijs van ACK-versheid.

Huidige kernfoutcodes: `MS2X_INVALID`, `FIRMWARE`, `MEMORY`, `IO`, `PROTOCOL`,
`TIMEOUT`, `CLOCK`, `TERMINAL` (de laatste is wel gedeclareerd, geen actieve
recoveryclassificatie). CLI-selecties: NONE, AMBIGUOUS en UNSUPPORTED.
CLI-exits zijn grof: 2 voor meerdere configuratie/selectie/imagefouten,
1 voor firmware-/transport-/verificatiefouten, 0 bij verificatie en schoon
resourcevenster, 3 bij geverifieerde firmware plus resourcefasefout.
Exit77 is een uitgeschakelde live-gate; de huidige bron zet beide live-gates
echter op 1. Er is geen aparte gebruikersbevestiging vóór iedere actie.

## 7. Wat upstream en CS3 werkelijk onderbouwen

**DOC.** De oorspronkelijke CS3-help beschrijft een afgebroken update,
bijvoorbeeld door stroomverlies, als mogelijk herstelbaar door opnieuw
updaten. Voor MS2 beschrijft zij een spanningsloze start door aansluiten op
een draaiende Central Station terwijl de draaiknop is ingedrukt; daarna
loslaten, waardoor de MS2 in de bootloader blijft. Zij noemt herstel een
mogelijkheid, geen garantie. Dit wordt hier alleen als bewijs beschreven;
de procedure is niet uitgevoerd en de tekst autoriseert geen hardwareproef.

**REF.** De gecontroleerde statische CS3-uitsneden ondersteunen:

- Bootidentificatie met een volledig 16-bit type: `event` 0x526a2..0x526ac;
  hardwarecataloguslookup bij 0x52730.
- Bootquery en een 500-ms-timer naar `startUpdate2`: 0x411e8..0x41290.
- Instelling `bootloaderUpdate` naar veld +0x51: 0x896de..0x896fe en
  0x42284..0x42288. De bestaande analyse van de vervolgroute
  0x527bc..0x527d4 beschrijft het onderdrukken van automatisch start-11.

De eerder gereconstrueerde generieke `DeviceUpdater` verzendt de gewone
blokken en heeft een gerichte start-11-afronding. Dat wijst op hergebruik van
de applicatie-updater, niet op een verplicht speciaal recovery-image of het
overschrijven van de bootloader. De precieze peerreactie is hiermee niet
gemeten. Geen volledige onafhankelijke audit van de CS3-binary is in deze
opdracht opnieuw uitgevoerd.

**REF.** Gerds bron bevat dezelfde basisframes, 1024-byte MS2-blokken en
CRC-overdracht. Zijn FSM leidt het doel eerst af uit een normale ping en
accepteert voor bootidentificatie alleen de laatste typebyte 10, 11 of 32;
dat bewijst geen 0033-/0034-recovery. Zijn afronding F5 plus start-11 met
nul-UID wijkt af van onze gerichte start-11. Die procedures mogen niet zonder
bewijs worden gecombineerd. Zijn F1/F2-pad breekt af. De eerdere CS3-analyse
beschrijft daarnaast begrensde F2-blokretries; dat is geen bewijs voor veilige
retry-idempotentie op een MS2 na stroomverlies. De huidige C-engine kiest abort.

**AANNAME.** Een intacte, zelfstandig reagerende bootloader kan waarschijnlijk
dezelfde applicatieblokken opnieuw ontvangen zonder werkende applicatie en
zonder fysieke CS2/CS3. De wire-overdracht bevat geen aangetoonde verplichte
CS-authenticatie. Afwezigheid in de onderzochte code is echter geen volledig
bewijs dat iedere bootversie en SRSEII-route dezelfde voorwaarden hebben.

## 8. Expliciete antwoorden over herstelbaarheid

| Vraag | Antwoord en bewijslimiet |
|---|---|
| Vindt de huidige updater een MS2 met alleen bootloader? | **Voorwaardelijk ja.** Parser, selectie en directe bootstart bestaan. Passieve updatediscovery vindt alleen spontane identiteiten; een actieve bootprobe of de expliciete UID/type-route kan een query-afhankelijk doel bereiken. Een complete passende `0037` blijft vereist. TEST bewijst herkenning van synthetische frames, geen echte response. |
| Kan de bootloader een compleet bestand ontvangen? | De engine kan het volledige gevalideerde **applicatie-image** in alle profielblokken aanbieden, inclusief padding; niet het bootgebied of alle resources. DOC ondersteunt opnieuw updaten via bootloader. Werkelijke volledige ontvangst/programmering via SRSEII is OPEN. |
| Kan een gedeeltelijke update opnieuw worden gestart? | **TEST, fase 2:** een nieuwe engine begint weer bij het hoogste profielblok; eerder geaccepteerde blokken worden opnieuw aangeboden en alle bytes gecontroleerd. Dit slaagt in twee flashmodellen, met en zonder gesimuleerde koude start. Geen resume vanaf een lokale teller. Of een echte bootloader dezelfde herselectie en herprogrammering accepteert blijft OPEN. |
| Eerst resetten? | Niet als een bootloader al aantoonbaar actief, identificeerbaar en gereed is: `ms2x_start_bootloader` zendt geen normale ping/reset. Na een vastgelopen gedeeltelijke overdracht kan een koude knopstart nodig zijn volgens de gedocumenteerde herstelroute. Of dezelfde sessie zonder reset kan worden herbewapend is OPEN. Geen automatische reset proberen. |
| Geen antwoord op normaal identificatiecommando? | Normale CLI-update weigert na discovery selectie en begint geen flash. Een directe engine blijft in `WAIT_PING` tot timeout. Geen automatische bootfallback. Alleen expliciete bootmodus met volledige bootidentiteit kan verder. |
| Hoe een ander CAN-apparaat uitsluiten? | Volledige ondersteunde hardware-ID, runtime-UID, consistente identiteiten, hashconflicten/overflow afwijzen, exacte imagevalidatie en heridentificatie. Dit beschermt tegen veel vergissingen, niet tegen spoofing of een tweede programmerende peer. Strengere recoveryselectie en bus-exclusiviteit ontbreken nog. |
| Reguliere versus herstelupdate? | Regulier gebruikt normale identiteit en reset, met timerstart en optionele bootidentiteit. Herstel slaat normale identiteit/reset over en eist een volledige bootidentiteit; daarna dezelfde blokken/CRC/start. Bediening, doelkeuze, voorcontrole en foutmeldingen moeten recovery expliciet onderscheiden. |
| Wanneer niet via CAN herstelbaar? | Een niet meer uitvoerbare/bereikbare bootloader, beschadigde noodzakelijke boot/resetconfiguratie, defecte flash/voeding/MCU/CAN-transceiver of onbruikbare bus kan dit protocol niet repareren. Ook ontbrekende identiteit blokkeert ons veilige pad. Afwezig antwoord bewijst niet welke oorzaak speelt. Service of een apparaatspecifieke externe herstelroute kan nodig zijn; die valt buiten deze analyse. |

### Zonder hardware-ID uit de normale applicatie

Dat is op zichzelf **geen blokkade** wanneer de bootloader het volledige
ondersteunde type én een eenduidige UID levert. De bootidentiteit mag dan
de primaire profielbron zijn. Er is geen gezonde applicatie nodig om haar
type nogmaals te melden. Met dezelfde validatie mag de engine worden hergebruikt.

Zonder hardware-ID **ook in de bootidentiteit** is veilig herstel momenteel
**niet ondersteund**. Een korte ACK, sticker/modelnaam, bekende UID, UID-prefix,
passende bestandsheader of `--hardware` bewijst het apparaattype niet.
`--hardware` is nu een te controleren verwachting, geen bypass; `WAIT_BOOT`
vereist alsnog DLC8 en passende typebytes. Het type uit uitsluitend een oud
logbestand is evenmin een verse, gecontroleerde identiteit. Een toekomstige
alternatieve bewijsroute vereist zelfstandig protocol-/hardwarebewijs en een
afzonderlijk ontwerp; geen verborgen override in recovery.

## 9. Veiligheidsverschillen die nog moeten worden opgelost

| Huidige situatie | Gevolg voor recovery | Benodigde aanvulling |
|---|---|---|
| Eén ondersteund apparaat mag automatisch worden geselecteerd | Te weinig deliberate targetkeuze voor herstel | Verplicht expliciet doel en review van de concrete identiteit/profielkeuze |
| Boot-updatediscovery is passief; UID/type-fallback mogelijk | Niet iedereen wordt gevonden; handmatige waarden kunnen onterecht als bewijs worden opgevat | Afzonderlijk geautoriseerde bootquery, volledig identiteitsbewijs vóór flashautorisatie |
| `ms2x_session_run` wist zijn resultaat tussen discovery en update | Discoveryconflicten zijn niet automatisch een blijvende runtimeguard | Behoud identiteitssnapshot en sticky conflicten; hercontrole vóór volgende TX |
| Firmwarefase filtert vreemde UID's weg en gebruikt `remember` niet | Later verschenen conflict/tweede bootloader wordt niet volledig beoordeeld | Controleer alle relevante identiteiten ook tijdens overdracht |
| Firmwarefilters zien hoofdzakelijk `0031`, `0037`, `0040` | Een tweede zender op `0036` of ander updaterverkeer wordt niet bewaakt | Observatie van updaterverkeer en conservatieve conflictstop; bus-exclusiviteit apart aantonen |
| Datadelen hebben geen UID, ACK88 heeft geen blok/nonce | Gedeelde bus kan data/voortgang vermengen | Alleen één updatecontext; geen claim dat UID-filtering dit oplost |
| Enginehash `4711` is een constante en wordt niet als busidentiteit gereserveerd | Lokale bridge/andere zender kan dezelfde hash gebruiken | Botsingsbeleid onderzoeken; 5A22 geldt alleen voor diagnostische probe |
| Alleen grove exitcodes, console toont volledige UID/hash | Geen voldoende foutclassificatie of privacyveilig protocoljournaal | Expliciete recovery-status/foutcodes en geminimaliseerde logging |
| Resourcefase start automatisch na appverificatie | Nieuwe, afzonderlijke schrijfacties zonder aparte recoverykeuze | Firmwareherstel afronden; resources pas afzonderlijk bevestigen/valideren |
| Geen vaste per-actie bevestigingslaag | Bestaand `update --bootloader` voldoet niet aan alle gevraagde veiligheidseisen | Default weigeren zonder concrete, begrensde autorisatie |

De bestaande target-/image-/ACK-controles zijn zinvol en blijven verplicht.
Alleen een nieuw CLI-label rond de huidige bootflag is onvoldoende om deze
punten veilig te noemen. Ook een geïsoleerde bus biedt geen cryptografisch
identiteitsbewijs; CAN-identiteiten zijn in dit ontwerp niet geauthenticeerd.

## 10. Voorstel voor een expliciet recovery-pad

Dit is een ontwerp, geen geïmplementeerde bediening of hardware-instructie.
Voorgestelde scheiding: `recover inspect` en `recover program`. Normale
`update` behoudt zijn eigen pad en krijgt nooit automatische recoveryfallback.
Geen autostart op apparaatherkenning, timer, reconnect of ontbrekende app-ping.

1. **Inspectie:** standaard uitsluitend passieve ontvangst; toon gewone en
   bootidentiteiten, conflictsituaties en het voorgestelde profiel. Een actieve
   bootquery is een eigen expliciet te bevestigen CAN-schrijfhandeling. Geen
   reset, selector, data, CRC, start-11 of resources in inspectiemodus.
2. **Voorcontrole:** verplicht runtime-doel-UID, volledige bootidentiteit met
   ondersteund type, geen conflicten/overflow/onvolledige bewijsroute en geen
   tweede actieve updatecontext. Houd identiteit en conflictsnapshot vast.
   Gebruikersinvoer moet met het ontvangen type overeenkomen. Controleer het
   gehele image met de bestaande loader vóór een programmeeractie.
3. **Autorisatie:** maak de concrete actie reviewbaar: modus, huidig doel,
   ontvangen type, profiel, versie, hash, grootte, blokbereik en mogelijke
   gevolgen bij onderbreking. Iedere schrijfactie krijgt expliciete bevestiging;
   afwijzen/EOF/niet-interactief zonder afzonderlijke autorisatie betekent nul
   zendingen. Geen algemene permanente `--yes`, automatische herhaling of
   firmwaredata tijdens het wachten op bevestiging.
4. **Timing en toestemmingen:** plan vooraf welke acties worden toegestaan.
   Onder 'iedere schrijfactie' vallen ook query, eventuele reset, programmeren,
   start-11 en resourcelevering. Als toestemming per afzonderlijk CAN-frame
   nodig is, moet ieder exact frame vooraf afzonderlijk geautoriseerd zijn;
   geen interactieve prompts midden in de 5-s-protocolfasen. Een bevestiging
   voor een hele image-overdracht als één samengestelde actie is alleen een
   mogelijke latere bedieningskeuze, geen stilzwijgende versoepeling van de
   eis. Het huidige API heeft deze autorisatielaag niet.
5. **Heridentificatie en overdracht:** nadat alle benodigde autorisaties zijn
   vastgelegd, een verse bootquery en volledige identiteitscontrole vóór de
   eerste selector. Alleen bij exact hetzelfde doel/type en consistente bus
   de bestaande `ms2x_start_bootloader` → select/data/CRC-engine gebruiken.
   De vooraf geautoriseerde immutable TX-inhoud mag niet worden vervangen;
   geen nieuwe UID raden als identiteit tijdens reset verandert.
6. **Fout of disconnect:** eerste fout is terminaal; stop nieuwe TX, sluit
   transport en trek resterende autorisaties in. Meld dat reeds gebufferde
   frames of uitgevoerde writes niet kunnen worden teruggedraaid. Geen
   automatische reset, start-11, retries, reconnect of resume. Een nieuwe
   poging vereist opnieuw inspectie, validatie en bevestigingen.
7. **Afronding:** start-11 alleen na alle correcte blok-ACK's en eigen
   autorisatie. Appidentiteit/versie controleren. Meld onderscheid tussen
   lokaal aangeboden bytes, protocol-ACK's, applicatie teruggezien en
   functioneel herstel; voor die laatste claim is meer bewijs nodig. Geen
   automatische resourcelevering; afzonderlijk geautoriseerde en gevalideerde
   resourceactie kan later volgen.

Voorstel voor fouten op de bedieningslaag: `RECOVERY_NO_BOOT_IDENTITY`,
`RECOVERY_UNKNOWN_HARDWARE`, `RECOVERY_IDENTITY_CONFLICT`,
`RECOVERY_MULTIPLE_BOOTLOADERS`, `RECOVERY_PROFILE_MISMATCH`,
`RECOVERY_IMAGE_INVALID`, `RECOVERY_CONFIRMATION_REQUIRED`,
`RECOVERY_PROTOCOL_NACK`, `RECOVERY_PHASE_TIMEOUT`, `RECOVERY_TRANSPORT_LOST`
en `RECOVERY_APPLICATION_UNVERIFIED`. Dit zijn nieuwe ontwerpbenamingen,
geen bestaande API-symbolen. Behoud kernfout plus fase/bloknummer als context;
geen onbekende status als succes interpreteren.

Log relatieve monotone tijd, richting, toestand, commandosoort, DLC,
blok/partnummer, ACK/status, deadline en fout. Gebruik willekeurige
sessie-aliassen voor apparaten; geen ruwe UID, CAN-senderhash, serienummer,
IP/MAC, hostname, volledige bestandslocaties of payloaddumps. Identiteitsvelden
uit CAN-ID/payload moeten vóór logging worden geminimaliseerd. Een gewone
ongezouten UID-hash is geen betrouwbare anonimisering. Bewaar geen
firmwarebytes in logs. De concrete doelidentiteit mag lokaal voor de
gebruikersbevestiging zichtbaar zijn; dat is geen automatisch exporteerbaar log.

### Afbakening van eventuele codewijzigingen

| Onderdeel | Voorgestelde wijziging in een latere fase |
|---|---|
| `linux_main.c` | Afzonderlijke recoverybediening, expliciete target-/typekeuze, inspect/program-scheiding en bevestigingslaag; geen normale fallback |
| `session.c` / `ms2x_session.h` | Identiteitssnapshot behouden, runtimeconflicten en autorisatiestatus bewaken, resources loskoppelen, gestructureerde uitslagen |
| `ms2x.c` | Bestaande profiel-, validatie- en blokengine behouden; eventuele minimale scheiding van start-11 voor autorisatie zonder deadlineproblemen |
| `socketcan.c` / filters | Recovery-specifieke TX-toelating en observeren van concurrent updaterverkeer; deadlines en fatale I/O-grenzen behouden |
| Tests/build | Firmwarevrije tests zelfstandig bereikbaar maken; synthetische testcatalogus uitsluitend in testbinary, dezelfde validator; foutieve fixtures/ontbrekende afhankelijkheden herstellen |

Er is geen aanwijzing dat een tweede programmeerengine, eigen bootloader,
CS3-emulator of nieuwe PIC/hardwarefirmware nodig is. Dat is een afbakening
op basis van de onderzochte bronnen, geen bewijs dat de bestaande transportketen
alle recoverytiming al haalt.

## 11. Fase 2 — uitgevoerde offline recovery-tests

Uitgangspunt: commit `f0c0551`. Alle tests gebruiken synthetische bytes en
RAM-frames. Geen private checkout, firmwarebestand, echte CAN-interface of
hardware is nodig. De productie-CLI wordt alleen gebouwd en als bestand
gelezen voor cataloguscontrole, nooit uitgevoerd.

### Fixturecorrectie en minimale implementatiewijziging

Scenario16 in `test_can_sim_loop.c::discovery` stelde een MS2 en een ander
CAN-apparaat (`0011`) voor met dezelfde UID. De verwachte selectie klopte dus
niet met de fixture. Alleen het tweede apparaat kreeg een eigen synthetische
UID. Extra assertions controleren dat beide apparaten zijn onthouden en dat
het niet-ondersteunde type expliciet wordt geweigerd. De bestaande scenario's
voor dezelfde UID met verschillende typen, hashconflicten en ambiguïteit
blijven ongewijzigd slagen. `--discovery-only` en `make check-discovery` maken
alle 19 bestaande scenario's bereikbaar zonder ontbrekende private fixtures.

Voor positieve overdrachten is alleen de vaste catalogus plus selector
verbatim naar [profiles.c](../src/ms2x-c/profiles.c) verplaatst. Productie
linkt dit bestand; uitsluitend `test-recovery` linkt
[tests/recovery_profiles.c](../src/ms2x-c/tests/recovery_profiles.c) met
synthetische hashes en dezelfde blokgeometrie. De daadwerkelijke validator,
SHA256, engine, sessie en CAN-loop zijn dezelfde objecten in beide builds.
Geen runtime-cataloguskeuze, force-optie, SHA256-mock of veiligheidsbypass.

[synthetic_image.h](../src/ms2x-c/tests/synthetic_image.h) genereert
niet-uitvoerbare patronen in geheugen. De C-validator moet lengte, versie,
type en de volledige testhash blijven controleren. De productievalidator
weigert diezelfde drie patronen. De onafhankelijke Python/hashlib-controle
[check_recovery_catalog.py](../src/ms2x-c/tests/check_recovery_catalog.py)
bevestigt de testhashes en dat de gebouwde productie-CLI alle oorspronkelijke
hashes en geen synthetische hashes/profielnamen bevat.

### Wat de recoverytest uitvoert

[tests/test_recovery.c](../src/ms2x-c/tests/test_recovery.c) implementeert
alleen een reagerende RAM-peer aan de bestaande vijf `ms2x_sim_*`-functies.
De host gebruikt `ms2x_session_run`, runtime-discovery/selectie,
`ms2x_engine_new_target` en de bestaande normale/directe bootstart. De peer
controleert gerichte controlframes, aflopende blokken, 128 datadelen per blok
en CRC met een afzonderlijke bitgewijze berekening. Aan het eind worden alle
applicatiebytes inclusief FF-padding vergeleken en wordt appversie 5.6 via
een synthetisch normaal identiteitsantwoord gecontroleerd.

Voor ieder profiel vindt de onderbreking plaats na de helft van de
CRC-geaccepteerde blokken, plus 64 delen van het volgende blok. De RAM-link
retourneert EIO. De host moet terminaal afbreken zonder start-11; dezelfde
engine kan niet opnieuw starten of hervatten. Daarna blijven ontvangen
applicatiebytes en bloktellers behouden. De test ontdekt alleen een
bootloaderidentiteit en maakt expliciet een nieuwe hostengine. Die begint
bij het hoogste blok, verstuurt de hele applicatie opnieuw en overschrijft
ook eerder geaccepteerde blokken. Er vindt geen stilzwijgende fallback plaats.

Dit wordt gekruist uitgevoerd met twee **modelaannames**:
commit pas na correcte blok-CRC versus wissen bij selector en direct delen
schrijven; en zonder peerreset versus een gesimuleerde koude boot die de
vluchtige blokbuffer wist. Beide modellen laten herselectie van een blok toe.
De tests bewijzen niet dat een echte MS2 die aannames deelt. Bij de koude
boot blijven applicatiebytes behouden; geen complete geheugenwisactie maakt
de hertest kunstmatig schoon.

### Resultaten

Uitgevoerd met GCC en opnieuw met Clang ASan/UBSan/LeakSanitizer:

| Controle | Resultaat |
|---|---|
| Bestaande discovery-suite | 19/19 scenario's, 278 controles geslaagd, inclusief 16–18 |
| Productieprofielen | 0032/0033/0034 correct, onbekende typen afgewezen |
| Productie-firmwareafwijzing | 28 controles geslaagd: oorspronkelijke 25 plus de drie exacte synthetische recovery-images |
| Dynamische RAM-recovery-suite | 49 testgevallen, 1.755.540 controles geslaagd |
| Normale complete overdracht | Alle drie profielen; normale discovery, gerichte reset, alle blokken/CRC en appverificatie geslaagd |
| Herstart na halve overdracht | 12 combinaties geslaagd: drie profielen × twee flashmodellen × met/zonder koude boot; herstart op blok 95, 159 respectievelijk 127 |
| Onjuiste firmware/profiel | Verkeerde lengte, inhoud/hash, versie, type, kruisprofiel en onbekend type geweigerd; geen TX bij imageafwijzing |
| Ontbrekende/afwijkende bootidentiteit | Geen antwoord, andere UID, verkeerd type en kort antwoord stoppen vóór firmwaredata |
| Fouten tijdens overdracht | Geen selector-ACK, geen CRC-ACK, timeout halverwege databurst en F2-NACK stoppen zonder automatische retry/start |
| Geen applicatie na laatste ACK/start | Firmware-ACK's worden niet als volledig succes gemeld; appverificatie verloopt met fout |
| Meerdere/onbekende apparaten | Automatische bootselectie bij twee doelen ambigu, expliciete UID selecteert passend profiel; nul/onbekend type of ontbrekende identiteit geweigerd |
| Catalogusscheiding | Productiehashes behouden, synthetische hashes alleen in testbinary; hashlib-controle geslaagd |
| Native productiebuild | CLI, offline tool en shared library linken met de verplaatste catalogus; CLI niet uitgevoerd |

De eerste Clang-build stopte op bestaande bestanden zonder afsluitende newline.
Voor de sanitizeropdracht is alleen de compilerdiagnostiek `-Wnewline-eof`
uitgezet; de andere strenge waarschuwingen blijven fouten. Daarna blokkeerde
LeakSanitizer onder ptrace in de sandbox. Dezelfde offline suite slaagde na
expliciete toestemming buiten die sandbox, met leakdetectie ingeschakeld.
Geen sanitizerfouten gevonden. Reproduceerbare commando's staan in
[het bijgewerkte testontwerp](BOOTLOADER_RECOVERY_TEST_DESIGN.md).

Er zijn geen falende scenario's in `make check-recovery`. De volledige
historische `make check` is niet uitgevoerd: die vereist nog niet meegeleverde
firmware, Python-module `ms2updater`, referentie-events/captures en
`tests/linux_test_entry.h`. Ook de oude SocketCAN-test verwacht nog een
uitgeschakelde live-gate. Deze opdracht repareert die bredere harness niet.

### Wat vóór een verantwoorde hardwareproef ontbreekt

1. Onafhankelijk protocolbewijs van bootidentiteit, ACK-vormen, herselectie en
   opnieuw programmeren na een onderbreking op een echte MS2. Vooral de
   gekozen erase-/commit-/resetsemantiek mag niet uitsluitend uit ons model komen.
2. Onderbouwde SRSEII SLCAN/PIC-timing en buffering: kernelacceptatie is nog
   geen fysieke CAN-verzending. Oude ACK's en UID-loze datadelen blijven risico's.
3. Een reviewed, expliciet geautoriseerd proefplan met één geïsoleerd doel,
   vastgestelde hardware-identiteit, onafhankelijk gecontroleerde bevoegde
   imagebron, stopcriteria en beschikbare externe herstel-/serviceroute.
4. De eerder beschreven autorisatie- en runtime-identiteits-/busbewaking vóór
   een schrijvende recoverybediening als veilig wordt aangeboden. Deze fase
   voegt die productiefunctionaliteit bewust niet toe. Resources en functioneel
   herstel vragen afzonderlijke validatie en bevestiging.

**Bijgesteld bewijs:** de host kan in deze simulatie opnieuw beginnen en het
hele image correct aanbieden. Daadwerkelijke CAN-recovery op hardware blijft
nog niet verantwoord als gegarandeerd of betrouwbaar ondersteund te claimen.

## 12. Risicoanalyse en ontbrekende bewijzen

| Risico | Ernst / beperking | Benodigde onderbouwing vóór betrouwbare recoveryclaim |
|---|---|---|
| Verkeerd doel of type | Andere MS2/CAN-peer kan worden geraakt | Strikte actuele identiteit, behoud conflicten, expliciete selectie en negatieve tests |
| Beschadigde of niet geactiveerde bootloader | Geen bruikbare CAN-ingang | Documentatie/trace van daadwerkelijke koude boot en identiteit; geen afwezige reply als definitieve defectdiagnose |
| Herprogrammeren na deelwrite | Verdere beschadiging of permanent niet startende app | Werkelijke erase/program/commit-grenzen, herselectie en herstart na verschillende onderbrekingen |
| Gedeelde UID-loze datastroom | Andere bootloader kan data ontvangen | Exclusieve updatecontext en observeerbaar conflictbeleid; alleen UID-selectie is onvoldoende |
| Oude CRC-ACK | Verkeerd voortgangs- of eindcriterium | Onafhankelijk protocolbewijs of expliciet geaccepteerde, onderbouwde restbeperking; geen nonce verzinnen |
| Kernelreceipt vóór fysieke overdracht | Buffers/jitter veroorzaken premature CRC of timeout | SRSEII SLCAN/PIC-volgorde, buffering, verlies en timing onderbouwd voor dit pad |
| Resources/appfunctioneren na flash | Versie zichtbaar maar herstel onvolledig | Apart bewijs voor resources en functioneel eindcriterium; geen automatische resourcewrites |
| Testmodel bevestigt eigen aannames | Groene tests overschatten herstelzekerheid | Onafhankelijke peerreacties naast synthetische tests; modelleer ongunstige flashvarianten |
| Logs lekken apparaat-/firmwaredata | Onbedoelde publicatie | Geminimaliseerde logger met tests op identificerende velden en payloads |

De nog ontbrekende onafhankelijke feiten zijn concreet: volledige
bootidentiteit en ACK-vormen per relevante bootversie; stand-alone bereikbaarheid
via SRSEII bij kapotte applicatie; opnieuw ontvangen van alle applicatieblokken
na verschillende uitvalmomenten; noodzaak en effect van reset; bootgebiedgrenzen
en bescherming; exacte afronding en app-/resourcegedrag; transportgedrag en
uitsluiting van gelijktijdige updaterzenders. De officiële herstelbeschrijving
ondersteunt het concept; zij vult deze SRSEII-specifieke bewijsgaten niet in.
Verzamelen van hardwarebewijs valt buiten deze offline opdracht.

**Besluit:** hergebruik de bestaande gevalideerde blokengine als basis voor
vervolgonderzoek, met een aparte expliciete recoverybediening en strengere
identiteits-, autorisatie- en busbewaking. Geef recovery nog niet vrij als
betrouwbaar ondersteunde functie. Zonder vastgestelde hardware-identiteit of
bereikbare intacte bootloader bestaat binnen dit ontwerp geen veilig CAN-pad.
Herstel wordt niet gegarandeerd.


## 13. Fase 3 — CAN-protocolaudit vanaf ff5a1c9

### Bereik en bronnen

De audit gebruikt de reeds lokaal aanwezige bron- en documentkopieën, zonder
netwerk, hardware of firmwaredownloads. Er is geen officiële volledige
CAN-bootloader-specificatie lokaal beschikbaar. Dit is daarom een toets aan
de gedocumenteerde herstelroute en concrete referentiecode, geen certificering
van alle mogelijke Märklin CAN-varianten of de actuele upstreamversie.

| Bron | Concrete herkomst en gecontroleerde onderdelen | Bewijswaarde |
|---|---|---|
| **D**: oorspronkelijke CS3-help | `hilfe/en/29-rescuedevice/bootloaderupdate.html`, regels24–45; hash opnieuw gecontroleerd, zie §2 | Primaire vendorinstructie voor herstel na onderbreking en MS2-knopstart; geen wire-specificatie |
| **G**: Gerd `gb2-update.c` | [Gepinde bron](https://github.com/GBert/railroad/blob/a427e6528922e7f17e4eebfb9c9af68599bb2268/can2udp/src/gb2-update.c); regels120–129,235–254,382–431,435–527,711–718 | Concrete open-sourcezender; geen geslaagde boot-only-0033/0034-proef |
| **M**: mmame GB2MS2Updater | [Gepinde updatercode](https://github.com/mmame/GB2MS2Updater/blob/5904006a244c3a36e3c52fe58ebed8cb323c621a/GB2MS2Updater/GB2MS2Updater.cs), plus [SerialCAN.cs](https://github.com/mmame/GB2MS2Updater/blob/5904006a244c3a36e3c52fe58ebed8cb323c621a/GB2MS2Updater/SerialCAN.cs) en [Crc16Ccitt.cs](https://github.com/mmame/GB2MS2Updater/blob/5904006a244c3a36e3c52fe58ebed8cb323c621a/GB2MS2Updater/Crc16Ccitt.cs) | Tweede implementatie, maar expliciet afgeleid van G; dus geen onafhankelijke tweede protocolontdekking |
| **C**: statische CS3-uitsneden | Bestaande `phase09`-uitsneden bij het in §2 gehashte `event`: `updater-construction`, `block-next-file`, `block-status-timers`, `updater-error-f2-gnu-bound`, `CRC-dispatch-completion`, `identity-boot`, `finalization`; serializer in `audit07/service-serializer` | Onafhankelijke vendorzendercode naast G/M; geen meting van een ontvanger of volledig opnieuw uitgevoerde binary-audit |

SHA256 van de gelezen M-bestanden, in bovenstaande volgorde:

- Updater: `1ef855ab830d9da3b0873dac87c313f23090ae19f79e11dcde1e87fa676a0fbd`.
- SerialCAN: `561ac829b34add385897386186a635ceea21991c1197145af7fdf356e76af3a5`.
- CRC: `c2e376963ed7691e76343657c61866f4542cbfada28f0d80dd7cd86683ae2222`.

G heeft nog de hash uit §2. De publieke links identificeren de vastgelegde
herkomst; zij zijn in deze fase niet online bezocht. De lokale kopieën en
CS3-uitsneden zijn niet in deze publieke repository opgenomen. Een onafhankelijke
lezer moet dus de genoemde bronrevision of bevoegde documentkopie verkrijgen.
Er is geen upstreamcode of propriëtaire inhoud aan het project toegevoegd.

### Vergelijking van de concrete protocolstappen

Alle ID's hieronder zijn wire-ID's zonder Linux EFF-vlag. `U` is een runtime
UID van vier bytes in big endian. Hash en UID zijn verschillende velden.

| Onderdeel | Bevestiging in bronnen | Onze implementatie en resterende grens |
|---|---|---|
| Klassieke extended CAN, DLC0–8 | G235–244; M `CANMessage` en `SerialCAN.SendCAN`131–164 | Codec/adapter passen bij dit frameformaat; de 13-byte replay-envelope is geen CAN-payload of Linux-record |
| Normale identiteit | G442–459; M431–449 lezen U en twee versiebytes uit DLC8 | `0031hhhh`, U/versie/type; onze volledige 16-bit typecontrole is sterker dan G/M's UID-prefixselectie |
| Bootidentiteit en hardwaretype | G472–485, M464–471; C0x51d22–0x51d28 en0x526a2–0x526ac lezen DLC8 en beide typebytes | `0037hhhh`, U, versie, type. 0032/0033/0034-profielmapping is niet door alle drie de open-sourcevarianten gevalideerd; G/M ondersteunen niet alle typen |
| UID en nulhash | G503/511 en M495/503 benoemen hash0000; zij vergelijken payload in plaats van hash | Onze doelcontrole gebruikt U; hash0000 wordt aanvaard. Bootversie/appversie en hashes hoeven niet gelijk te zijn. Geen vaste apparaat-UID of prefix nodig |
| Reset en bootquery | G122–124,456–466; M394–399; C0x5064a–0x50660 en0x411e8–0x41290 | Gericht `00004711`, DLC6, U/80/FF; daarna lege `00364711`. Direct bootpad slaat reset/ping over. Diagnosehash5A22 is onze lokale keuze, geen vendorvoorschrift |
| Blokselectie en ACK | G382–390,504–514; M325–336,495–508; C0x6ef4a–0x6ef50 en0x51d42–0x51d56 | `00364711`, DLC6, U/44/N; ACK `0037hhhh`, DLC6, U/44/N. Verkeerd blok faalt. C kent ook een DLC7/tweebyteblokvariant; die is niet aangetoond als nodig voor onze bloknummers≤159 |
| Data en volgorde | G416–425,507–509; M356–370,498–500; C0x6eed2–0x6eee8 en serializer | `00360300+k`, DLC8, k0–127, acht bytes zonder UID; aflopende blokken en juiste bestandsoffset. Gelijktijdige updatecontexten blijven onveilig |
| Geometrie en padding | G53–56; M71,259–281; C0x6f0da–0x6f0e2 en0x6eefe–0x6ef0a | 1024-byteblokken, logische bootgrens4, FF-padding; geen fysieke bootprotectie bewezen. Exacte 0033/0034-imagegrootten komen uit onze vaste catalogus, niet uit M |
| CRC en CRC-ACK | G427–431,479–480; M373–380,503–504,663–665 plus CRC-class | CRC16 initFFFF/poly1021, big endian; `U,88,CRC_hi,CRC_lo`, DLC7; antwoord `U,88`, DLC5. Geen echo van blok/CRC/nonce. C dispatcht88 naar volgende blok; dat bewijst niet alle toegestane ACK-lengten op echte MS2's |
| Negatieve status | G498–501 en M484–492 aborteren F1/F2; C0x5280a–0x5281c en0x6ebd8–0x6ec80 | Onze F1–F6-abort is conservatief beleid. C heeft F2-retry tot drie, F3/F4-return; F5/F6 zijn niet door die Event-dispatch doorgelaten. Geen retry overnemen of precieze statusbetekenissen verzinnen |
| Afronding | G517–521: F5+nul-UID11; M516–524: gericht F5,1s,gericht11,13s; C0x52a0c–0x52a1a: gericht11 | Onze gerichte start11 sluit aan bij dit C-pad. F5 is daardoor geen bewezen ontbrekend verplicht commando; verschil blijft te toetsen aan de echte bootversie |

De laagste 16 bits van data-ID's zijn partcodering, geen normale senderhash.
De CS3-serializer bevat bovendien een alternatieve codering met andere hoge
ID-bits; wij modelleren uitsluitend de hier onderzochte `0036/0037`-route.
Geen bewijs dat die alternatieve route voor MS2-recovery via SRSEII nodig is.
Ook de ontvangstmaskers zijn daarmee geen claim op alle prioriteits-/routingmodi.

### Timing en toegang zonder applicatie

G wacht500 ms na reset en daarna op bootidentificatie; G's CAN-backend wacht
20 ms na ieder frame, met een Gleisbox-gerelateerd TODO-commentaar. De UDP-route
heeft die vertraging niet. M wacht500 ms na reset, dan op de bootreply en100 ms
vóór de eerste selector (`SendInitialBlockId`,659–669). M's SerialCAN wacht
standaard5 ms na elke zending, stelt250 kbit/s CAN in en gebruikt230400 baud
voor zijn eigen dongle. Dat zijn implementatiekeuzes, geen opgegeven MS2-minima.

C gebruikt tweemaal500 ms in het normale startpad. `DeviceUpdater`
0x6f318–0x6f358 verstuurt eerst een part direct, start een10-ms-timer en doet
maximaal vijf volgende part/CRC-aanroepen per tick. Onze logische datapacing
komt daarmee overeen. De directe bootroute gebruikt500 ms **na de vereiste
bootreply**, een lokale conservatieve keuze; geen aangetoond minimum of maximum
van de MS2. Hostplanning bewijst geen fysieke UART/PIC/CAN-timing op SRSEII.

G's5-s-timeout is een opnieuw gestarte `select`-inactiviteitstimer; M heeft
expliciet een TODO en een1-uurs hoofdwacht. C configureert5000 ms bij
0x6f13a, maar de precieze hardwaregrens volgt niet uit die timerwaarde. Onze
absolute5-s-fasedeadlines, inclusief afwijzing precies op de deadline, zijn
veilig hostbeleid en niet een letterlijk gedocumenteerde MS2-eis.

D beschrijft herstel na een onderbroken update en MS2-knopstart die de
bootloader actief houdt. C leest bootidentiteit en koppelt type aan catalogus;
de bootloaderoptie onderdrukt vroeg automatisch start11. Dat onderbouwt een
bedoeld pad buiten een werkende applicatie. Het interne startmechanisme en
de bootdialoog via SRSEII na daadwerkelijk beschadigde applicatie zijn nog
niet gemeten. **Een ontbrekende normale ping sluit recovery niet uit; een
ontbrekende volledige bootidentiteit sluit ons veilige programmeerpad wel uit.**

M bewijst dit niet zelfstandig: `StartUpdate` kiest eerst050, verstuurt een
normale ping, en verwerkt bootantwoorden alleen na `PingReceived`. De
bootidentificatie accepteert alleen laatste typebyte10 of32; G daarnaast11.
Geen van beide is daarmee bewijs van boot-only0033/0034-recovery. M's README
verwijst expliciet naar G; tweemaal dezelfde logica is geen onafhankelijk
bewijs dat een fysieke ontvanger na deelprogrammering weer gereed is.

### Wat de49 tests wel en niet bewezen, en gerichte aanvullingen

De oorspronkelijke49 tests blijven slagen. Hun positieve peer geeft altijd
passende identiteiten, snelle ACK's en toestemming voor herselectie na een
onderbreking. De uitkomsten bewijzen de echte hostengine onder die aannames;
zij toetsen niet onafhankelijk of die aannames waar zijn. Met name:

- De initiële normale/directe bootroute en blok/CRC/UID-layout sluiten aan bij
  G/M/C, met de hierboven genoemde verschillen. Er was geen test van nulhash,
  alle negatieve statussen of een vertraagd echt lijkende ACK.
- Commit/wissen en persistentie zijn testmodellen. Er is geen uitgevoerde
  bootloader, MCU, flashcontroller of watchdog. Een modelherstart is geen
  bewijs dat een werkelijke MS2 na iedere uitval opnieuw CAN aanbiedt.
- De nieuwe sessie wist de RAM-transportqueue. Dat laat oude bus-/bridgeframes
  weg. De49 tests bewezen daarom geen versheid van ACK's na een herstart.
- De synthetische5.6-identiteit bevestigt alleen het gekozen eindcriterium;
  geen readback van echte flash, functionele UI of resourceherstel. Meerdere
  apparaten worden vóór programmeren getest, geen twee actieve programmeerders.

Alleen `tests/test_recovery.c` is uitgebreid, zonder nieuw framework of
productieaanpassingen. Twintig extra gevallen:

| Aanvulling | Uitkomst |
|---|---|
| Nulhash bij bootidentiteit en volledige overdracht | Geslaagd; UID/type blijven leidend |
| F1–F6 als DLC5 én DLC8 met plausibele typebytes | 12 gevallen: abort vóór volgende selector/start; geen retry |
| CRC-ACK op4999/5000/5001 ms | Eén complete overdracht vóór deadline; twee time-outs op/na deadline |
| Verkeerd selectorblok, premature88, verkeerde hoge hardwaretypebyte | Drie gevallen: weigering vóór data |
| Oude identieke88 in latere `WAIT_CRC` | Beperking gereproduceerd: host telt twee blokken, peer heeft slechts één gecommit |

Ook de bestaande normale tests controleren nu exact500 ms reset→query en
500 ms query→selector. De directe recoverytests controleren500 ms na de
bootreply. Alle overdrachten controleren logische partpacing: één deel direct,
daarna groepen van maximaal vijf per10 ms. Deze timingchecks bewijzen het
hostschema, niet de busprestatie of een voorgeschreven ontvangstinterval.

De stale-ACK-test bewaart de echte ACK van het eerste blok. Voor het tweede
blok wordt niet gecommit en geen echte ACK gegeven; alleen die oude ACK
wordt in de geldige wachtfase teruggestuurd. De host accepteert hem en
verstuurt de derde selector. De test stopt daarna bewust met EIO, vóór start11.
Dit is een **geslaagde reproductie van een veiligheidsbeperking**, geen bewijs
van veilige voortgang. Dezelfde vijfbytesvorm biedt geen manier om ouderdom
of bloknummer te reconstrueren. Geen verzonnen correlatieveld of niet bewezen
productiewijziging is toegevoegd; de beperking was al in de engine benoemd.

Uitgevoerd: `make BUILD=/tmp/ms2-recovery-phase3 check-recovery` vanuit
`src/ms2x-c`. Resultaat:69 gevallen /2.372.864 controles, plus19 discoverygevallen
/278 controles, productieprofielen,28 firmwareafwijzingen en catalogusscheiding.
Alle assertions slagen, inclusief de expliciet gelabelde beperking. Dezelfde
suite slaagt met ASan/UBSan/LeakSanitizer volgens de opdracht uit het testontwerp,
met buildpad `/tmp/ms2-recovery-phase3-sanitize`. Geen echte firmware gebruikt;
de gebouwde productie-CLI is niet uitgevoerd. De volledige historische
`make check` blijft buiten bereik om de eerder beschreven ontbrekende inputs.

### Advies voor een hardwaretest

**Nog niet klaar voor een schrijvende bootloader-recoverytest met de huidige
CLI.** De basisframes en het gedeelde overdrachtspad zijn onderbouwd; er is
geen aangetoonde fout die nu een ander frame, F5, retry of gewijzigde timing
rechtvaardigt. Volledige overeenstemming met een echte0033/0034-bootloader
na onderbreking is echter niet bewezen. De resterende voorwaarden zijn:

1. Eerst onafhankelijk bewijs van koude bootidentiteit/ACK-vormen en behoud
   van de bootmodus op de bedoelde SRSEII-route; alleen volledige actuele
   UID/type-identiteit gebruiken. Geen handmatige typeaanname bij ontbrekend ID.
2. Timing en buffering van de concrete SLCAN/PIC-keten onderbouwen en één
   exclusieve updatecontext aantonen. Een rustige bus of UID-filter alleen
   maakt een oude88 of UID-loze data niet aantoonbaar veilig.
3. Een bewust geaccepteerd en onderbouwd beleid voor ACK-versheid, onderbreking
   en onbekende flashsemantiek, met stopcriteria en beschikbare externe
   herstel-/serviceroute. Geen opzettelijke onderbreking op een enig apparaat
   zonder die route als eerste proef.
4. De eerder beschreven concrete gebruikersautorisatie en identiteit-/busbewaking
   vóór writes; resources niet automatisch meeschrijven als onderdeel van een
   recoveryproef. De huidige CLI levert die scheiding niet volledig.

Een later afzonderlijk geautoriseerde, begrensde identificatieproef kan het
eerste ontbrekende bewijs verzamelen zonder programmeerblokken te sturen;
passieve ontvangst heeft daarbij de voorkeur en een actieve query is een
aparte actie. Dat is geen goedkeuring of uitvoering van hardwaretoegang in
deze opdracht. Groene simulaties alleen maken een schrijvende proef niet veilig.


## 14. Fase 4 — ACK-validatie en bus-exclusiviteit vanaf 1c61210

### ACK-versheid: wat wel en niet kan worden opgelost

De oorspronkelijke69 tests zijn vóór wijzigingen opnieuw uitgevoerd en
slagen:2.372.864 controles plus de bestaande discovery- en validatietests.
De bronvergelijking uit §13 blijft van toepassing; geen apparaat of firmware
is hiervoor benaderd. De onderzochte vijfbyte-CRC-ACK bevat alleen de
vierbyte-UID en88. Geen bloknummer, CRC-echo, sessie-ID, nonce of apparatuurtijd.
De senderhash is geen transactienummer; nulhash blijft ondersteund.

Twee geschiedenissen kunnen daardoor dezelfde waarneming opleveren:

| Geldige geschiedenis | Onveilige geschiedenis | Ontvangen frame |
|---|---|---|
| CRC van blok B verzonden, apparaat commit B en bevestigt | CRC van B verzonden, B niet gecommit, vertraagde ACK van A arriveert | Dezelfde `0037hhhh`, DLC5, U/88 tijdens `WAIT_CRC` |

UID-, DLC-, status-, fase- en lokale TX-receiptcontroles blijven noodzakelijk,
maar onderscheiden die geschiedenissen niet. Deduplicatie op bytes zou ook
iedere normale tweede CRC-ACK verwerpen. Een nieuwe CAN-hash, wachttijd,
lege queue of hostontvangsttimestamp bewijst niet bij welk blok de ACK hoort.
Een hosttimestamp bewijst hoogstens wanneer het frame lokaal is gezien, niet
wanneer de MS2 het maakte of welke write persistent voltooid is. Een extra
selector is evenmin een bewezen readback en kan zelf schrijf-/wisgedrag hebben.

**Protocolcorrecte oplossing bij deze onbewezen versheid:** geen live
programmeeroverdracht beginnen. Dit stopt vóór een reset of eerste blokwrite,
waarmee een twijfelgeval niet pas na gedeeltelijk overschrijven wordt ontdekt:

- `linux_main.c` zet `live_update_enabled=0`, zonder runtime-override. Normale
  `update` en `update --bootloader` eindigen met exit77 vóór discovery,
  firmwaretoegang of CAN-interfacegebruik. De reden staat in stderr en help.
- `socketcan.c::ms2x_linux_open` weigert `MS2X_LINUX_UPDATE` met EPERM vóór
  `if_nametoindex`, socket en bind. Dit geldt ook voor een rechtstreekse
  adaptergebruiker die de CLI omzeilt. De write-grens weigert die modus ook;
  een gesloten adapter weigert met EBADF, zonder syscall-write.
- Er is geen ACK-veld toegevoegd, bestaande controle verwijderd, nieuwe
  firmware geaccepteerd of verborgen force/simulation-vlag aan de live CLI
  toegevoegd. De bestaande gevalideerde RAM-overdracht blijft bruikbaar
  voor onderzoek. De pure protocolengine bewijst uit zichzelf geen versheid.

De oude-ACK-test blijft bewust het ononderscheidbare geval in de **offline
parser** reproduceren. Dat is geen opgeloste parserbeperking: twee hostblokken
kunnen daar nog tegenover één peercommit staan. De operationele oplossing
is de harde weigering van het live schrijfpad. Een externe gebruiker die
zelf een ander hardwaretransport rond de pure library bouwt, krijgt hiermee
geen veiligheids- of versheidsgarantie. Opnieuw activeren vereist onafhankelijk
bewijs of een aantoonbare transportgarantie en aparte review, niet alleen
het terugzetten van een constante.

### Zichtbare interferentie wordt geweigerd

`session.c` voegt kleine controles toe rondom de bestaande state machine:

1. Probe en firmwarefase filteren nu ook de concrete0036-commando's en
   systeemcommando's0000 naast0031/0037. Ontvangen bootquery, selector, data,
   CRC, start11 of reset80 is ander updaterverkeer. Ook verkeer van andere
   lokale sockets wordt bekeken; `RECV_OWN_MSGS` blijft uit. De sessie stopt
   zodra zo'n record is verwerkt. Lokale software-identiteiten worden niet
   als fysieke apparaten geselecteerd; lokale ACK's tijdens update zijn een fout.
2. De firmwarefase onthoudt de gekozen normale identiteit en volgende
   identiteiten. Ander ondersteund MS2-apparaat, andere bootloader,
   conflicterend type/hash/versie of overflow veroorzaakt een terminale fout.
   Een botsing met onze senderhash4711 wordt geweigerd, ook wanneer die al
   in de gecachete identiteit zit. Verschillende app-/boothashes en een
   legitieme nieuwe applicatieversie na start11 blijven mogelijk.
3. Een boot-ACK/status met een andere UID wordt niet geaccepteerd en niet
   langer stil genegeerd: de sessie stopt onmiddellijk. Een gewone
   niet-MS2-identiteit met andere UID en eigen hash mag worden waargenomen;
   ze kan geen doel of ACK voor de overdracht worden. Hashbotsingen stoppen wel.
4. `bus_conflict` is sticky binnen één sessie, verschijnt in de CLI-uitkomst
   en blokkeert ook expliciete doelkeuze via `ms2x_select_device`. De sessie
   krijgt `MS2X_PROTOCOL` plus reden, trekt pending TX in en hervat niet na
   een later antwoord. Een busconflict kan nooit als uitsluitend resourcefout
   met firmware-succes worden teruggegeven. Er is geen automatische reset, retry of reconnect.

Direct herkenbare oude of dubbele antwoorden blijven fouten van de engine:
verkeerde selector, premature88, tweede selector-ACK tijdens data en tweede
CRC-ACK tijdens de volgende selectorfase. Tests controleren nul volgende
TX na het verwerken van het conflict. Ze claimen niet dat frames die al in
kernel, bridge of controller staan kunnen worden teruggetrokken.

### Grenzen van exclusiviteit en gelijktijdige processen

De detector is **geen buslock of bewijs dat alle deelnemers bekend zijn**.
Een stil apparaat kan later actief worden; een vreemd proces kan tussen
observatie en zending starten; een vervalst frame met dezelfde UID is niet
cryptografisch te herkennen. CAN-arbitrage kent geen lease voor een hele
firmwareoverdracht. De gewone datadelen hebben geen doel-UID. Bovendien
bestrijken onze filters niet iedere mogelijke prioriteits-/routingvariant.
De RX-loop verwerkt eindige batches: nog niet gelezen interferentie is niet
al gedetecteerd. Deadlinecontroles en zichtbare conflictstop blijven zinvol,
maar vormen geen garantie over onzichtbare of vertraagde frames.

Een expliciete doel-UID bij een probe mag nog steeds één apparaat uit een
meervoudige inventaris tonen; dit is identificatie, geen toestemming om op die
bus firmwaredata te versturen.

Een lokale advisory filelock zou alleen meewerkende processen op dezelfde
host coördineren. Zij blokkeert geen ander programma, andere host, bridge of
MS2 en bewijst geen ACK-versheid. Daarom is geen lockframework gebouwd.
Twee instanties van deze CLI kunnen allebei niet live programmeren: beide
worden vóór interfacegebruik geweigerd. Een al elders draaiende updater is
waarneembaar via conflictframes, voor zover die in onze filters zichtbaar zijn;
hij kan niet door ons betrouwbaar worden uitgesloten of gestopt.

De bestaande standalone resourceservice is niet het bootloaderblokprotocol
of een omweg om deze gate te openen. Die service blijft een afzonderlijk
schrijfbaar commando, buiten de scope van de niet-schrijvende identificatieproef.
Dit project wordt hiermee dus niet als volledig read-only aangeboden.

### Uitgevoerde tests

`make BUILD=/tmp/ms2-recovery-phase4 check-recovery` vanuit `src/ms2x-c`
hergebruikt de native RAM-peer, bestaande discovery-suite en de bestaande
SocketCAN-harness. Het ontbrekende `tests/linux_test_entry.h` is met alleen
de test-entrydeclaratie hersteld. Een nieuwe testkeuze
`--identification-only` en target `check-identification` voeren precies de
relevante adapter/CLI-controles uit zonder firmware of apparaten. Socket,
interface-opzoeking, bind, read/write, poll en klok zijn daar link-wrapped.

| Controle | Resultaat |
|---|---|
| Bestaande69 scenario's na wijzigingen | Slagen; normale overdracht en12 recoverycombinaties blijven werken in RAM. Verkeerde UID stopt nu direct met protocolconflict in plaats van timeout |
| Dubbele selector/CRC-ACK | Abort vóór volgende data/selector; geen extra hostvoortgang door de herkenbare duplicaat |
| Vreemde selector/CRC-ACK, lokale CRC-ACK | Geweigerd; geen onterechte blokbevestiging |
| Tweede normale MS2/bootloader, hashbotsing, gewijzigde gecachete identiteit | Geweigerd; gecachete senderhashbotsing stopt met nul TX |
| Concurrerende data tijdens de databurst | Stop na64 delen, vóór het volgende deel, zodra geïnjecteerd frame ontvangen is |
| Late ACK na abort | Geen nieuwe TX, geen tellerverhoging en geen hervatting |
| Onverwante gewone CAN-identiteit | Volledige RAM-overdracht blijft slagen, zonder deze als doel of ACK te gebruiken |
| Actuele RAM-suite | 83 scenario's /2.487.366 controles geslaagd; inclusief de expliciet gelabelde onoplosbare stale-ACK-reproductie |
| Discovery/metadata/firmwarevalidatie | 19 discoverygevallen /335 controles, drie productieprofielen en28 imageafwijzingen geslaagd |
| Identificatie- en gate-harness | 709 controles geslaagd: update77 en adapterEPERM vóór interface-opzoeking/socket, geen write na geweigerde open; passief0 writes, actieve bootquery exact één leeg frame |
| Remote/lokale andere updater in de probe | Query, selector, data, CRC, start11 en reset80 geven conflict; een al klaargezette diagnostische query wordt dan niet verstuurd |
| Bestaande volledige SocketCAN/CLI-harness | Ook uitgevoerd zonder bestaande imagefile:1191 controles geslaagd, alle apparaat/syscalls onderschept |
| Catalogusscheiding | Oorspronkelijke productiehashes behouden, synthetische hashes alleen in testbinary |

Dezelfde `check-recovery` slaagt met ASan/UBSan en LeakSanitizer, met buildpad
`/tmp/ms2-recovery-phase4-sanitize` en de eerdere compileropties. Uitvoering
buiten ptrace is afzonderlijk toegestaan; leakdetectie blijft aan. Geen
sanitizerfouten. De volledige historische firmware-/capture-afhankelijke
`make check` is niet uitgevoerd. Haar ontbrekende testheader en oude gate-
verwachting zijn nu verholpen, maar andere niet meegeleverde inputs blijven.
Alle werkzaamheden bleven offline; geen echte CLI-probe op een CAN-interface,
geen firmwaredownload of hardwareflash. README's zijn niet gewijzigd.

### Advies: afzonderlijke identificatieproef

**Klaar voor een afzonderlijk geautoriseerde passieve hardware-identificatieproef,
niet voor firmwareflash of schrijvende recovery.** De offline transporttests
bevestigen dat passief observeren geen CAN-zending doet en geen firmware-engine
of resource-aanbod start. Dit bewijst niet dat een echte MS2 in de bedoelde
bootmodus zal antwoorden; juist dat ontbrekende bewijs kan een begrensde
identificatieproef verzamelen. Een stille bus/ontbrekend antwoord geeft geen
recht om alsnog te flashen of het hardwaretype te raden.

Een actieve identificatiequery is een CAN-schrijfhandeling, ook als zij geen
flashblok bevat. Zij vereist daarom afzonderlijke autorisatie en een
vooraf beoordeelde buscontext; zij valt niet automatisch onder toestemming
voor een passieve proef. Geen van beide proeven is in deze fase uitgevoerd.
De fundamentele ACK-versheid en fysieke bus-exclusiviteit zijn nog niet bewezen.
