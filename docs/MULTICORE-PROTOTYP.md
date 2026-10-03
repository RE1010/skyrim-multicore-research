# Multicore-Prototyp: parallele Sichtbarkeitsprüfung

Stand: 2. Oktober 2026. Das Nutzerziel ist ausdrücklich **Spiel-/Renderarbeit auf mehrere CPU-Kerne verteilen**. Der Framerate-Limiter aus der Baseline ist ein Messbefund und nicht das Entwicklungsziel.

## Implementiert und ausgeführt

Ein nativer C++20-Rechenkern ist implementiert und im Release-Modus gebaut:

- `include/multicore/visibility.hpp`: API mit numerischen Objektgrenzen und Frustum-Ebenen.
- `src/visibility.cpp`: serielle Referenz und dauerhafte Worker, disjunkte Ausgabebereiche, synchroner Abschluss pro Auftrag.
- `tests/visibility_tests.cpp`: Korrektheit, Grenzfälle und Nebenläufigkeit.
- `benchmarks/visibility_benchmark.cpp`: synthetischer Vergleich bei wechselnder Messreihenfolge.

Die parallele Version verteilt Kugel-/Frustum-Tests auf mehrere native Threads. Die Ergebnisflags entsprechen derselben Eingabereihenfolge. Der aufrufende Thread wartet auf den abgeschlossenen Auftrag; Worker greifen ausschließlich auf unveränderliche Zahlen und getrennte Ergebnisbytes zu. Es gibt keine Engine-Aufrufe oder lebenden Engine-Pointer in diesem Kern. Frustum-Ebenen dürfen unnormalisiert sein; aktive Ebenen werden berücksichtigt. Ungültige Objektgrenzen werden vorsichtshalber nicht aussortiert, ungültige aktive Ebenen abgewiesen.

Die Zahl der Worker ist konfigurierbar. Kleine Aufträge fallen standardmäßig unter 4096 Objekten auf die serielle Variante zurück. Diese Schwelle ist vorläufig und muss mit tatsächlichen Szenendaten bestimmt werden. Aufträge verschiedener Aufrufer werden vollständig nacheinander abgearbeitet. Parallelität besteht innerhalb eines Auftrags; es gibt noch keine Pipeline über mehrere Frames.

**Der Rechenkern besitzt inzwischen einen SKSE-Diagnose-Adapter.** Neben dem ursprünglichen konservativen Labormodell gibt es einen anhand der lokalen Originalfunktion geprüften Runtime-Modus. Echte numerische Engine-Daten wurden erfasst und auf vier Workern verglichen. Die Culling-Ausgabe im Spiel wird weiterhin vom Original bestimmt. Einzelheiten und Live-Ergebnisse stehen in [Engine-Anbindung](ENGINE-ANBINDUNG.md).

## Lokaler Labortest

Compiler: MSVC 19.51.36252, x64 Release. Alle Korrektheitstests bestehen: innen/außen, Tangente, nicht normierte Ebenen, aktive Ebenen, ungültige Daten, leere und ungerade große Aufträge, wiederholte Aufträge mit 0/1/2/4/8 Workern, zwei gleichzeitige Aufrufer, serieller Rückfall und falsche Ausgabegröße.

Benchmark: deterministische künstliche Kugeln, zehn Aufwärmrunden und 100 Messungen pro Kombination; 0/2/4/6 Worker und jeweils mit/ohne Kopieren. Die Reihenfolge wechselt pro Runde. Jede Ausgabe wird nach der Zeitmessung vollständig gegen die serielle Referenz verglichen. Die Worker bleiben zwischen Aufträgen bestehen. Der Test erzwingt Parallelität auch bei kleinen Aufträgen, um deren Nachteile sichtbar zu machen.

Folgende Zahlen stammen aus `measurements/multicore-lab-001/timings-interleaved.csv`. Beide Varianten schließen das Kopieren eines bereits zusammenhängenden numerischen Snapshots ein:

| Künstliche Objektgrenzen | Seriell, Median | 4 Worker, Median | Verhältnis seriell/parallel |
|---|---:|---:|---:|
| 1.024 | 0,0107 ms | 0,0238 ms | 0,45×; parallel langsamer |
| 16.384 | 0,3057 ms | 0,1243 ms | 2,46× |
| 65.536 | 1,2726 ms | 0,4359 ms | 2,92× |
| 262.144 | 5,1807 ms | 1,6777 ms | 3,09× |

Bei 65.536 Grenzen beträgt P95 seriell 1,5418 ms und parallel 0,6063 ms. Dies ist eine einzelne lokale Laborserie, kein statistisch abgesicherter Skyrim-Benchmark. Unbekannt sind die tatsächlich geeignete Objektzahl und der Anteil dieser Rechnung am Frame-Pfad. Insbesondere sind Scene-Graph-Traversierung, Engine-Snapshot-Erstellung, Objektlebensdauer, Engine-Ergebnisübergabe und Rendering nicht enthalten. Es gibt keine daraus ableitbare Skyrim-FPS-Steigerung.

## Warum dieser Kandidat und was noch geprüft werden muss

Die geprüften CommonLib-Header beschreiben Kugelgrenzen (`NiBound`) und sechs Frustum-Ebenen mit Aktivmaske. Der Header für `BSCullingProcess` zeigt bereits Culling-Jobs sowie Kontext mit Kamera, Compound Frustum, Portalen, Akkumulatoren und einer Queue. Daraus folgt: Eine reine Parallelisierung von Kugeltests ist als Rechenkern sinnvoll prüfbar, aber **noch kein nachgewiesener fehlender Engine-Worker**. Der bestehende Culling-Pfad könnte die Arbeit bereits parallel ausführen. [NiBound](https://github.com/alandtse/CommonLibSSE-NG/blob/ng/include/RE/N/NiBound.h), [NiFrustumPlanes](https://github.com/alandtse/CommonLibSSE-NG/blob/ng/include/RE/N/NiFrustumPlanes.h), [BSCullingProcess](https://github.com/alandtse/CommonLibSSE-NG/blob/ng/include/RE/B/BSCullingProcess.h)

Die Header wurden am 2. Oktober 2026 gelesen; sie sind mutable Quellstände, keine für 1.7.104 verifizierten Struct-Layouts oder Hook-Adressen. Die vorherige Weißlauf-Aufnahme weist einen Skyrim-internen Worker nach, benennt dessen Aufgaben aber noch nicht.

Für die Integration arbeiten wir in dieser Reihenfolge:

1. **Engine-Pfad und Thread-Rolle verifizieren:** Für die exakt gehashte Runtime 1.7.104 den Grenz-/Frustum-Test sowie seine Aufrufer rekonstruieren. Seine Kosten getrennt von Warteschleifen messen. Wird er bereits effizient auf Workern ausgeführt oder ist sein Anteil zu klein, einen anderen seriellen Teil der Render-Vorbereitung wählen.
2. **Snapshot am richtigen Punkt gewinnen:** Objektgrenzen, Ebenen, Aktivmasken, Pass-Identität und Zuordnung zu lebenden Objekten an einer bestätigten Engine-Grenze kopieren. Keine Übernahme historischer Offsets. Die Objektlebensdauer bleibt beim Engine-Thread gesichert. Alle Daten gehören zu demselben Frame und Pass.
3. **Zunächst nur Ergebnisvergleich:** Bestehende Engine-Ausgabe unverändert lassen; unsere serielle und parallele Ausgabe vergleichen. Ebenenvorzeichen, Rundung, Tangenten und spezielle Culling-Modi müssen der Engine entsprechen. Portale, Compound Frustums, Occlusion, Schatten und dynamische Bounds lassen sich nicht einfach durch sechs Kameraebenen ersetzen.
4. **Abgegrenzten Teilpfad ersetzen:** Erst bei bestätigter Gleichheit geeignete Tests auslagern. Ergebnisflags in ursprünglicher Reihenfolge an der bestätigten Engine-Grenze verwenden. Für unpassende Modi oder nicht verifizierte Runtime den Originalpfad ausführen.
5. **Gesamten Frame vergleichen:** Original, neuer serieller Pfad, neuer paralleler Pfad; gleiche Szene, gleiche Grafik. Snapshot, Worker-Warten und Ergebnisübergabe vollständig mitmessen. Mehr CPU-Auslastung allein ist kein Erfolg. Danach Zellwechsel, Kämpfe, Save/Load und längere Sitzungen prüfen.

SKSE 2.3.1 für Steam 1.7.104 wurde installiert. Unser Diagnose-Adapter nutzt exakt geprüfte lokale RTTI-, Code- und Vtable-Daten und wird von SKSE erfolgreich geladen. Die Anbindung ersetzt noch keine Engine-Arbeit. [Offizielle SKSE-Seite](https://skse.silverlock.org/)

## Lokal bauen und prüfen

Auf diesem Rechner findet `tools/Build-Multicore.cmd` Visual Studio über vswhere, nutzt das vorhandene MSVC-14.51-Toolset und Ninja und erzeugt `build/lab/`. Der MSBuild-Generator scheiterte zuvor an einer Path/PATH-Kollision in der Build-Umgebung; Ninja kompiliert denselben Quellcode erfolgreich. Es wurde kein Compiler nachinstalliert.

```powershell
.\tools\Build-Multicore.cmd
ctest --test-dir build/lab --output-on-failure
.\build\lab\visibility_benchmark.exe
```

Der Laborbenchmark schreibt CSV auf stdout. Eine neue Ausgabe in einen neuen Messordner speichern, um die vorhandenen Belege zu bewahren. Tests und Benchmark laufen außerhalb von Skyrim; die Plugin-DLL wird gesondert über `Install-Integration.ps1` installiert. Der Build kopiert nichts in den Spielordner.
