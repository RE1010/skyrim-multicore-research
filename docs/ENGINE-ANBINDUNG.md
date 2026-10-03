# Lokale Engine-Anbindung

Stand: 2. Oktober 2026. Ziel ist echte Parallelisierung geeigneter Engine-Arbeit. Dieses Plugin ist der erste verifizierte Adapter und dient zunächst zur Korrektheitsmessung.

## Tatsächlich umgesetzt

- SKSE 2.3.1 Steam aus dem offiziellen SKSE-Team-Eintrag auf Nexus heruntergeladen und gemäß beiliegender Anleitung installiert. Archiv-SHA256: `7BAD616ED360823A027F8828801D91E3A4AA2EACD952023AF7F3E31ADB2AE250`.
- Ziel: SkyrimSE.exe 1.7.104, SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`.
- `MulticoreVisibilityShadow.dll` als SKSE-Plugin in `Data/SKSE/Plugins` installiert. SKSE meldet erfolgreiches Laden; das Plugin bestätigt Hash, Funktionsbytes und Vtable-Slot.
- Gebunden ist `BSCullingProcess::TestBaseVisibility3`, Vtable-RVA `0x1a6ef90`, Slot `0x1c`, Funktion-RVA `0xfee940`. Zusätzlich wird der verifizierte identische Slot der abgeleiteten Klasse `BSGeometryListCullingProcess`, Vtable-RVA `0x1868af8`, erfasst. RTTI, Aufrufstellen und lokales Disassembly stehen in `research/integration/`.
- Sechs numerische Ebenen, Bounding Sphere, Plane-Masken vor/nach dem Originalaufruf und aufrufende Thread-ID werden kopiert. Engine-Zeiger verlassen den Callback nicht. Eine begrenzte Queue verwirft Messdatensätze bei Überlast, statt auf die Auswertung zu warten.
- Ein Hintergrundkoordinator gruppiert numerisch gleiche Ebenenzustände und vergleicht die Originalergebnisse mit serieller und paralleler Berechnung auf vier persistenten Worker-Threads.

## Korrektheitsprüfung

131.456 Fälle werden gegen die tatsächlichen 198 Bytes dieser lokalen Originalfunktion im isolierten Testprozess verglichen. Das prüft Sichtbarkeit und die vom Original veränderte Plane-Maske, mit und ohne Plane-Optimierung. Zufallsdaten, alle 64 Plane-Masken, NaN und Grenzfälle sind enthalten. Zusätzlich werden der Snapshot-Replay, absichtlich falsche Ergebnisse und die Ablehnung falscher Runtime/EXE geprüft. Ein weiterer Test sperrt die Berichtsdatei als externer Leser und prüft, dass die Veröffentlichung aufgeschoben und nach Freigabe erfolgreich wiederholt wird. Alle vier CTest-Tests bestehen.

Engine und konservativer Laborpfad unterscheiden sich an der unteren Tangentialgrenze. Engine-Reihenfolge und Maskenänderung sind im eigenen Runtime-Modus nachgebildet. Das bestandene Testset beweist keine universelle Übereinstimmung für alle Float-Werte oder beliebige zukünftige Skyrim-Versionen.

## Aufnahme

Skyrim über `skse64_loader.exe` starten und eine Szene laden. Danach aus dem Projektverzeichnis:

```powershell
.\tools\Start-CullingCapture.ps1
$current=Get-Content .\measurements\live-culling\current-session.json | ConvertFrom-Json
Get-Content (Join-Path $current.sessionDirectory 'summary.json')
# Erst nach status=capture-complete:
.\build\lab\replay_culling.exe (Join-Path $current.sessionDirectory 'snapshots.bin')
```

Die Aufnahme endet nach höchstens 100.000 Versuchen oder etwa 20 Sekunden. Danach wird der ursprüngliche Vtable-Eintrag wiederhergestellt, sofern kein anderes Plugin ihn zwischenzeitlich geändert hat. Bei einem Konflikt bleibt der Callback als Original-Passthrough aktiv. Ein Neustart des Spiels ist für eine neue Aufnahme nötig. Jede Sitzung besitzt `plugin.log`, `snapshots.bin` und eine atomar aktualisierte `summary.json`.

Die erste Sitzung (`session-17152-42766078`) mit ausschließlich der Basisklassen-Vtable lieferte null Aufrufe und gilt als ungültige Engine-Messung. Anschließend wurde der identische Slot der abgeleiteten Geometry-List-Klasse ergänzt und das Spiel neu gestartet.

Die zweite Sitzung (`session-36984-43087687`) erfasste 4.288 gültige Engine-Datensätze aus sechs aufrufenden Skyrim-Threads. Ein unabhängiger Replay bestätigte keine Sichtbarkeits-, Masken- oder Serial/Parallel-Abweichungen. Diese Aufnahme wurde durch einen Fehler bei der Berichtspublikation vorzeitig beendet. Die korrigierte Fassung wiederholt die Veröffentlichung bei vorübergehenden Dateisperren, ohne die Aufnahme zu beenden.

Die dritte Sitzung schloss vollständig ab: **68.739 gültige Live-Datensätze, keine Abweichungen, sechs aufrufende Engine-Threads**. Ein unabhängiger Replay bestätigt das Ergebnis. 31.261 weitere Messdatensätze wurden wegen der begrenzten Diagnose-Queue verworfen. Details und Rohdatenbezüge: [Live-Vergleich](../research/LIVE-CULLING-001.md).

Ein gültiger Korrektheitsnachweis benötigt `compared > 0`, keine abgelehnten Datensätze und keine Ergebnisabweichungen. Die verworfenen Queue-Einträge werden separat gezählt. Ein Bericht mit null Datensätzen ist kein erfolgreicher Engine-Test.

## Noch keine Beschleunigung im Spiel

Jeder Spielaufruf führt weiterhin die originale Funktion aus und gibt deren Ergebnis samt Seiteneffekten zurück. Die parallele Berechnung läuft zusätzlich zur Diagnose. Deshalb sind FPS während dieser Aufnahme kein Beschleunigungsbenchmark.

Einzelne Sphere-Tests synchron an Worker zu schicken wäre wegen der Übergabe- und Wartekosten voraussichtlich langsamer. Der nächste notwendige Schritt ist eine verifizierte Batch-Grenze in der Traversierung: unabhängige numerische Eingaben zusammentragen, gemeinsam rechnen und Ergebnisse in Engine-Reihenfolge übernehmen. Plane-Masken und hierarchische Abhängigkeiten dürfen nicht übergangen werden. Erst dieser Schritt ersetzt Main-Thread-Arbeit und muss mit identischen Szenen, Frames, Trace und Bildausgabe gemessen werden.

## Wiederherstellung und Build

Die Installation enthält 66 einzeln geprüfte Dateien. Das Installationsmanifest unter `measurements/installation-20261002-210437/manifest.json` dokumentiert jeden Zielpfad und gegebenenfalls die gesicherte Vorgängerdatei. Zum Deaktivieren des Diagnose-Plugins bei beendetem Spiel genügt, `MulticoreVisibilityShadow.dll` aus dem SKSE-Plugins-Verzeichnis zu verschieben. SKSE kann separat erhalten bleiben.

Build: `tools/Build-Multicore.cmd`, anschließend CTest in `build/lab`. Das CMake-Projekt erstellt Runtime-Evidenz nur bei passendem EXE-Hash. Die Originalbytes liegen ausschließlich im ignorierten Build-Verzeichnis. Download und entpackte SKSE-Quellen sind ebenfalls ignoriert und werden nicht mit unserem Projekt weiterverteilt.

Grundlagen: [offizielle SKSE-Seite](https://skse.silverlock.org/), [offizielles Plugin-Interface](https://github.com/ianpatt/skse64/blob/master/skse64/PluginAPI.h), [SKSE-Team-Download](https://www.nexusmods.com/skyrimspecialedition/mods/30379). Die minimalen ABI-Deklarationen wurden außerdem gegen den Quellcode im tatsächlich heruntergeladenen 2.3.1-Paket geprüft.
