# Renderpakete mit eigenen Zuständen und Konstantendaten

3. Oktober 2026. Der bisherige D3D11-Test hatte einen festen Pipelinezustand und unveränderliche Indexdaten. Der neue wiederverwendbare Backend übernimmt wechselnde Zustände und CPU-Konstanten pro Draw. Er zeichnet dieselbe Paketfolge seriell oder auf 1/2/4/6 Workern auf. **Es handelt sich um einen implementierten und getesteten Backend-Baustein; Skyrim liefert ihm noch keine Renderpakete. Ein Spiel-FPS-Gewinn ist nicht nachgewiesen.**

## Implementierung

API: `include/skyrim_mc/render_packets.hpp`, Implementierung: `src/render_packets.cpp`, Hardwaretest: `tests/render_packets.cpp`.

- `Batch::freeze` kopiert die Konstanten in einen zusammenhängenden eigenen Speicherblock. Draws enthalten Offsets statt Verweisen auf veränderbare Producerdaten. Ressourcen werden innerhalb des Batches anhand ihrer vollständigen Kombination zusammengefasst; im Test entstehen sechs Ressourcensätze für bis zu 3.072 Draws. Die COM-Referenzen werden pro Ressourcensatz gehalten, nicht für jeden Draw wiederholt.
- Der aktuelle unterstützte Pfad umfasst statische Indexed-Draws mit Vertex-/Pixelshadern, Inputlayout, einer Texture2D/Sampler-Bindung, Konstanten in VS/PS-Slot 0, Raster-/Blend-/Depth-Zustand, Viewport und Scissor. Geometrie und Texturen müssen `D3D11_USAGE_IMMUTABLE` verwenden. Schreibbare Eingaben werden abgewiesen; deren Referenzzählung garantiert keine unveränderlichen Inhalte.
- Jeder Worker besitzt seinen Deferred Context und eigene dynamische Konstantenbuffer. Die kopierten Bytes werden mit `WRITE_DISCARD` hochgeladen. Es gibt keine parallelen Zugriffe auf den Immediate Context und keine Worker-Dereferenzierung von Engine-Paketzeigern.
- Alle Worker zeichnen zusammenhängende Abschnitte der Originalfolge auf. Erst wenn sämtliche Command Lists erfolgreich fertiggestellt sind, führt der Besitzerthread sie in Originalreihenfolge aus. Aufnahmefehler führen vor der Wiedergabe zum Abbruch; teilweise aufgezeichnete Listen werden verworfen. Die Fehlerbehandlung ist implementiert, ein echter Hardware-Geräteverlust wurde nicht künstlich ausgelöst.
- `ExecuteCommandList(TRUE)` stellt den vorherigen D3D-Kontextzustand nach jeder Liste wieder her. Dies schützt nachfolgenden seriellen Code vor den Backend-Bindungen. Die Zustandswiederherstellung ersetzt keine Prüfung der Engine-eigenen Caches und Passgrenzen.

Die Thread-/Map-Regeln entsprechen der [D3D11-Dokumentation zu Deferred Contexts](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render). Die Semantik der Wiederherstellung ist in [ExecuteCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist) beschrieben.

## Korrektheit

Der Debug-Hardwarelauf prüft **33 vollständige RGBA-Bilder**, jeweils 256 × 256 Pixel, bytegenau gegen eine unabhängige CPU-Referenz. Der Workload verändert Konstanten, Shader, Texturen, Farb-Schreibmasken und Scissor-Rechtecke. Mehrere Lagen überschreiben dieselben Pixel; veränderte Wiedergabereihenfolge oder versehentlich gemeinsam genutzte Zustände können nicht durch gleichfarbige Draws verborgen werden.

Nach der Übernahme werden Producer-Konstanten überschrieben und Source-Pakete zerstört. Ein separater Lebensdauertest gibt sämtliche ursprünglichen Shader-/Geometrie-/Material-/RTV-Referenzen frei. Der Batch liefert weiter die richtige Ausgabe. Tests prüfen außerdem die Wiederherstellung von Viewport, Pixelshader und Shaderressource, unterschiedliche Worker-TIDs sowie die Ablehnung eines Submit von einem fremden Thread, schreibbarer Geometrie/Texturen, ungültiger Konstantengrößen und Indexbereiche.

Ergebnis: **keine Bildabweichung, keine gespeicherte Debugmeldung**. Alle **14 CTests bestehen**. Zwei Release-Messläufe prüfen weitere **720 vollständige Bilder**. Insgesamt liefert der letzte dokumentierte Hardwarelauf **753 fehlerfreie Bildprüfungen**, zusätzlich zur CTest-Ausführung.

## Gesamtkosten statt reiner Worker-Zeit

Die Release-Messung umfasst zwölf gemessene Durchgänge nach drei Aufwärmdurchgängen je Konfiguration. Drei Varianten werden in wechselnder Reihenfolge ausgeführt:

1. Serielle Übernahme, Uploads und Draws über denselben neuen Backend.
2. Übernahme, Worker-Dispatch, Aufzeichnung, Join und geordnete Wiedergabe mit Zustandswiederherstellung.
3. Serielle Draws aus bereits übernommenen Daten, ohne erneute Kopier-/Validierungskosten.

GPU-Readback und Erzeugung der Producer-Testdaten liegen außerhalb der CPU-Zeitmessung; die Bilder jeder Variante werden danach geprüft. Die gemessene CPU-Abgabezeit ist daher keine GPU-Framezeit und keine Skyrim-FPS-Messung.

| Lauf, 3.072 Draws | Worker | Serie mit Übernahme | Serie bereits übernommen | Parallel mit Übernahme |
|---|---:|---:|---:|---:|
| 1 | 4 | 0,9227 ms | 0,6483 ms | 0,5713 ms |
| 2 | 4 | 0,6276 ms | 0,4404 ms | 0,6044 ms |
| 1 | 6 | 0,7330 ms | 0,4783 ms | 0,5269 ms |
| 2 | 6 | 0,7593 ms | 0,4815 ms | 0,5464 ms |

Die Main-Thread-Zyklen sinken bei diesen großen Paketen gegenüber dem seriellen Pfad **mit gleicher Übernahme** um etwa **63–69 %**. Das belegt die Verlagerung der Backend-Arbeit auf Worker. Die verstrichene Gesamtzeit verbessert sich gegenüber dieser Variante ebenfalls. Der strengere Vergleich mit der bereits vorhandenen seriellen Datenbasis ist aber wechselhaft und meist langsamer. **Damit ist noch kein robuster Vorteil gegenüber einem Engine-Pfad belegt, der diese zusätzliche Übernahme nicht benötigt.** Kleine Pakete zeigen ebenfalls keinen zuverlässigen Gesamtzeitgewinn. Mehr Threads allein helfen hier nicht.

Die Übernahme kostet im großen Paket etwa 0,16–0,18 ms. Frühere Versionen mit Referenzhaltung pro Draw lagen bei etwa 0,8–0,9 ms; die Ressourcen-Zusammenfassung und der zusammenhängende Datenspeicher sind deshalb Teil der finalen Implementierung. Die Läufe zeigen deutlich messbare Schwankungen; daraus werden keine präzisen Spiel-Speedups extrapoliert.

## Offene Engine-Anbindung

Der kommende Adapter muss an der geprüften Renderpass-Grenze die tatsächlichen Konstantenbytes und Ressourcen übernehmen und dort Originalarbeit ersetzen. Die bekannten Render-Paketzeiger und ihre globalen Shader-/Material-/TLS-Zustände sind dafür keine bereits freigegebenen Worker-Eingaben. Die bisherige Live-Diagnose liefert Tokens, keine vollständigen Konstantenbytes oder bewiesenen Ressourcenbesitz.

Zusätzlich benötigt der Adapter Unterstützung beziehungsweise geprüfte Lebensdauerregeln für die in Skyrim tatsächlich vorkommenden Ressourcen. Der neue Backend lehnt `DEFAULT`/`DYNAMIC`-Geometrie und -Texturen zunächst ab. Ob ein bestimmter Skyrim-Pass überhaupt ausreichend viele geeignete Ressourcen besitzt, ist noch nicht gemessen. Instancing, Skinning, zusätzliche Shaderstufen/Slots, UAVs, Queries, indirekte Draws und weitere Engine-Zustände sind nicht durch diesen begrenzten Prototyp abgedeckt. Solche Pakete brauchen einen geprüften Originalpfad.

Vor einer aktivierten Spiel-Parallelisierung müssen Übergabe und Synchronisation gegenüber der **uncapped Originalengine** tatsächlich günstiger werden. Die vorliegende Implementierung löst Zustandsbesitz, Konstantenübernahme und geordnete parallele Aufzeichnung im eigenen Backend; sie ersetzt noch keine Skyrim-Funktion.

## Berichte

Letzter Lauf: `measurements/20261003-011131-465-owned-render-benchmark/`. Dateien: `manifest.json`, `debug-correctness.json`, `benchmark-1.json`, `benchmark-2.json`, `ctest.log`. Das Manifest hält den getesteten EXE-Hash und die Berichtshashes fest. Reproduktion: `tools/Build-Multicore.cmd`, danach bei regulär geschlossenem Skyrim `tools/Test-OwnedRenderPackets.ps1`.
