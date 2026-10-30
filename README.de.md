# MS2 Updater

**Sprache:** [English](README.md) · [Nederlands](README.nl.md) · **Deutsch**

Unabhängiger, von der Community gepflegter Updater für die Märklin Mobile Station 2 (MS2) über Linux SocketCAN.

**Status: [v1.0.0-rc1](https://github.com/Calimerorulez/ms2-updater/releases/tag/v1.0.0-rc1) ist als Vorabversion veröffentlicht.** Eine statisch gelinkte Binärdatei für SRSEII / Onion Omega2+ ist verfügbar und wurde auf einer echten SRSEII mit einer Identifikationsabfrage getestet. Ein **vollständiges Firmware-Update wurde mit dieser öffentlichen Binärdatei nicht erneut getestet**. Firmware-Updates erfolgen auf eigenes Risiko.

Siehe [RELEASE.md](RELEASE.md) für die überprüfte SHA256-Prüfsumme, Installationshinweise und verbleibende Einschränkungen.

## Kompilieren

```sh
cd src/ms2x-c
make build/ms2-updater
./build/ms2-updater --help
```

Benötigt einen C11-Compiler, `make`, Linux und SocketCAN-Header. Für einen OpenWrt-Cross-Build für Omega2+:

```sh
make -f Makefile.cross all CROSS_COMPILE=<pfad-zum-toolchain-praefix>
```

`Makefile.cross` erstellt und prüft das Offline-Werkzeug und kompiliert außerdem die statisch gelinkte SocketCAN-CLI nach `cross-build/omega2-static/release/ms2-updater.stripped`.

Die vollständigen historischen Tests benötigen separat beschaffte offizielle Firmwaredateien sowie einige nicht öffentliche, anonymisierte CAN-Mitschnitte. Diese Dateien sind bewusst nicht enthalten. `make check` ist deshalb derzeit kein vollständig eigenständig ausführbarer Testlauf.

Zwei Tests funktionieren ohne Firmwaredateien und ohne CAN-Hardware:

```sh
make check-profiles
make check-firmware-reject
```

## Verwendung

Passive Beobachtung (keine CAN-Nachrichten senden):

```sh
./build/ms2-updater probe --interface can0 --seconds 10
```

Aktive Identifikation (eine Identifikationsabfrage über CAN):

```sh
./build/ms2-updater probe --interface can0 --seconds 10 --query
```

**Warnung:** `update` und `serve-resources` können den Zustand angeschlossener Geräte verändern. Diese Befehle sind im Quellcode aktiviert und sollten erst verwendet werden, wenn Zielauswahl, Firmware und Installationsumgebung unabhängig überprüft wurden. Proprietäre Firmwaredateien sind nicht Teil der Vorabversion.

## Geräteunabhängige Zielauswahl

Die CLI benötigt keine fest einprogrammierte MS2-UID, sondern erkennt MS2-Geräte zur Laufzeit. Sind mehrere geeignete MS2-Geräte vorhanden, wird das gewünschte Gerät mit `--target UID` ausdrücklich ausgewählt.

Firmwareprofile sind für die Hardwaretypen `0032`, `0033` und `0034` vorhanden. Das Firmwareabbild muss exakt zum erkannten Hardwareprofil passen. Unbekannte Revisionen werden abgelehnt. **Nur Typ `0033` wurde mit dieser öffentlichen Binärdatei auf echter Hardware geprüft; `0032` und `0034` wurden offline getestet.**

Historische Offline-Testdaten verwenden künstliche UIDs; sie bestimmen kein festes Zielgerät.

## Häufige Fragen: Firmware, Risiken und Wiederherstellung

**Benötige ich eine CS2 oder CS3, um die MS2-Firmware zu bekommen?** Märklin verteilt MS2-Firmware üblicherweise als Bestandteil von CS2/CS3-Updatepaketen. Das benötigte MS2-Abbild kann aus einem rechtmäßig bezogenen offiziellen Updatepaket extrahiert werden; allein für die Beschaffung der Datei ist nicht zwingend eine eigene CS2 oder CS3 erforderlich. Dieses Repository enthält **weder** einen Extraktor **noch** Märklin-Firmware. Extraktionsschritte und Paketformate wurden hier nicht validiert. Bitte nur offizielle, berechtigte Quellen verwenden und das Abbild vor einem Update überprüfen.

**Welche Datei passt zu meiner MS2?** Die erkannte Hardware-ID bestimmt das Profil: `0032` → `050-ms2.bin`, `0033` → `051-ms2.bin`, `0034` → `052-ms2.bin`. Das sind Profildateinamen, **keine** Zusage, dass jede Datei mit demselben Namen geeignet ist. Ein unpassendes Abbild niemals umbenennen, um die Prüfung zu umgehen. Unbekannte Hardware oder abgelehnte Firmware nicht erzwingen. Der tatsächliche Testumfang steht in [RELEASE.md](RELEASE.md).

**Wie sicher ist das mit nur einer SRSEII und einer MS2?** Die öffentliche Vorabversion hat auf einer echten SRSEII eine MS2 vom Typ `0033` identifiziert, aber **mit der veröffentlichten Binärdatei noch kein vollständiges Firmware-Update durchgeführt**. Die Typen `0032` und `0034` wurden nur offline getestet. Ein fehlgeschlagener Flash-Vorgang kann die MS2 unbrauchbar machen. Wer dieses Risiko nicht tragen kann, sollte sich auf die passive Beobachtung beschränken und unabhängig dokumentierte vollständige Flash-Tests abwarten.

**Was passiert bei Stromausfall oder einer unterbrochenen USB-/Netzwerk- oder CAN-Verbindung?** Für diese Version ist **keine zuverlässig getestete Fortsetzungs- oder Wiederherstellungsprozedur** dokumentiert oder garantiert. Es darf nicht angenommen werden, dass ein erneuter `update`-Aufruf eine teilweise programmierte MS2 wiederherstellt. Für stabile Versorgung und CAN-Verkabelung sorgen und laufende Updates nicht unterbrechen. Im Fehlerfall Protokolle sowie genaue Geräte- und Abbilddaten sichern und vor einem erneuten Versuch gerätespezifische Wiederherstellungshinweise einholen. Eine automatische Wiederherstellung von SRSEII oder MS2 ist nicht nachgewiesen.

## Firmware

Firmware, Lokomotivdatenbanken sowie Sprach- und Ressourcendateien müssen separat und rechtmäßig bezogen werden. Sie werden nicht über dieses Repository verteilt.

## Datenschutz, Lizenzen und Herkunft

Der aktuelle Quellcode wurde auf private Netzwerkangaben und ursprüngliche Test-UIDs überprüft. Die Haupt-Git-Historie wurde durch einen bereinigten initialen Commit ersetzt. Frühere Objekte können weiterhin in GitHub-Caches oder externen Kopien vorhanden sein.

Für abgeleiteten Code Dritter gelten deren ursprüngliche Lizenz- und Namensnennungsbedingungen; siehe [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) und [src/ms2x-c/LICENSE.gerd](src/ms2x-c/LICENSE.gerd).

Eigene Projektbeiträge stehen unter der MIT-Lizenz. Dieses Projekt ist unabhängig und weder mit Märklin verbunden noch von Märklin unterstützt.
