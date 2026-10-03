# Erste uncapped serielle Referenz

3. Oktober 2026. SkyrimSE.exe 1.7.104.0 mit SKSE 2.3.1. `UncappedBenchmark` ist installiert, von SKSE als korrekt geladen bestätigt und im geladenen Spiel aktiv. Die vorherige 60-FPS-Messung konnte durch die Engine-Warteschleife keinen kleinen Multicore-Gewinn zeigen.

**Resultat: 236,04 Presents/s in der bestätigten normalen Szene. Dies ist Uncapping, keine Engine-Parallelisierung.**

## Messung und Qualität

Sitzung `measurements/20261003-004542-543-baseline`, PID 42516. Der Nutzer bestätigte eine bereite Szene; der genaue Ort wurde nicht genannt. Aufgezeichnet wurden 15 Sekunden Frames; der gesamte ETL-Zeitraum beträgt etwa 16,70 Sekunden. Es gab keine neuen NPC-Anweisungen. Eine absolut identische Sicht zur früheren Aufnahme kann daraus nicht behauptet werden.

| Messgröße | Wert |
|---|---:|
| Gültige Frames, eine Swapchain | 3.535 |
| Mittlere Presentrate | **236,04/s** |
| Frameintervall Mittel / Median | 4,2365 / 4,1019 ms |
| Frameintervall P95 / P99 | 5,4515 / 6,9122 ms |
| PresentMon CPU Busy, Mittel | 3,7951 ms |
| PresentMon GPU Busy, Mittel | **4,0182 ms** |
| SyncInterval | durchgehend 0 |
| Uncapped-Zustandsbeobachtungen | 16, sämtlich aktiv |
| Gemeldete ungültige Physikzeiten | 0 |
| Verlorene ETW-Ereignisse / Puffer | **0 / 0** |

Die Plugin-Zähler steigen während der Aufnahme; es wurde nicht nur ein alter Bericht gelesen. Diese Beobachtungen ersetzen keine Prüfung jedes Physikframes oder der vollständigen Havok-Simulation. [Anpassung und Grenzen](../docs/UNCAPPED-BENCHMARK.md)

## CPU-Verteilung

Der zuerst gestartete Prozessthread ist TID 24032; sein Start liegt unmittelbar am Prozessstart. Auf ihm liegen die Hauptframe- und Render-Aufrufstapel. Er benötigt etwa **85,67 % eines logischen Prozessors** im ETL-Zeitraum. Der Prozess insgesamt benötigt etwa **3,64 logische Prozessoren**. Das sind Laufzeiten, keine kapazitätsbereinigten P-/E-Core-Werte.

TID 29640 mit 57,40 % zeigt im gesonderten Stackbericht NVIDIA-Treiberarbeit. Er wird deshalb nicht aufgrund seiner hohen CPU-Zeit als Skyrim-Engine-Worker bezeichnet. Sechs weitere stark aktive Threads liegen bei je etwa 34,6–35,0 %. Die vorhandene Parallelität erklärt, weshalb eine höhere gesamte CPU-Auslastung allein keinen Fortschritt belegt.

14.133 Main-Thread-Stichproben sind gegen Modul- und Exklusivsumme geprüft. Der untersuchte Renderpaket-CALL, Rückkehradresse `0x15601d4`, umfasst **4.581 inklusive Stichproben, 32,4135 %**. Die direkte Exklusivzeit der Funktion `0x1560340..0x15606a1` ist **4,4010 %**. Der Rest des inklusiven Anteils liegt in ihren Unteraufrufen. `SleepEx` liegt nur noch bei 4,37 % inklusive; `RtlQueryPerformanceCounter` bei 1,76 % exklusiv. Die vorherige dominierende Limiter-Warteschleife ist damit entfernt.

D3D11 benötigt 13,99 % der exklusiven Main-Thread-Stichproben, der NVIDIA-Treiber 9,15 %. Sichtbare D3D-Funktionen umfassen Index-/Vertexbuffer-Bindung, Shaderressourcen und `Map`. Das stützt den nächsten Fokus auf Render-Vorbereitung und geordnete Befehlsaufzeichnung.

Die GPU-Zeit liegt zugleich nahe dem Frameintervall. Daraus folgt kein alleiniger GPU-Engpass; ohne kritischen Zeitpfad wäre eine präzise Klassifikation verfrüht. Diese normale Szene ist aber kein Beleg dafür, dass ein CPU-Patch einen großen FPS-Sprung erzeugen könnte. Weniger Main-Thread-Arbeit und CPU-limitierte zusätzliche Szenen müssen separat beurteilt werden.

## Nächster konkreter Render-Eingriff

Die Offline-Prüfung wurde anhand der neuen Hotspots erweitert:

- `0x1560340` enthält Shader-/Material-Globals und Engine-TLS-Zugriffe. Die komplette Originalfunktion ist keine freigegebene Worker-Aufgabe.
- Caller `0x155ff40` läuft über eine Paketliste, deren Next-Zeiger bei `+0x30` liegt. Er bereitet gemeinsame Zustände vor und kann nach der Schleife die Liste verändern. Eine spätere Worker-Dereferenzierung der bloßen Zeiger wäre daher keine eigene Datenhaltung.
- Geometrie-Vorbereitung `0x1562270` endet in Shader-Virtualdispatch und nutzt weitere Renderer-Globals. Auch diese Originalfunktion ist nicht isoliert.
- Der Hotspot `0x15791cd` liegt in `0x1579120..0x1579231`: zwei Hash-Lookups und anschließende Bindungen am globalen Renderer `0x3331f40`. Eine geeignete Aufteilung müsste reine Lookup-/Vorbereitungsdaten vom serialisierten Binden trennen.
- `0x155f050` beginnt einen Draw-Dispatch mit geometrieabhängigen Zweigen. Die `.pdata`-Einträge decken hier mehrere Teilbereiche ab; der erste Eintrag allein ist ausdrücklich kein vollständiger Funktionsschutz.

**Implementierungsziel:** Eigene Pakete übernehmen die benötigten Zustände und halten Ressourcen für die gesamte Worker-Aufzeichnung am Leben. Worker bereiten diese Pakete mit eigenen Zuständen und eigenen Deferred Contexts vor. Der Main Thread führt fertiggestellte Command Lists an einer geprüften Passgrenze in Originalreihenfolge aus. Die Ressourcensicherung muss auch wiederverwendete beziehungsweise beschriebene Konstantenbuffer umfassen; `AddRef` allein macht deren Inhalt nicht unveränderlich.

Der vorhandene eigenständige D3D11-Backend liefert geordnete parallele Aufzeichnung und eine Bildprüfung. Seine Engine-Ressourcenübernahme und die Passgrenze sind noch offen. Diese Aufnahme ist jetzt die serielle uncapped Referenz für den nächsten tatsächlich ersetzten Engine-Pfad. Es wurde in diesem Schritt keine Skyrim-Renderarbeit auf zusätzliche Worker verschoben.

## Reproduzierbarkeit

`tools/Analyze-UncappedBaseline.py` prüft Frame-/Prozessidentität, ETW-Qualität, CPU-Summen, steigende Plugin-Zähler und Uncapped-Zustände. Bericht: `uncapped-analysis.json`. Frame-CSV-SHA-256: `5A305CF949CA2A3AB9EEF491C54959A18A5BEB073A0EFD8DB7E1BC42D90369B8`.

Installation: `measurements/uncap-installation-20261003-004305-551/manifest.json`. DLL-SHA-256: `C5AB9B56D5BC83E5CA765DE0A47CE01DF9AACF936BAAE3081632A4811B20E027`. Plugin-Sitzung: `measurements/live-uncap/session-42516-55870437`. Alle 13 CTests bestehen, einschließlich 24.000 isolierter Aufrufe der Original-Physikbudget-Funktion und der Win64-Weiterleitungstests.
