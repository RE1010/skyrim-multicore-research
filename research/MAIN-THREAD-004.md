# Arbeit vom Main Thread auf Worker verlagern

Untersuchung vom 2. Oktober 2026, Zielbuild SkyrimSE.exe 1.7.104.0. Dies erweitert die NPC-Untersuchung auf den Renderer und die Engine-Abhängigkeiten.

## Ergebnis

Ein allgemeiner Multithreading-Schalter ist im untersuchten Pfad nicht die fehlende Voraussetzung. Skyrim führt bereits Engine-Jobs parallel aus. Sein unveränderter D3D11-Geräteerzeugungspfad setzt auch nicht das Flag `D3D11_CREATE_DEVICE_SINGLETHREADED`. Die aussichtsreiche zusätzliche Arbeit ist deshalb eine gezielte Verlagerung der Render-Vorbereitung und gegebenenfalls der Aufzeichnung von Renderbefehlen auf Worker mit eigenen Zuständen.

Der dafür implementierte D3D11-Laborbackend zeichnet echte Grafikbefehle auf bis zu sechs eigenen Threads auf. Auf der lokalen NVIDIA-Hardware bestehen insgesamt 744 Bildprüfungen in zwei Läufen ohne Abweichung; die D3D11-Debugschicht speichert keine Meldungen. Das ist ein Hardware- und Backend-Nachweis. **Skyrims Renderer wurde dadurch noch nicht parallelisiert, und ein FPS-Gewinn im Spiel ist nicht belegt.** [Laborbericht](D3D11-PARALLEL-LAB.md)

Gleichmäßige CPU-Auslastung ist kein geeignetes Erfolgskriterium. Auch ein wartender Main Thread oder zusätzliche Hintergrundarbeit kann die Auslastung verändern. Relevant sind weniger Arbeit beziehungsweise Wartezeit auf dem kritischen Frame-Pfad und eine kürzere Framezeit bei identischer Ausgabe.

## Was die Spielmessungen unterscheiden

| Aufnahme | Beobachtung | Konsequenz |
|---|---|---|
| [Baseline](BASELINE-002.md) | Rund 61 Presents/s; viel `Sleep(0)`/Zeitabfrage im untersuchten Limiter. | Diese CPU-Last ist kein Beleg für teure serielle Simulation. |
| [600 Wachen, ruckelnde Außenszene](NPC-STRESS-002.md) | 10,96 Presents/s; sechs Engine-Worker stark beschäftigt, Main Thread überwiegend in Job-Warteschleife. GPU-Arbeit deutlich kürzer als der Frame. | Mehr Main-Thread-Parallelisierung allein behebt langsame Worker-Jobs nicht. Redundante Bewegungssuchen sind ein separater Kandidat. |
| [Andere 600-Wachen-Aufnahme über SKSE](NPC-STRESS-003.md) | 29,31 Presents/s; ein Render-Aufrufpfad umfasst 60,48 % der CPU-Stichproben des Main Threads. GPU-Arbeit liegt hier nahe der Framezeit. | Render-Vorbereitung untersuchen, aber einen möglichen GPU-Engpass mitprüfen. Der Unterschied zur vorigen Aufnahme ist kein SKSE-Leistungsnachweis. |

Die neue Auswertung ordnet 2.630 von 16.932 exklusiven Main-Thread-CPU-Stichproben der Funktion RVA `0x1560340..0x15606a1` zu: **15,53 %**. Die 60,48 % des übergeordneten Aufrufpfads enthalten Nachfahren und dürfen nicht dazu addiert werden. Beide Zahlen beschreiben CPU-Stichproben, keine garantierte Einsparung von Framezeit. [Maschinenlesbare Auswertung](integration/render-thread-audit.json)

## D3D11 ist nicht künstlich auf einen Thread beschränkt

`tools/Audit-RenderThread.py` prüft zunächst den SHA-256 der konkreten EXE und ermittelt Import-Aufrufkandidaten sowie Funktionsgrenzen. Der Geräteaufruf wurde anschließend im vollständigen Funktionsdisassembly geprüft:

- Funktion RVA `0x1012060..0x1012478`, Aufruf von `D3D11CreateDeviceAndSwapChain` bei `0x1012320`.
- Das vierte x64-Argument `R9D` erhält 0 oder bedingt 2 (`D3D11_CREATE_DEVICE_DEBUG`).
- Die Flags 1 (`SINGLETHREADED`) und 8 (`PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`) werden an diesem unveränderten Codepfad nicht gesetzt.

Das ist ein statischer Befund am geprüften Build. Er beweist nicht, dass ein fremder Hook die Argumente zur Laufzeit unverändert lässt. [Disassembly](integration/d3d11-device-creation-disassembly.txt), [Microsoft: Geräteflags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag)

Ein threadfähiges Device verteilt vorhandene Engine-Aufrufe nicht automatisch. Für parallele Befehlsaufzeichnung brauchen Worker getrennte Deferred Contexts; die Command Lists werden anschließend auf dem Immediate Context geordnet ausgeführt. Der Immediate Context und `Present` bleiben in unserem Entwurf auf dem Render-/Main-Thread. [Microsoft: Multithreading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro), [Immediate und Deferred Contexts](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render)

## Die eigentliche Integrationshürde: gemeinsame Renderzustände

Der untersuchte serielle Pfad schreibt globale Werte, unter anderem bei RVA `0x36939d8`, `0x36939d4` und `0x36939e0`, und verändert Objektzustand. Die Zuordnung dieser Werte zu Shader, Technik und Material ist eine Interpretation des Codes, noch keine vollständig geprüfte C++-Schnittstelle. [Disassembly des Kandidaten](integration/serial-candidate-1560340-disassembly.txt)

Die separat geprüften Community-Shaders-Quellen zeigen ein konkretes verwandtes Problem: Die Shader-Hooks ändern gemeinsamen Shader-/Descriptor-Zustand und globale Shader-/Dirty-Flags und verwenden den gemeinsamen D3D-Kontext. Eine solche Funktion unverändert gleichzeitig aufzurufen erzeugt konkurrierende Zugriffe. Ein Lock um die ganze Funktion würde diesen Abschnitt wieder serialisieren. [Community Shaders, geprüfter Commit](https://github.com/community-shaders/skyrim-community-shaders/blob/7109b526082f392385b30a4258f73418a7fa102e/src/Hooks.cpp)

Der Umbau braucht folgende Daten- und Ausführungsgrenzen:

1. **Stabile Renderpakete erzeugen:** Objekt-/Passliste, Transformdaten, Material-/Shader-Parameter und Ressourcenreferenzen für einen abgegrenzten Pass erfassen. Ressourcen bis zum Ende der Ausführung halten; Weltobjekte dürfen während der Worker-Arbeit nicht ungeprüft gelesen oder geändert werden.
2. **Vorbereitung auf Worker verlagern:** Eigene Zustände pro Worker für Shaderauswahl, Konstantenaufbereitung und geeignete Paketberechnungen. Gemeinsame Engine-Globals dürfen dort nicht mehr geschrieben werden. Ob Sortierung oder Culling dazugehören kann, hängt vom konkreten Pass und seinen Daten ab; der bisher erfasste Culling-Pfad läuft bereits auf Engine-Workern.
3. **Optional Befehle parallel aufzeichnen:** Ein Deferred Context pro Worker, vollständiger Pipeline-Zustand pro Paket, getrennte Upload-Bereiche. Ressourcenupdates, Queries und Map-Verhalten müssen den D3D11-Regeln entsprechen. Das Labor deckt dynamische Skyrim-Uploads noch nicht ab.
4. **Geordnet ausführen:** Die ursprüngliche Pass- und Abhängigkeitsreihenfolge bewahren. Transparenz, Schatten, Compute-/Render-Ziel-Wechsel und lesende Folgepässe dürfen nicht beliebig umsortiert werden.
5. **Originalpfad als Rückfall behalten:** Nicht unterstützte Pässe seriell ausführen. Erst nach Bildvergleich, Lebensdauerprüfung und Messung tatsächliche Engine-Arbeit ersetzen.

```mermaid
flowchart LR
    A[Main: stabile Pakete und Ressourcen] --> B[Worker: eigene Zustände und Vorbereitung]
    B --> C[Worker: eigene Deferred Contexts]
    C --> D[Main: geordnete Command Lists]
    D --> E[GPU und Present]
```

Die Hardwareseite von Schritt 3 und die geordnete Ausführung aus Schritt 4 sind im Labor umgesetzt. Schritt 1 und 2 für echte Skyrim-Daten und deren Lebensdauer sind noch nicht umgesetzt. Der Geräteaufruf und die Renderfunktion sind Untersuchungsadressen, keine freigegebenen parallelen Hooks.

## Weitere relevante Engine-Arbeit

| Bereich | Nutzen für das Ziel | Stand / erforderlicher Nachweis |
|---|---|---|
| Renderpakete und Konstanten vorbereiten | Direkte Verlagerung bislang serieller Renderarbeit. | Höchste Priorität für den nächsten Engine-Eingriff; gemeinsame Zustände und Passgrenzen müssen erfasst werden. |
| Bestehende Jobs und deren Join-Punkte | Main-Thread-Wartezeiten reduzieren, wenn Worker überlastet oder schlecht verteilt sind. | Aufgabenlaufzeiten und Abhängigkeiten messen. Mehr Worker oder veränderte Affinität allein sind keine Lösung. |
| Asset-Dekodierung und vorbereitende Streaming-Arbeit | Geeignete unabhängige Arbeit kann neben dem Frame laufen. | Kein solcher Engpass in den bisherigen Szenen belegt; Aktivierung und Änderung der Welt benötigen klare Übergaben. |
| Animation, AI und Script-nahe Engine-Funktionen | Möglicherweise viel Arbeit, aber gemeinsame Actor-/Weltzustände. | Keine Thread-Sicherheitsfreigabe aus den aktuellen Messungen. Ganze Actor-Updates unkontrolliert auf Worker zu verschieben ist kein implementierter Ansatz. |
| Bewegungsnachrichten-Typindex | Potenziell weniger redundante Arbeit in bestehenden Engine-Jobs. | Eigener Optimierungspfad. Der getestete zustandslose Scan ist korrekt in den Stichproben, aber bisher nicht schneller. [Live-Test](LIVE-MOVEMENT-003.md) |

Die ebenfalls disassemblierten `GetSystemInfo`-Wrapper lesen die Prozessorzahl. Das belegt keinen universellen Schalter oder eine geprüfte Konfiguration der Zahl der Engine-Worker. [Disassembly](integration/processor-count-disassembly.txt)

## Was bestehende Ansätze leisten

- **SKSE-Aufgaben:** `TaskInterface::AddTask` stellt Aufgaben in eine Queue; der untersuchte `BSTaskPool::ProcessTasks`-Hook arbeitet sie nacheinander ab. Diese Schnittstelle erzeugt allein keinen neuen CPU-Worker. [SKSE-Implementierung](https://github.com/ianpatt/skse64/blob/master/skse64/Hooks_Threads.cpp)
- **Community Shaders:** Der untersuchte ShaderCache enthält Hintergrundverwaltung für Shaderkompilierung. Daraus folgt keine Parallelisierung der gesamten Render-Vorbereitung oder Spielsimulation. Für unseren Entwurf sind besonders die gemeinsamen Renderzustände und spätere Hook-Kompatibilität relevant. [ShaderCache](https://github.com/community-shaders/skyrim-community-shaders/blob/7109b526082f392385b30a4258f73418a7fa102e/src/ShaderCache.cpp)
- **Engine Fixes:** Die geprüften Form- und Baumreferenz-Caches vermeiden wiederholte Suchen. Das ist ein Beispiel für das Entfernen unnötiger CPU-Arbeit, kein globaler Threading-Schalter. Die separat angefragte Speicherverwaltungsdatei war am abgefragten Pfad nicht verfügbar und wurde nicht als Beleg verwendet. [Form-Cache](https://github.com/aers/EngineFixesSkyrim64/blob/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c/src/patches/form_caching.h), [Baumreferenz-Cache](https://github.com/aers/EngineFixesSkyrim64/blob/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c/src/patches/tree_lod_reference_caching.cpp)
- **WorkerSpinLockFix:** Der Autor beschreibt Korrekturen für Deadlocks beziehungsweise Worker-Synchronisation. Das ist kein Nachweis für breitere Frame-Parallelisierung. Die Eignung für 1.7.104 wurde nicht geprüft; es wurde nicht installiert. [Autorenquelle](https://github.com/GoAhead-at/FreezeLogger/blob/main/skyrim-freeze-fix/README.md)

Die verwendeten Community-Shaders- und Engine-Fixes-Dateien sind mit Commit und SHA-256 in [render-source-manifest.json](render-source-manifest.json) dokumentiert.

## Nächster messbarer Engine-Schritt

Ein geeigneter einzelner Renderpass muss zunächst seine Paketanzahl, CPU-Vorbereitungsdauer, Zustandsänderungen und Ressourcenlebensdauer offenlegen. Anschließend kann ein Teil seiner Vorbereitung anhand stabiler Eingaben auf unseren Worker-Pool wandern. Erst danach ist die Command-List-Anbindung sinnvoll. Bloß alle bisherigen D3D-Aufrufe gleichzeitig weiterzuleiten löst die Engine-Abhängigkeiten nicht.

Für den Leistungsnachweis brauchen wir identische Ansichten mit vielen sichtbaren Objekten und separaten CPU-/GPU-Zeiten, zusätzlich zur Actor-Stressszene. Die Zahl gespawnter Wachen ist keine Messung der Renderpakete. Bei einem GPU-Limit kann weniger Main-Thread-Arbeit ohne FPS-Zuwachs bleiben; bei kleinen Paketen kann die Parallelisierung selbst bremsen. Erfolg bedeutet korrekte Ausgabe, stabile Ausführung und weniger Zeit auf dem kritischen Frame-Pfad. Eine umfassende Engine-Parallelisierung bleibt damit ein größeres Entwicklungsprojekt mit mehreren gezielten Eingriffen.
