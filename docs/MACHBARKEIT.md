# Können wir Skyrims seriellen CPU-Engpass entschärfen?

Recherche und lokale Bestandsaufnahme vom **2. Oktober 2026**.

Diese Analyse dokumentiert den Projektbeginn. SKSE und Diagnose-Plugins wurden inzwischen installiert und getestet; die Ausgangslage unten ist historisch. Die aktuelle Untersuchung umfasst einen getesteten parallelen D3D11-Backend und die noch offenen Engine-Abhängigkeiten: [Main Thread und Renderer](../research/MAIN-THREAD-004.md), [Hardware-Labor](../research/D3D11-PARALLEL-LAB.md).

## Urteil

**Ja, ein begrenzter Engine-Eingriff ist technisch plausibel und einen Versuch wert.** Noch nicht belegt ist, welcher Eingriff in diesem Setup hilft oder wie groß der Gewinn wäre. Ein universeller Patch, der beliebige Engine-Funktionen auf zusätzliche Kerne verteilt, ist kein belastbarer Plan.

Die aussichtsreichste Vorgehensweise: den kritischen Pfad messen, einen teuren Teil mit überprüfbaren Datenabhängigkeiten auswählen, erst unnötige Arbeit entfernen und danach unabhängige Berechnungen parallelisieren. Das Ergebnis muss bei identischer Szene und Bildqualität schneller sein und stabil bleiben. Eine reine FPS-Anzeige belegt das nicht.

## Lokale Ausgangslage

| Befund | Bedeutung / Grenze |
|---|---|
| Steam-SkyrimSE.exe, Dateiversion **1.7.104.0** | Diese konkrete Binärversion ist das Untersuchungsziel. Historische Offsets sind keine geprüften Hook-Adressen. |
| SHA-256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F` | Lokaler Identifikator für spätere Messungen und Disassemblierung; kein Vergleich gegen eine Hersteller-Prüfsumme. |
| Intel Core i5-14400F / NVIDIA RTX 5060 Ti, aus Registry | Hardwarebestand; keine Aussage über Auslastung, GPU-Speichergröße oder aktuelle Taktraten. |
| EXE importiert `D3D11CreateDeviceAndSwapChain` und `CreateDXGIFactory` | Der installierte Build verwendet weiterhin den D3D11/DXGI-Pfad. Mit lokalem Visual-Studio-`dumpbin /imports` geprüft. |
| Keine `skse64*`-Dateien im Spielhauptordner, kein sichtbarer `Data/SKSE/Plugins`-Ordner | In dieser Installation wurde kein SKSE-Pluginbestand gefunden. Eine mögliche virtuelle Modmanager-Umgebung ist nicht vollständig untersucht. |
| Keine `.ess`-Savegames am Standardpfad, Skyrim beim Prüfen nicht gestartet | Noch keine reproduzierbare Benchmark-Szene und keine Laufzeitmessung. |
| INI auf Datenträger: 1920×1080, VSync-Intervall 1, Schattenentfernung 10000, Schattenkarte 4096 | Relevante Ausgangswerte. Modmanager, Laufzeit-Overrides oder Treibereinstellungen können sie überschreiben. |
| WPR, WPA, CMake und Visual-Studio-C++-Werkzeuge vorhanden | CPU-Traces und native Entwicklungsarbeit sind grundsätzlich vorbereitet. PresentMon wurde auf PATH nicht gefunden. |

Die aktive `plugins.txt` war nicht lesbar. Deshalb wird die aktive Modliste ausdrücklich als **unbekannt** geführt. Vollständige Ausgangsdaten stehen in `research/local-setup.json`.

## Was an der Main-Thread-Erklärung stimmt

Skyrim hat mehrere Threads; entscheidend ist trotzdem die seriell verbleibende Arbeit auf dem Pfad bis zum fertigen Frame. Historische Skyrim-Crash-Traces zeigen beispielsweise `BSJobs::JobThread`, und der lokale Build importiert Windows-Thread- und Synchronisationsfunktionen. Das belegt Thread-Infrastruktur, aber noch nicht die Verteilung der Arbeit in unserem Build. [Historischer Skyrim-SE-Trace](https://gist.github.com/rshackleton/4e7dc7f8740628a732e844a552e91047)

Wir müssen mindestens vier Ursachen unterscheiden:

1. **Spielsimulation:** Actor-/AI-Updates, Animation, Physik oder native Script-Aufrufe.
2. **Render-Vorbereitung:** Sichtbarkeit, Pass-Erzeugung, Shader-/Materialzustände, Sortierung und Objektverwaltung.
3. **API/Treiber:** CPU-Kosten der D3D11-Aufrufe und der Ressourcenverwaltung.
4. **Warten:** VSync, FPS-Limit, GPU, I/O oder Synchronisation mit anderen Threads.

Diese Liste ist ein Untersuchungsmodell, keine bereits gemessene Aufteilung Skyrims. Main Thread und Render Thread dürfen nicht ohne Trace gleichgesetzt werden. Eine geringe gesamte CPU-Auslastung identifiziert keinen dieser Engpässe.

**DirectX 11 ist nicht grundsätzlich auf einen Thread beschränkt.** Das Device unterstützt parallele Nutzung; ein einzelner Device Context darf nur von einem Thread gleichzeitig bedient werden. Deferred Contexts erlauben paralleles Aufzeichnen mehrerer Command Lists. Ihre Wiedergabe auf dem Immediate Context bleibt sequenziell. [Microsoft: Threading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro), [Microsoft: Immediate/Deferred Rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render)

Damit ist eine Parallelisierung prinzipiell möglich. Sie bringt aber nichts, wenn wir nur Locks um dieselbe globale Arbeit legen, zu wenig unabhängige Arbeit finden oder Kopieren und Synchronisieren den Gewinn aufzehren. Eine feste universelle Draw-Call-Grenze lässt sich aus den geprüften Quellen nicht ableiten.

## Was existierende Projekte bereits zeigen

**SSE Engine Fixes:** Der geprüfte Quellcode beschleunigt Form-Lookups durch Caches und ersetzt die Suche nach Tree-LOD-Referenzen. Er behandelt auch Cache-Invalidierung. Das zeigt, dass gezieltes Ersetzen von Engine-Arbeit machbar ist; es ist kein Beleg für eine umfassend parallelisierte Engine oder den Gewinn in unserer Spielversion. [Form-Cache](https://github.com/aers/EngineFixesSkyrim64/blob/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c/src/patches/form_caching.h), [Tree-LOD-Cache](https://github.com/aers/EngineFixesSkyrim64/blob/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c/src/patches/tree_lod_reference_caching.cpp)

**Community Shaders:** Die Renderer-Hooks greifen in Shader-Techniken und Geometry-Setup ein. Dabei werden globale Felder wie aktueller Shader und Permutationsdaten verändert. Meine Schlussfolgerung aus diesem Code: Solche Pfade lassen sich nicht unverändert gleichzeitig auf mehreren Threads ausführen. Der globale Zustand muss durch lokale Daten ersetzt oder aus dem parallelen Bereich herausgehalten werden. [Geprüfte Renderer-Hooks](https://github.com/community-shaders/skyrim-community-shaders/blob/7109b526082f392385b30a4258f73418a7fa102e/src/Hooks.cpp)

Das Projekt enthält ein Performance Overlay mit Draw-Call-Auswertung und A/B-Vergleichen. Ein weiterer Profiler misst instrumentierte Render-Pässe über CPU-Zeitintervalle und GPU-Timestamp-Queries. Diese Instrumentierung erfasst nicht automatisch die gesamte Spielsimulation. Wir sollten ihre Messmöglichkeiten verwenden oder daran anknüpfen, sofern die passende Version mit unserem Setup funktioniert. [Overlay](https://github.com/community-shaders/skyrim-community-shaders/blob/7109b526082f392385b30a4258f73418a7fa102e/src/Features/PerformanceOverlay.cpp), [Pass-Profiler](https://github.com/community-shaders/skyrim-community-shaders/blob/7109b526082f392385b30a4258f73418a7fa102e/src/Profiler.cpp)

**DXVK:** Übersetzt D3D11 nach Vulkan. Das ist ein sinnvoller späterer Vergleich, falls API-/Treiberkosten dominieren. Eine Übersetzung ändert nicht automatisch die seriellen Spiel- oder Szenenberechnungen vor den API-Aufrufen. Das Projekt unterstützt Windows offiziell nicht und dokumentiert besondere Kompatibilitätsgrenzen. Deshalb kein Ausgangspunkt für einen allgemeinen Windows-Skyrim-Fix. [DXVK](https://github.com/doitsujin/dxvk), [Windows-Hinweise](https://github.com/doitsujin/dxvk/wiki/Windows)

## Kandidaten und Entscheidung

Die Bewertung ist eine technische Arbeitshypothese; kein Kandidat ist bisher für Skyrim 1.7.104 validiert.

| Ansatz | Wann er sinnvoll ist | Hauptproblem | Priorität |
|---|---|---|---|
| Redundante Lookups, Zustandswechsel und wiederholte Berechnungen entfernen | Trace zeigt teure Wiederholungen | Invalidierung und Mod-Kompatibilität | Hoch, zuerst prüfen |
| Reine Sichtbarkeits-/Sortier-/Pass-Vorbereitung auf kopierten Daten parallelisieren | Wesentlicher CPU-Anteil und unabhängige Datensätze nachgewiesen | Snapshot-Kosten, Objektlebensdauer, globale Zustände | Bevorzugtes Parallelisierungsziel |
| Wiederholte statische Geometrie bündeln oder instanzieren | Viele kompatible Draws desselben Typs | Material-/Shaderunterschiede, Beleuchtung, Transparenz, Schatten | Eigener Renderer-Prototyp nach Messung |
| D3D11 Deferred Contexts für einen abgegrenzten Pass | Command-Aufzeichnung dominiert | State-Rekonstruktion, Map-/Query-Regeln, Treiberverhalten | Zweite Stufe |
| Actor-/AI-Updates parallelisieren | Simulation ist der tatsächliche Engpass | Weltzustand, Events, Reihenfolge und Synchronisation | Deutlich schwieriger |
| Gesamter Renderer in DX12/Vulkan | Engere Ansätze reichen nachweislich nicht | Ressourcen-/Shader-/Pass-Rekonstruktion und breite Mod-Kompatibilität | Langfristiges separates Projekt |

Keine pauschalen INI-Thread-Tweaks als Lösung; keine CPU-Affinität als Ersatz für Parallelisierung. Auf dem Hybridprozessor wären P-/E-Core-Platzierung und Ready-Zeit zusätzliche Messgrößen, falls der Trace dafür Anlass gibt.

## Konkreter erster Engine-Prototyp

**Arbeitstitel: isolierte Render-Vorbereitung mit serieller Referenz.** Die konkrete Funktion wählen wir erst nach dem Trace aus; noch keine Hook-Adresse und kein Struct-Layout erfunden.

1. Einen teuren Kandidaten instrumentieren, seine Aufrufer und Datenzugriffe für die exakt identifizierte EXE rekonstruieren. Ein heißer Funktionsname allein reicht nicht: Wir brauchen den Anteil am kritischen Frame-Pfad.
2. Eine identische serielle Referenzberechnung definieren. Zunächst Ergebnisvergleich, noch keine Ausgabe im Spiel ersetzen.
3. An einer verifizierten Engine-Grenze unveränderliche numerische Eingaben kopieren: beispielsweise Bounds, Transformdaten, Kamera-/Passdaten und stabile IDs. Ein Pointer oder Handle allein garantiert keine sichere Nutzung durch Worker.
4. Worker rechnen ausschließlich auf diesem Snapshot und schreiben in getrennte Ergebnisbereiche. Keine Engine-Aufrufe, keine Mutation lebender Actors oder Scene Nodes.
5. Ergebnisse desselben Frames an einer verifizierten Grenze zusammenführen. Reihenfolge, Ressourcenlebensdauer und Zellwechsel prüfen. Keine asynchrone Verwendung veralteter Sichtbarkeit als stillschweigender Ersatz.
6. Erst bei gleichem Ergebnis und positivem End-to-End-Vergleich den bisherigen Teilpfad ersetzen. Serieller Fallback bei unbekannter Runtime, nicht verifizierbaren Voraussetzungen oder ungültigem Snapshot.

Die Rückgabe an einen sicheren Engine-Punkt darf nicht aus dem Namen einer Task-API abgeleitet werden. Thread-Rolle und Ausführungsphase werden im Zielbuild geprüft.

Der grobe Break-even lautet:

`Snapshot + parallele Berechnung + Warten + Zusammenführen < bisheriger Anteil auf dem kritischen Pfad`

Ein reines Rechenbeispiel: Sind in einem 20-ms-Frame 6 ms geeignete Arbeit enthalten, könnten vier Worker diese idealisiert auf 1,5 ms reduzieren. Bei zusätzlich 1 ms Aufwand ergäben sich 16,5 ms statt 20 ms, ungefähr 61 statt 50 FPS. Das ist **keine Prognose für Skyrim**, sondern verdeutlicht die begrenzende Wirkung des verbleibenden seriellen Anteils. CPU und GPU können außerdem überlappen; deshalb zählt der gemessene kritische Pfad, nicht die Summe beliebiger Profiler-Zeiten.

## Runtime-Kompatibilität

Die offizielle SKSE-Seite nennt **SKSE 2.3.1 für Steam-Runtime 1.7.104**. Die Plugin-API dokumentiert außerdem ein neues Address-Library-V5-Kompatibilitätsflag ab 1.7.99. Damit ist eine Plugin-Basis verfügbar; das bedeutet nicht, dass ein älterer CommonLib-Fork oder jeder Engine-Hook schon kompatibel ist. [SKSE](https://skse.silverlock.org/), [Plugin-API](https://github.com/ianpatt/skse64/blob/master/skse64/PluginAPI.h)

Vor einem Plugin-Build müssen passende Address-Library-Daten, Bibliotheksstand, Funktionssignaturen und Struct-Layouts konkret geprüft werden. Der für diese Recherche betrachtete CommonLibSSE-NG-Stand ist `b93280e832f263dbef44e44cbe2936622a02f91a`; seine Eignung für 1.7.104 ist hier **nicht verifiziert**. Das Spiel wurde nicht heruntergestuft.

## Was jetzt fertig ist und was noch fehlt

**Aktualisierung nach der ersten auswertbaren Spielaufnahme:** `research/BASELINE-002.md` enthält CPU-/Frame-Daten und einen am Code von 1.7.104 bestätigten Framerate-Wartepfad. Die Szene belegt noch keinen Engpass durch serielle Spielarbeit. Die folgenden Angaben beschreiben den ursprünglichen Stand vor den Aufnahmen.

Fertig: Primärquellenprüfung, lokale Versions-/API-Bestandsaufnahme, Kandidatenbewertung, Messplan und vorbereitete Diagnose-Skripte. Quellenstände von Engine Fixes und Community Shaders sind oben festgehalten; mutable Dokumentationsseiten wurden am Recherchedatum geprüft.

Offen: echte Spielszenen, Frames und CPU-/GPU-Traces; Identifikation eines lohnenden Teilpfads; verifizierte Runtime-Anbindung; Prototyp und Langzeittests. **Ohne diese Daten lässt sich weder ein Main-Thread-Fix noch eine FPS-Steigerung behaupten.** Der nächste konkrete Schritt ist die erste reproduzierbare Baseline gemäß `MESSPLAN.md`.
