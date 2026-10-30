# MS2 bootloader recovery — offline analyse

Datum: 2026-10-30. Repository: `Calimerorulez/ms2-updater`.
Onderzochte uitgangsrevision: `9dcbd05`.

## 1. Conclusie en onderzoeksgrens

**Conclusie: gedeeltelijk ondersteund.** De huidige updater heeft al een
expliciet bootloaderpad, hardwareprofielen en een gedeelde blokoverdracht.
Een werkende applicatiefirmware is voor dat pad niet noodzakelijk **mits de
bootloader een volledige, passende CAN-identiteit teruggeeft**. De lokale
CS3-help documenteert bovendien MS2-herstel na een onderbroken update.

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
Geen productiecode, veiligheidscontrole of README gewijzigd. Bestaande
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

**CODE — `ms2x_profile_for`, ms2x.c:10–30; firmwarevalidatie:101–162.**

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
| Kan een gedeeltelijke update opnieuw worden gestart? | De software kan met een nieuwe engine weer bij het hoogste profielblok beginnen. Geen resume vanaf een lokale teller. Dat de bootloader eerder beschreven en gedeeltelijk gewiste blokken opnieuw accepteert is nog niet bewezen voor deze route en uitvalmomenten. |
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

## 11. Offline testresultaten en testplan

Uitgevoerd op deze checkout, uitsluitend native hostcode en synthetische
buffers/events; buildproducten onder `/tmp`:

| Controle | Resultaat |
|---|---|
| `make BUILD=/tmp/ms2-recovery-analysis-build check-profiles check-firmware-reject` vanuit `src/ms2x-c` | Geslaagd: drie profielmappings en unsupported-afwijzing; 25 negatieve firmwarechecks |
| Negen aanvullende probes via bestaande RAM-loop, zonder Linux-transport | Geslaagd: 120 controles; app/boot, normale app als niet-bootdoel, lege query zonder reply, korte/gepadded NACK, onbekend type, kort antwoord en geïnjecteerde leesfout |
| Bestaande `discovery()` afzonderlijk aangeroepen via tijdelijke testdriver | Scenario's 0–15 passeerden; scenario16 faalt bij `test_can_sim_loop.c:438`; 17–18 niet bereikt |

Scenario16 biedt normale identiteiten met dezelfde UID en verschillende
hardwaretypen aan, maar verwacht `MS2X_SELECTED`. `remember` markeert dit
terecht als ambigu. Dit is een inconsistentie tussen testdata en verwachting;
de veiligheidscontrole mag niet worden versoepeld om de fixture groen te
maken. Er is geen bestaande test- of productiebron aangepast.

De volledige `make check` is niet uitgevoerd. Die vereist echte firmware,
ontbrekende Python-module `ms2updater`, niet meegeleverde referentie-events/
capturedata en voor het SocketCAN-testtarget ook het ontbrekende bestand
`tests/linux_test_entry.h`. Bovendien verwacht `test_socketcan.c:328` een
uitgeschakelde live-gate (exit77), terwijl `linux_main.c` die op 1 zet.
Historische slaagmeldingen vormen dus geen verse testuitslag van deze bron.

De acht gevraagde scenario's, exacte reproduceerbare RAM-driver, reeds
uitgevoerde deeltests en nog uit te voeren volledige overdrachtstests staan
in [BOOTLOADER_RECOVERY_TEST_DESIGN.md](BOOTLOADER_RECOVERY_TEST_DESIGN.md).
Onderbreking en time-out **tijdens firmwareoverdracht** zijn in deze opdracht
statisch beoordeeld en als tests ontworpen, niet dynamisch bewezen.

Een gesimuleerde bootloader kan goed aan de bestaande vijf `ms2x_sim_*`-seams
worden toegevoegd. Positieve engine-overdracht met uitsluitend synthetische
bytes kan nu echter niet via de publieke firmware-API: alleen exacte
productiehashes worden geaccepteerd. De toekomstige tests moeten dezelfde
validatie met een afzonderlijk in de testbinary gelinkte synthetische
catalogus gebruiken, zonder productiehashes of runtime-bypasses te veranderen.

Minimale volledige suite: alle drie profielen, normale en directe bootstart,
alle blokken/CRC/padding, gedeeltelijke overdracht op meerdere momenten,
verkeerd profiel, deadline tijdens data/CRC, ontbrekende boot/selector/CRC-ACK,
meerdere en later verschijnende apparaten, onbekende/wisselende identiteit,
oude ACK's, I/O-fouten en nul extra TX na abort. Een model moet verschillende
erase-/commit-/resetsemantieken kunnen kiezen; modelkeuzes mogen niet als
bewezen hardwaregedrag worden gepresenteerd.

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
