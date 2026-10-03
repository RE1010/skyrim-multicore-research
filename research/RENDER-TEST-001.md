# Renderer-Test: Hardwarewiederholung und normale Spielszene

2. Oktober 2026. Der Benutzer hat den Test angefordert und eine normale Szene ohne zusätzliche gespawnte Wachen bestätigt. Die genaue Position und Blickrichtung wurden nicht angegeben; diese Aufnahme ist deshalb keine reproduzierte A/B-Szene. Der neue Renderer läuft als separater Hardwaretest. **Im Spiel wurde weiterhin keine Renderfunktion durch einen parallelen Renderer ersetzt.**

## Wiederholung des Hardwaretests

`tools/Test-RenderBackend.ps1 -ReleaseRuns 3` führte einen Debuglauf und drei Läufe ohne Debugschicht aus. Alle vier Läufe bestehen je 372 Bildprüfungen und zwölf Kombinationen von Workerzahl und Drawzahl: **1.488 Bildprüfungen, null Abweichungen, null Debugmeldungen**. Der Test lief vor dem Start von Skyrim.

Verzeichnis: `measurements/20261002-235344-098-render-backend/`. `manifest.json` enthält den Executable-Hash, die Berichts-Hashes und den Abschlussstatus; `aggregate.json` enthält alle drei Releasewerte pro Konfiguration.

| Draws | Worker | Verhältnis serieller zu paralleler CPU-Abgabezeit, Bereich der drei Läufe | Einordnung |
|---:|---:|---:|---|
| 4.096 | 4 | 0,362–0,411 | Parallel deutlich langsamer. |
| 16.384 | 4 | 0,990–1,010 | Kein konsistenter Zeitgewinn, obwohl Main-Thread-Zyklen rund 67 % niedriger sind. |
| 32.768 | 4 | 1,350–1,446 | Größtes Paket profitiert; Main-Thread-Zyklen rund 81 % niedriger. |
| 32.768 | 6 | 1,538–1,604 | Größtes Paket profitiert auch mit sechs Workern. |

Ein Verhältnis über 1 bedeutet, dass der gemessene parallele Abschnitt kürzer ist. Das Verhältnis ist kein Skyrim-FPS-Faktor. Dispatch, Worker-Warten und geordnete Command-List-Ausführung sind enthalten; GPU-Readback und Bildprüfung liegen außerhalb. Der Test verwendet eigene unveränderliche Ressourcen und eine einfache Pipeline. Die Werte bestätigen die Paketgrößenabhängigkeit des [ersten Labortests](D3D11-PARALLEL-LAB.md).

## Spielaufnahme

Anschließend startete Skyrim über das bereits installierte SKSE. Prozess 42520, Spielversion 1.7.104.0. Die vorhandene Bewegungsdiagnose blieb im Zustand `waiting-for-trigger`; ihr zusätzlicher Vergleichsscan wurde nicht gestartet. Ihre Originalweiterleitung bleibt Bestandteil der Installation. Kein neuer Renderer-Hook wurde installiert.

Verzeichnis: `measurements/20261002-235519-156-baseline/`. ETL-Dauer 30,8079 Sekunden, 30 Sekunden angeforderte PresentMon-Aufnahme. **Keine verlorenen ETW-Ereignisse oder Puffer**, erfolgreiche Recorder-Abschlüsse, eine Swap Chain, 1.814 gültige Present-Intervalle. Der früheste Prozess-Thread 37796 startet 7,3 Mikrosekunden nach dem Prozess; seine Frame-/Render-/Limiter-Stacks stützen die Zuordnung zum Main Thread.

| Messwert | Ergebnis |
|---|---:|
| Mittlere Present-Rate | 60,5478/s |
| Mittleres Present-Intervall | 16,5159 ms |
| Median / P95 / P99 | 16,5102 / 16,5573 / 16,6145 ms |
| Mittlere GPU-Busy-Zeit | 3,8923 ms |
| Main-Thread-Laufzeit relativ zu einem logischen Prozessor | 97,6681 % |
| Sechs vorhandene Engine-Worker, jeweils | ungefähr 9,75–9,87 % |
| Gesamte Prozess-Laufzeit in logischen Prozessoräquivalenten | 1,7877 |

Die Laufzeitwerte beziehen sich auf das vollständige ETL-Intervall. GPU-Busy-Zeit und Present-Intervalle sind PresentMon-Mittelwerte, keine direkte Zuordnung jedes CPU-Stacks zum kritischen Frame-Pfad. Eine Present-Rate ist nicht automatisch die Zahl einzigartiger angezeigter Bilder.

## Ursache der auffälligen Main-Thread-Auslastung

Die geprüfte Main-Thread-Auswertung enthält 29.824 CPU-Samples:

- `Kernelbase!SleepEx`: 17.986 inklusive Samples, **60,31 %**.
- Bekannte Limiter-Aufrufstelle RVA `0x1009fc8`: 18.061 inklusive Samples, **60,56 %**.
- `ntdll!RtlQueryPerformanceCounter`: 3.626 exklusive Samples, **12,16 %**.
- Render-Aufrufstelle RVA `0x15601d4`: 2.368 inklusive Samples, **7,94 %**.
- Renderfunktion RVA `0x1560340..0x15606a1`: 283 exklusive Samples, **0,95 %**.
- Job-Warte-Aufrufstelle RVA `0xebc10d`: 436 inklusive Samples, **1,46 %**.

Verschachtelte Anteile dürfen nicht addiert werden. Insbesondere sind die verschiedenen Limiter-/Sleep-/Kernel-Anteile keine unabhängigen Kosten. CPU-Sampleanteile messen laufende Arbeit, nicht blockierte Wartezeit oder garantierte Einsparungen.

Dieser Befund passt zum zuvor am gleichen Build geprüften Engine-Limiter: wiederholte Zeitabfrage und `Sleep(0)` bis zu einer Grenze von ungefähr 16,3934 ms vor Present. Dessen Codeanalyse steht in [BASELINE-002](BASELINE-002.md). Die hohe Main-Thread-Laufzeit in dieser normalen Szene wird deshalb wesentlich von der Framerate-Warteschleife bestimmt. Sie ist kein Beleg dafür, dass die eigentliche Render-Vorbereitung einen Kern auslastet. GPU-Busy von knapp 3,9 ms und nahezu konstante Present-Intervalle stützen diese Einordnung.

Die sechs zusammengefassten Worker besitzen 18.023 konsistent geprüfte Samples und enthalten die bekannten Engine-Job-Aufrufpfade bei RVA `0xebab9a`, `0xebc811` und `0xebc248`. Sie sind in dieser Szene nicht wie im 600-Wachen-Stresstest ausgelastet.

## Konsequenz

Der unabhängige Renderbackend besteht die Wiederholung. Die normale Spielszene eignet sich als Ausgangsmessung, zeigt aber bei der aktuellen Begrenzung keine überlastete serielle Render-Vorbereitung. Sie liefert keinen FPS-Nachweis für die neue Parallelisierung, weil diese noch nicht im Spiel aktiv ist.

Für die erste Engine-Anbindung brauchen wir weiterhin stabile Renderpakete und getrennte Shader-/Materialzustände. Die Vorbereitungskosten eines abgegrenzten Passes müssen direkt gemessen werden, damit der dominante Limiter den Vergleich nicht verdeckt. Mehr NPCs zu spawnen oder den vorhandenen Limiter zu entfernen würde allein noch keine Render-Parallelisierung testen. [Integrationsgrenzen](MAIN-THREAD-004.md)

Belege der Spielaufnahme: `capture-state.json`, `trace-stats.txt`, `frame-summary.json`, `cpu-summary.json`, `thread-origins.json`, `hot-thread-symbols.html/.json`, `six-workers-symbols.html/.json`, `render-thread-audit.json`, `quality-check.json`, ETL und Frame-CSV. Der Aufnahmebericht bleibt unverändert auf seinem ursprünglichen Stand zur Tracequalität; die nachträgliche Prüfung steht in `quality-check.json` und `cpu-summary.json`.
