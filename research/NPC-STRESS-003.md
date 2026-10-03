# 600 Wachen vor Weißlauf mit SKSE

2. Oktober 2026, Europe/Berlin. Der Nutzer bestätigte „600 vor Weißlauf sind ready“. Skyrim wurde über den bereits geprüften SKSE-Loader gestartet. Prozess 38040 enthält `skse64_1_7_104.dll` und `MulticoreVisibilityShadow.dll`; Plugin-Log und Modulliste bestätigen den aktuellen Prozess, statt eine frühere Sitzung wiederzuverwenden. Runtime 1.7.104, EXE-SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`.

## Getrennte Aufnahmen

Zuerst lief die CPU-/FPS-Aufnahme, während das Sichtbarkeits-Plugin auf seinen Trigger wartete. Es führte dabei keine Snapshot-Aufzeichnung oder parallele Ergebnisprüfung aus. Seine Vtable-Hooks und Zähler waren jedoch bereits aktiv: Dies ist keine reine SKSE-Messung ohne Diagnose-Overhead. Der Aufwand des wartenden Plugins wurde nicht durch einen kontrollierten A/B-Test bestimmt.

Baseline-Verzeichnis: `measurements/20261002-220239-640-baseline/`. PresentMon zeichnete von 22:02:53 bis 22:03:14 Uhr auf; der ETL-Trace umfasst 21,055153 Sekunden. Erst um 22:03:50 Uhr wurde die zusätzliche Sichtbarkeitsprüfung ausgelöst. Es gab keine Szenenfrage während der Aufnahme. Kamera, Menüs und tatsächliche Actor-Zahl wurden nicht unabhängig überwacht; die Szenenangaben stammen vom Nutzer.

## CPU-/FPS-Ergebnis

| Merkmal | Mit SKSE und wartendem Diagnose-Plugin | Vorheriger Lauf ohne SKSE |
|---|---:|---:|
| Gemeldete zusätzliche Wachen | 600 | 600 |
| Present-Rate | **29,3054/s** | 10,9609/s |
| Mittleres Present-Intervall | 34,1234 ms | 91,2331 ms |
| Median | 33,9719 ms | 90,2757 ms |
| P95 / P99 | 40,5824 / 44,1022 ms | 103,8800 / 108,5503 ms |
| Mittlere GPU-Busy-Zeit | **32,7791 ms** | 9,1766 ms |
| CPU-Laufzeit in logischen Prozessoräquivalenten | 3,7970 | 6,3000 |

582 gültige Frame-Intervalle, eine Swap Chain, 19,8208601 Sekunden zwischen erstem und letztem Present. Keine verlorenen ETW-Ereignisse oder Puffer; PresentMon und WPR beendeten sich erfolgreich. CPU-Zeiten beziehen sich auf das ganze ETL-Intervall. Present-Raten sind nicht automatisch die Zahl einzigartig gerenderter oder angezeigter Bilder.

Die GPU-Busy-Zeit liegt diesmal nahe am gesamten Present-Intervall. Zusätzlich ist der Render-Pfad auf dem Main Thread stark vertreten. Das spricht für eine andere Belastungsverteilung mit erheblichem Grafikanteil. **Der Anstieg von ungefähr 11 auf 29 Presents/s ist kein nachgewiesener SKSE- oder Plugin-Gewinn.** Es wurde keine Originalarbeit durch unseren Rechenkern ersetzt. Blickrichtung, sichtbare Geometrie, Bewegungszustand und Nachrichtenrückstau wurden zwischen den beiden Spielprozessen nicht identisch reproduziert; die Ursache der veränderten Verteilung bleibt offen.

## CPU-Stacks

Thread 32492 startete unmittelbar nach dem Prozess und enthält die Frame-/Render-Aufrufkette. Er beanspruchte 80,7850 % eines logischen Prozessors. Seine Auswertung enthält 16.932 CPU-Samples:

- Render-Aufrufstelle RVA `0x15601d4`: 10.240 inklusive Samples, **60,48 %**.
- Job-Warte-Aufrufstelle RVA `0xebc10d`: 1.748 inklusive Samples, **10,32 %**.
- `Kernelbase!SwitchToThread`: 1.735 inklusive Samples, **10,25 %**.

Die inklusiven Anteile sind verschachtelt und dürfen nicht addiert werden. Sample-Anteile sind keine direkte Messung blockierter Wartezeit.

Die sechs Engine-Worker 9488, 19836, 27564, 28596, 34604 und 40320 beanspruchten jeweils ungefähr 40 % eines logischen Prozessors. Ihre zusammengefasste Auswertung enthält 50.661 Samples. Der bekannte Hotspot RVA `0x797bf6` in der Suche nach `MovementMessageNewPath` bleibt der größte einzelne exklusive Treffer: **9.002 Samples, 17,77 %**. Die zugehörige Aufrufstelle RVA `0x673d6d` erreicht 21,39 % inklusive. Im vorherigen stark ruckelnden Durchlauf waren es am Hotspot 71,46 % exklusiv. Ein gleicher NPC-Zähler allein erzeugt somit noch keinen gleichen CPU-Engpass.

## Sichtbarkeitsprüfung unter Last

Sitzung: `measurements/live-culling/session-38040-46033843/`.

| Merkmal | Ergebnis |
|---|---:|
| Aufnahmezustand | `capture-complete` |
| Aufzeichnungsversuche | 100.000 |
| Gespeicherte und verglichene Datensätze | **68.889** |
| Wegen belegter/gefüllter Diagnose-Queue verworfen | 31.111 |
| Sichtbarkeits-, Masken-, Serial/Parallel-Abweichungen | **jeweils 0** |
| Nicht unterstützte gespeicherte Datensätze | 0 |
| Aufrufende Engine-Threads | 6 |

Ein unabhängiger Replay bestätigt 68.889 Vergleiche ohne Abweichungen. Snapshot-Datei: 8.817.872 Bytes; SHA256 `03DE54DCCA3AC5FBE17A85D7A4298D150374BAD7CBB3825D2A26F1AB580237C2`. Das Plugin meldet die Wiederherstellung der ursprünglichen Vtable-Slots. Verworfene Datensätze sind Messverluste der begrenzten Diagnose-Queue; die Originalfunktion und ihre Entscheidungen blieben maßgeblich. Dies ist ein Korrektheitsnachweis für die gespeicherten Daten und kein Beschleunigungsbenchmark.

## Weiterer Ansatz

SKSE und die Engine-Anbindung funktionieren auch in der gemeldeten Stressszene. Es wurde weiterhin kein Engine-Fix eingebaut. Die Bewegungsnachrichten-Suche bleibt ein konkreter CPU-Kandidat, dessen Aufwand stark mit dem Szenenzustand variiert. Für einen Optimierungsprototyp sind Aufrufzahlen, Listenlängen und Änderungen unter den bestehenden Locks zu messen; diese Instrumentierung ist noch nicht implementiert. Ein belastbarer Leistungsnachweis erfordert anschließend reproduzierbare A/B-Durchläufe derselben Szene, mit unveränderten Grafikbedingungen.

Belege: `quality-check.json`, `capture-state.json`, `frame-summary.json`, `cpu-summary.json`, `thread-origins.json`, `hot-thread-symbols.html/.json`, `six-workers-symbols.html/.json` sowie ETL/CSV im Baseline-Verzeichnis; `startup-verification.json`, `plugin.log`, `summary.json`, `snapshots.bin` und `replay.json` in der Plugin-Sitzung. Die exklusiven Stack-Summen wurden geprüft. TESV.exe und nvwgf2um.dll sind interne Symbolreport-Namen der in den Metadaten bestätigten x64-Module.
