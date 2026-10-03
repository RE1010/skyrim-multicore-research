# Bewegungsscan: Kandidat im Stichprobenvergleich

Stand: 2. Oktober 2026. `MovementMessageProbe.dll` Version 2 für die exakt geprüfte SkyrimSE.exe 1.7.104.0. Die zusätzliche Suche verändert noch keine Engine-Entscheidung. Ein Laufzeitgewinn ist bisher **nicht nachgewiesen**.

Die erste Live-Diagnose erfasste 384.888 Originalaufrufe, darunter nur fünf Treffer, mit langen Nachrichtenlisten. Ein dauerhafter Typindex könnte negative Suchen günstiger machen, braucht aber vollständig nachgewiesene Mutations- und Lebensdauerregeln. Die lokale Untersuchung findet unter anderem eine gesperrte Verarbeitung bei `0x7977b0`, die die Elementzahl bei `controller + 0x168` zurücksetzt. Sie belegt noch nicht alle Änderungen an dieser Liste. Deshalb wurde zunächst ein zustandsloser Scan gebaut.

## Berechnung und Anbindung

`tools/Inspect-MovementRuntime.py` erkennt vollständige ausführbare Getter-Sequenzen in der hashgeprüften lokalen EXE: 80 Getter mit lazy initialisiertem globalem Typwert sowie 1.042 einfache Getter für globale Werte, Objektfelder oder Konstanten. Die insgesamt 1.122 Körper sind eine Kandidatenmenge, keine Liste von 1.122 Bewegungsnachrichtentypen. Die Beschreibung darf nur verwendet werden, wenn der tatsächliche virtuelle Getter-Zeiger exakt passt und die vollständigen zugehörigen Codebytes zur geprüften Sequenz passen. Private Originalbytes werden nur im ignorierten Build-Verzeichnis erzeugt.

Die CALL-Anbindung und Original-Lock-Semantik bleiben wie in Version 1. Beim ersten und anschließend jedem 64. Aufruf je Thread führt der Wrapper nach dem originalen Lock-Erwerb einen zusätzlichen Scan aus. Er liest dieselbe Liste und die aktuellen Typwerte neu, ohne virtuelle Getter-Aufrufe. Eine nur für diesen Scan gültige kleine Tabelle hält immutable Funktionsbeschreibungen; sie speichert keine Typwerte, Suchergebnisse oder Engine-Objekte über den Aufruf hinaus. Die Komplexität bleibt O(n). Die vorhandenen Engine-Worker führen diesen Scan aus; neue Spiel-Worker werden nicht gestartet.

Bei unbekanntem Getter oder einem noch nicht initialisierten Typwert bricht der Kandidat ab. Die Originalsuche läuft in jedem Fall genau einmal, inklusive Originalresolver und Lock-Freigabe, und liefert das maßgebliche Ergebnis. Vollständige Kandidatenergebnisse werden damit verglichen. Jede Abweichung deaktiviert weitere Kandidatenscans. Akzeptierte Getter werden beim Plugin-Load und nochmals vor Messbeginn geprüft. Änderungen während der Messung sind nicht vollständig abgedeckt; das ist noch keine Kompatibilitätsfreigabe für beliebige weitere Engine-Mods.

## Prüfung und erster Laborbefund

Alle acht CTest-Prüfungen bestehen. Der neue Test vergleicht 4.037 Entscheidungen mit dem relocierten originalen Such-Maschinencode. Er enthält fehlende und früh/spät gefundene Typen, Null-Einträge, Listen bis 50.755 Einträge, Änderungen des Typs bei gleicher Adresse und Listenlänge, kalte Typresolver und unbekannte Getter. Acht Threads führen 4.000 Aufrufe auf einem gemeinsamen Controller mit dem Original-Lock aus. Eine absichtlich falsche Getter-Beschreibung prüft, dass eine Abweichung den Kandidaten abschaltet und der Originalrückgabewert erhalten bleibt.

Der einmalige gepaarte Laborlauf mit 250 ausgewerteten Stichproben je Fall ergab:

| Einträge | Getter-Mischung | Kandidat, Mittel µs | Originalschätzung, Mittel µs |
|---:|---|---:|---:|
| 1.024 | homogen | 2,89 | 1,92 |
| 10.000 | homogen | 18,09 | 13,97 |
| 50.755 | homogen | 107,10 | 81,08 |
| 1.024 | gemischt | 2,05 | 1,70 |
| 10.000 | gemischt | 20,37 | 16,30 |
| 50.755 | gemischt | 113,33 | 88,95 |

Der Kandidat ist hier langsamer. Dieser Ansatz rechtfertigt deshalb noch keinen Ersatz der Originalsuche. Die Laborlisten referenzieren wenige kontrollierte Nachrichtenobjekte; ein Live-Test prüft Abdeckung und Kosten mit echten Objekten. Das Laborergebnis ist kein repräsentativer FPS-Test. Numerische Evidenz: `research/MOVEMENT-FAST-LAB.json`.

## Aufnahme und Auswertung

Installation erfolgt bei regulär beendetem Spiel mit `tools/Install-MovementProbe.ps1 -PauseVisibility`, inklusive Vorgängersicherung und Hashprüfung. Anschließend über SKSE starten und die Außenszene mit den gemeldeten 600 Wachen laden. `tools/Start-MovementCapture.ps1` startet nach acht Sekunden Wechselzeit eine einmalige Aufnahme von etwa 20 Sekunden. Währenddessen Szene und Kamera stabil halten und keine zusätzlichen NPCs spawnen.

Das Berichtsformat hat `schemaVersion: 2` und `mode: movement-fast-shadow`. Die CSV ergänzt `FastTicks`, `FastStatus`, `FastResult`, `Inspected`, `NonNull` und `UnsupportedGetterRVA`. Status 0 bedeutet vollständiger Vergleich, 1 unbekannter Getter, 2 kalter Getter, 3 kalter Zieltyp und 4 deaktivierter Kandidat. `fastCompared`, `fastMismatches` und `fastUnsupported` zählen auch Stichproben jenseits einer ausgeschöpften CSV-Kapazität.

`tools/Summarize-Movement.py` prüft Status, Zähler, Stichproben, Listen-/Scanlängen und Zeitgrenzen. Tatsächliche Abweichungen werden ausdrücklich ausgewiesen und verhindern eine positive Bewertung; sie werden nicht still herausgefiltert. Der Bericht unterscheidet vergleichbare und abgebrochene Kandidatenscans und nennt unbekannte Getter-RVAs.

`FunctionTicks` enthält jetzt den zusätzlichen Kandidatenscan. `FunctionTicks - FastTicks` ist eine Schätzung der Original-Wandzeit samt Lock und verbleibendem Diagnoseaufwand. Der Kandidat läuft zuerst, verändert den Cachezustand und verlängert das Halten des Locks. Ein Verhältnis der Zeiten ist damit kein unverzerrter Beschleunigungsfaktor. Eine neue FPS-Aussage braucht später einen echten Ersatzpfad und einen kontrollierten Vergleich derselben Szene mit deaktivierter Diagnose.

Erst nach dem Live-Befund wird entschieden, ob dieser Scan weiter verbessert wird oder ein nachweislich korrekt invalidierter Typindex nötig ist. Zusätzliche Threads allein beseitigen die bereits auf mehreren Engine-Workern ausgeführte lange Suche nicht.

## Installation und Startnachweis

Version 2 wurde lokal um 22:58 Uhr installiert. Die Vorgängerversion ist unter `measurements/movement-installation-20261002-225850/backup/` gesichert; das Manifest bestätigt beide Kopien. SHA256 der neuen DLL: `87DEBFF30BF5B8BAC4E99E0DC2A868B796ADEF5ED2641925DDD2D2B702DF9D76`. Die Sichtbarkeits-DLL bleibt für diesen Test pausiert.

Der SKSE-Start um 22:59 Uhr erzeugte Prozess 38732. Die Modulliste bestätigt SKSE und die neue Bewegungs-DLL. Die Sitzung `measurements/live-movement/session-38732-49623171/` enthält 6.665 gültige Stichproben und 1.321 vollständige Vergleiche ohne Abweichungen. Die 5.344 übrigen Scans brachen am zuvor unbekannten Getter für `MovementMessageActorCollision` ab; keine lange Liste wurde vollständig verglichen. [Ergebnis und ergänzte Version 3](../research/LIVE-MOVEMENT-002.md).

Version 3 ist gebaut und mit allen acht CTest-Prüfungen getestet. Sie ergänzt den vollständig geprüften JMP/131-Byte-Getter und dessen Initialisierungshelfer. Der readonly Kandidat liest den Typwert ausschließlich nach konservativ bestätigtem Initialisierungsabschluss; die Originalsuche übernimmt weiterhin sämtliche Entscheidungen und Initialisierungsarbeit. Diese Ergänzung erweitert die Menge auf 1.123 Beschreibungen. Auch der dazugehörige Laborlauf zeigt bisher keinen Laufzeitgewinn.

Version 3 wurde um 23:10 Uhr mit Vorgängersicherung installiert: `measurements/movement-installation-20261002-231038/manifest.json`. DLL-SHA256: `5B1A4514291FEDD84D2133B389F01FC4AB623BA6ECD2576BA095DE2DF6D8555F`. Der neue SKSE-Prozess 31304 lädt SKSE und die Bewegungs-DLL, ohne aktive Sichtbarkeits-DLL. Die Sitzung `measurements/live-movement/session-31304-50327421/` bestätigt 1.123 geprüfte Beschreibungen. Ihre Aufnahme ist abgeschlossen: 5.875 vollständige Vergleiche ohne Abweichung, darunter 3.334 lange Listen. Die Zeitwerte zeigen bisher keinen Laufzeitgewinn; der zusätzliche Scan ist nach Aufnahmeende inaktiv. [Vollständiger Live-Befund](../research/LIVE-MOVEMENT-003.md).
