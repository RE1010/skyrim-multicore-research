# Erster vollständiger Live-Vergleich

2. Oktober 2026, vom Nutzer bestätigte Weißlauf-Szene. SkyrimSE.exe 1.7.104, SKSE 2.3.1, lokales Diagnose-Plugin. Diese Messung prüft Korrektheit und Aufrufverteilung; sie ist kein FPS-Benchmark.

## Ergebnis

| Merkmal | Ergebnis |
|---|---:|
| Aufnahmezustand | `capture-complete` |
| Gespeicherte und verglichene Engine-Aufrufe | 68.739 |
| Sichtbarkeitsabweichungen | 0 |
| Abweichende Plane-Maske nach dem Originalaufruf | 0 |
| Abweichungen zwischen serieller Berechnung und vier Workern | 0 |
| Nicht unterstützte gespeicherte Datensätze | 0 |
| Verworfene Messdatensätze wegen belegter/gefüllter Queue | 31.261 |
| Aufrufende Skyrim-Threads in den gespeicherten Daten | 6 |

Die Diagnose erlaubte 100.000 Aufrufe zur Aufzeichnung. Der Bericht zählt 100.001 Versuche einschließlich des abschließenden Limit-Checks. Von den 100.000 zugelassenen Versuchen wurden 68.739 gespeichert und 31.261 verworfen. Die Originalfunktion wurde auch bei verworfenen Messdatensätzen ausgeführt; ihre Ergebnisse und Maskenänderungen blieben maßgeblich.

Das Plugin meldet die erfolgreiche Wiederherstellung der ursprünglichen Vtable-Slots nach Abschluss. Ein eigenständiger Replay-Prozess las die gespeicherten 68.739 Datensätze und bestätigte dieselben Null-Abweichungen. Dateigröße: 8.798.672 Bytes = 80 Bytes Header + 68.739 × 128 Bytes.

## Aufrufverteilung

| Skyrim-Thread-ID | Gespeicherte Aufrufe |
|---|---:|
| 12324 | 10.383 |
| 17884 | 13.505 |
| 21976 | 11.690 |
| 25780 | 12.635 |
| 30096 | 9.129 |
| 39608 | 11.397 |

Dies sind die aufrufenden Engine-Threads, nicht die vier zusätzlich gestarteten Diagnose-Worker. Eine Main-/Render-Thread-Zuordnung und CPU-Kosten pro Aufruf wurden in dieser Aufnahme nicht gemessen. Die Verteilung belegt, dass der erfasste Culling-Pfad in dieser Szene bereits von mehreren Threads aufgerufen wird.

## Belege

Sitzung: `measurements/live-culling/session-37216-43266062/`

- `summary.json`: Abschluss, Zähler, Ergebnisvergleich und Thread-Verteilung.
- `plugin.log`: Runtime-/Code-Prüfung, beide gebundenen Culling-Klassen, Aufnahmestart und Wiederherstellung.
- `snapshots.bin`: numerische Live-Eingaben und Originalergebnisse; SHA256 `C474F912E93D9A9893D8AC169BA9219EDAF70527181D216696A81FC4BFB3625F`.
- `replay.json`: unabhängiger Replay, 68.739 Vergleiche, alle Fehlerzähler null.

Installierte Plugin-DLL und Build-Artefakt besitzen identischen SHA256 `017B9C70B549924B3F388E2FAA602ED2ECC5927411F8443022CE4F0D89135A5E`. Das SKSE-Log bestätigt `loaded correctly (handle 1)`. Das Update-Manifest liegt in `measurements/installation-20261002-211312/manifest.json`; die ursprüngliche SKSE-Installation enthält 66 geprüfte Dateien.

Alle vier CTest-Prüfungen bestehen: Laborkorrektheit, 131.456 Vergleiche mit Original-Maschinencode samt Snapshot-Fehlerinjektion, DLL-/Runtime-/EXE-Guards und Wiederholung einer Berichtspublikation nach einer externen Dateisperre.

Die vorangegangenen Sitzungen bleiben dokumentiert: Ein erster Adapter nur an der Basisklassen-Vtable lieferte keine Daten. Nach Ergänzung der abgeleiteten Geometry-List-Klasse wurden 4.288 gültige Ergebnisse aufgezeichnet, bevor die Berichtspublikation die Diagnose beendete. Dieser Fehler wurde korrigiert und mit einer tatsächlichen Lesesperre getestet. Der hier ausgewertete dritte Durchlauf schloss vollständig ab.

## Bedeutung für das Multicore-Ziel

Die Engine-Anbindung funktioniert und der eigene Runtime-Rechenkern stimmt für die erfassten Daten mit der Originalfunktion überein. Damit ist die Voraussetzung für weiterführende Experimente geschaffen.

Eine Beschleunigung oder Entlastung des Main Threads wurde nicht nachgewiesen. Die parallele Rechnung erfolgte zusätzlich zur Originalfunktion. Eine weitere Verteilung genau dieses bereits mehrfach aufgerufenen Culling-Pfads ist deshalb bisher kein begründeter Main-Thread-Fix. Der nächste notwendige Nachweis ist ein tatsächlich serieller, kostspieliger Abschnitt mit geeigneter Batch-Grenze, ehe dort Originalarbeit ersetzt wird. Weder die synthetische Laborbeschleunigung noch höhere CPU-Auslastung erfüllen dieses Erfolgskriterium.
