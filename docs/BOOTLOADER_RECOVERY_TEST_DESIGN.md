# Offline testontwerp voor MS2-bootloaderherstel

Datum: 2026-10-30. Onderzochte productiebron: commit `9dcbd05`.
Dit document hoort bij [de analyse](BOOTLOADER_RECOVERY_ANALYSIS.md).
Alle apparaatidentiteiten en events hieronder zijn synthetisch.

## 1. Direct reproduceerbare tests zonder firmware

Vanuit `src/ms2x-c`:

```sh
make BUILD=/tmp/ms2-recovery-analysis-build check-profiles check-firmware-reject
```

Deze bestaande targets controleren profielmetadata en verwerpen gefabriceerde
images. Ze laden geen Märklin-firmware en hebben geen CAN-backend.

De onderstaande C-driver hergebruikt uitsluitend de bestaande RAM-harness en
haar 19 discovery-scenario's (waarvan één momenteel faalt). De oorspronkelijke `main`, firmwaretests en
tracelezers worden niet aangeroepen. Negen extra probe-scenario's controleren
een actieve query zonder antwoord, app- en bootidentificatie, boot-NACK's,
een ontbrekend hardwaretype en een leesfout. `query=1` betekent hier uitsluitend
een frame in RAM; `socketcan.c` en `linux_main.c` worden niet gelinkt.

Sla het C-blok tijdelijk op als `/tmp/ms2-recovery-probe.c`. Compileer en voer
het uit vanuit de repositoryroot met:

```sh
cc -std=c11 -Wall -Wextra -Werror -I src/ms2x-c/include -I src/ms2x-c/tests /tmp/ms2-recovery-probe.c src/ms2x-c/ms2x.c src/ms2x-c/sha256.c src/ms2x-c/can_offline.c src/ms2x-c/can_sim_loop.c src/ms2x-c/session.c src/ms2x-c/resource_server.c -Wl,--wrap=ms2x_tx_processed -o /tmp/ms2-recovery-probe
/tmp/ms2-recovery-probe
```

```c
/* research-probe-driver */
int research_historical_main(int argc, char **argv);
#define main research_historical_main
#include "test_can_sim_loop.c"
#undef main

static void probe_cases(void)
{
    unsigned before = checks;
    for (unsigned n = 0; n < 9; ++n) {
        ms2x_session_config cfg = {0, n == 1 ? 0 : 1, 1,
                                  0x11223344U, NULL, 100U, NULL, 0, 0U};
        ms2x_session_result r;
        ms2x_identity id;
        memset(&sim, 0, sizeof(sim));
        sim.integrated = 1;
        sim.target = SIZE_MAX;
        sim.prefix_pos = SIZE_MAX;
        memset(trace, 0, sizeof(trace));
        events = 1;
        trace[0].tx = 1;
        trace[0].frame.extended = 1;
        trace[0].frame.id = cfg.bootloader ? 0x00365a22U : 0x00304711U;
        if (n != 0 && n != 8) {
            events = 2;
            trace[1].at = 1;
            trace[1].frame = discovered(cfg.target, 0x33U, 0x1234U,
                                        n != 1 && n != 3);
            if (n == 4 || n == 5) {
                trace[1].frame.data[4] = 0xf1U;
                if (n == 4) trace[1].frame.dlc = 5;
            }
            if (n == 6) trace[1].frame.data[7] = 0;
            if (n == 7) trace[1].frame.dlc = 7;
        }
        if (n == 8) { sim.fault = READ_ERROR; sim.target = 1; }
        int result = ms2x_session_run(NULL, &cfg, &r);
        CHECK(sim.tx == 1 && sim.data == 0 && sim.blocks == 0);
        CHECK(!r.firmware_local && !r.firmware_protocol && !r.start_local);
        CHECK(ms2x_select_device(&r, cfg.target, cfg.bootloader, &id) ==
              (n == 1 || n == 2 ? MS2X_SELECTED :
               n == 6 ? MS2X_SELECT_UNSUPPORTED :
               n == 8 ? MS2X_SELECT_AMBIGUOUS : MS2X_SELECT_NONE));
        if (n == 8) CHECK(result != 0 && r.error == MS2X_IO);
        else CHECK(result == 0 && r.window_ended && sim.now == 100);
        if (n == 2) CHECK(id.bootloader && r.normal_seen == 0);
    }
    printf("Research probes:9 scenarios, %u checks passed; RAM only\n",
           checks - before);
}

int main(void)
{
    probe_cases();
#ifndef RESEARCH_PROBE_ONLY
    discovery();
#endif
    return 0;
}
```

Uitgevoerd tijdens dit onderzoek: de profieltest slaagde; alle 25 negatieve
firmwarechecks slaagden; de negen extra probes slaagden met 120 controles.
De bestaande discovery-suite passeerde scenario's 0–15 en faalde bij scenario
16 op `test_can_sim_loop.c:438`. Die fixture geeft twee apparaten **dezelfde
UID** en verschillende typen (`0033` en `0011`), terwijl de verwachting
`SELECTED` is. De implementatie markeert dit terecht als ambigu. Scenario's
17–18 zijn door die vroege exit niet uitgevoerd. De repositorytests zijn
ongewijzigd gelaten. Een zelfstandige groene uitvoering van alleen de negen
extra probes gebruikt bij dezelfde compileeropdracht `-DRESEARCH_PROBE_ONLY`.
Dit zijn
geen volledige flash- of hersteltests. Een probe zonder antwoord sluit zijn
observatievenster normaal af; het selectieresultaat moet apart worden getoetst.

## 2. Waarom volledige synthetische overdracht nog niet kan

`ms2x_firmware` is opaque. De enige publieke fabrikanten valideren de exacte
productie-SHA256. Geen synthetische inhoud heeft die hash. Daardoor kunnen
we zonder productieaanpassing geen positieve engine-overdracht met uitsluitend
synthetische bytes opzetten via de ondersteunde API.

Niet toegestaan als oplossing: SHA256 mocken, een private firmwarestructuur
namaken, productiehashes wijzigen, een `--force` toevoegen of een bestaand
firmwarebestand gebruiken. In deze onderzoekswijziging is niets daarvan gedaan.

Voor een volgende implementatiefase: splits de onveranderlijke productcatalogus
en de generieke validator. Maak een afzonderlijk testprogramma met dezelfde
validator, een uitsluitend in dat programma gelinkte synthetische catalogus
en vooraf vastgelegde hashes voor drie deterministische patronen. De gewone
CLI mag deze catalogus, testfabriek of selector nooit bevatten. Verifieer die
afwezigheid in de build. Ook in tests blijven lengte, header, type en SHA256
verplicht. Test productcatalogusafwijzing van dezelfde patronen afzonderlijk.
Dit is een voorstel; deze refactor is niet uitgevoerd of goedgekeurd als
productie-recoveryfunctie.

## 3. Ontwerp van het gesimuleerde apparaat

Gebruik de vijf bestaande `ms2x_sim_*`-symbolen als RAM-transport, met logische
monotone tijd. Geen SocketCAN, vcan, UART, netwerk, subprocess voor apparaten,
of imports uit een private checkout. De peer krijgt UID en hardwaretype per
test en kent het verwachte image niet. Hij assembleert de ontvangen bytes en
berekent CRC onafhankelijk, bijvoorbeeld met Python `binascii.crc_hqx`.

Peer-toestanden: `APP`, `BOOT_IDLE`, `SELECTED`, `RECEIVING`,
`BLOCK_ACCEPTED`, `APP_STARTED`. Verwerk alleen complete canonieke frames.
Geheugen bestaat uit een blokbuffer en een aparte array voor persistente
applicatieblokken. Houd bootblokken buiten die array. Dat modelleert de
schrijfgrens, geen bewezen fysieke beveiliging.

Maak flashsemantiek parametriseerbaar: wissen bij selector versus bij CRC;
partial writes versus commit na correcte CRC; buffer behouden versus verliezen
bij reset; bootloader blijft actief versus start applicatie na watchdog.
Een test van slechts één gunstige variant mag geen herstelbewijs heten.
Bootversies, responsehash en applicationhash mogen verschillen. UID blijft een
testinput; test een gewijzigde UID als identiteitsverlies, zonder relatie te raden.

## 4. Verplichte scenariomatrix

Status **deeltest** betekent dat alleen de hierboven uitgevoerde detectie of
validatie gedekt is. **Ontworpen** betekent niet uitgevoerd met de engine.

| Nr. | Scenario en injectie | Vereiste uitkomst bij toekomstige volledige test | Status nu |
|---|---|---|---|
| 1 | Gezonde applicatie, ieder van de drie profielen | Normale identiteit, expliciete resetautorisatie, twee wachttijden, alle blokken exact, ACK's en geverifieerde appstart; geen recoveryfallback | Deeltest: normale detectie en metadata geslaagd |
| 2 | Alleen bootloader; geen enkele `0031` | Volledige `0037` selecteert exact doel; geen ping/reset nodig; bootstart vereist nieuwe volledige identiteit; identieke blokengine | Deeltest: bootdetectie met en zonder app en query geslaagd |
| 3 | Onderbreek na selector, midden in data, na laatste data, na CRC en na enkele ACK's | Terminale fout, geen verdere writes/start; nieuwe handmatig geautoriseerde sessie begint bij hoogste blok, nooit bij lokale teller; test alle peer-flashvarianten | Ontworpen; huidige engine statisch beoordeeld |
| 4 | `0032/0033/0034` kruislings, verkeerde header/versie/lengte/hash | Geen reset/selector/data/start bij validatiefout; profielconflict blijft fataal ondanks expliciete UID | Deeltest: alle 25 firmwareafwijzingen geslaagd |
| 5 | CAN-time-out tijdens data/CRC; EAGAIN tot deadline; verloren ontvangst | Absolute deadline blijft gelden; geen retry of reconnect; nul nieuwe writes na abort | Ontworpen voor overdracht; RAM-probe-I/O-fout afzonderlijk geslaagd |
| 6 | Geen bootidentiteit of geen selector/CRC-ACK | `WAIT_BOOT/WAIT_SELECT/WAIT_CRC` verlopen; geen data zonder selector-ACK; probe toont alleen 'niet gevonden', nooit succesclaim | Deeltest: lege actieve bootprobe sluit zonder selectie; enginefasen ontworpen |
| 7 | Twee MS2's, ander CAN-apparaat, gelijke hash, duplicaat-UID; tweede bootloader verschijnt later | Geen automatische recoveryselectie; conflictsituaties blokkeren ook met doel-UID; vreemd apparaat ontvangt geen programmeeractie; runtime-conflict stopt volgende TX | Deeltest: meerdere doelen/hashconflict afgewezen; bestaande fixture 16 faalt door onjuiste verwachting; runtimebeleid ontworpen |
| 8 | Type onbekend, nul, ontbrekend, wisselend of alleen handmatig opgegeven | Weiger flash; `--hardware` levert nooit identiteitsbewijs; geen header/UID-prefix/serienummer als vervanging | Deeltest: onbekend/nul type, onvolledig antwoord en hardwareconflicten; bestaande scenario's 17–18 niet bereikt |

Voor iedere overdrachtstest: tel query/reset/selector/data/CRC/start/resources
apart. Controleer de exacte TX-prefix en nul extra frames na de eerste fout.
Controleer de uiteindelijke bytes inclusief FF-padding voor alle drie profielen.
Na laatste ACK is alleen de overdracht bevestigd; een ontbrekend `0031` met de
doel-UID, het doeltype en versie 5.6 blijft een mislukte appverificatie.

Extra regressies: F1..F6 ook met DLC8, onbekende status, verkeerde blok-ACK,
duplicaat-ACK tijdens andere fasen, oude identieke `88` in latere `WAIT_CRC`,
response precies vóór/op/na deadline, dubbele lokale TX-receipt, verkeerde
owner/sequence, korte writes, bus-off, disconnect, SIGINT, RX-flood en
clock overflow/terugloop. Oude ACK-ambiguïteit moet als beperking zichtbaar
blijven; een test die acceptatie reproduceert is geen geslaagde veiligheidsfix.

## 5. Acceptatiegrens

Een volledig groene synthetische suite bewijst hostlogica onder de gekozen
peer-aannames. Voor een betrouwbare recoveryclaim ontbreekt daarna nog
onafhankelijk protocolbewijs van een MS2-bootloader na een echte onderbreking,
inclusief herhaald programmeren en de SRSEII-transportketen. Dit onderzoek
voert zulke hardwaretests niet uit en geeft er geen toestemming voor.
