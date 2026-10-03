# Repository-Inhalt und Nutzung

Dieses Repository enthält den bisherigen Quellcode, Tests, Werkzeuge, Forschungsberichte und das geprüfte RenderWorkerBridge-v8-Testpaket. Der aktuelle Stand ist in [PROJEKTSTAND-2026-10-03.md](PROJEKTSTAND-2026-10-03.md) beschrieben.

Version 8 ersetzt geeignete D3D11-Draws durch geordnete Aufzeichnung auf vier Workern. Zwei kurze Spielvergleichspaare bestätigen eine höhere Bildrate gegenüber der internen V8-Kopierkontrolle. Der Originalrenderer bleibt deutlich schneller. Dies ist ein Forschungsprototyp; eine allgemeine Skyrim-FPS-Steigerung ist nicht erreicht. Die Ersetzung startet ausgeschaltet.

## Mitgelieferte Messgrundlage

Die kompakten Berichte für den aktuellen v8-Labor- und Spielvergleich liegen unter `measurements/`. Der [Live-Bericht](../measurements/20261003-v8-live-comparison/analysis.json) enthält Messherkunft, Raten, Phasenbudgets und Grenzen. `analyze.py` reproduziert ihn aus den fünf gespeicherten Capture-Berichten und Zustandsverläufen. Frühere Berichte verweisen teilweise auf ausschließlich lokal vorhandene Aufnahmen.

Große ETL-Rohaufnahmen, vollständige Frame-CSV-Dateien, Bildschirmbilder, Spielstände, Installationssicherungen, lokale Builds und heruntergeladene Drittanbieterwerkzeuge sind nicht enthalten. Das Repository enthält keine Skyrim-Spielinstallation oder SKSE-Distribution. Skyrim-Code-Evidenz wird beim Bauen aus der eigenen, versionsgebundenen Installation erzeugt.

## Lokale Voraussetzungen

- Windows, Visual Studio C++ Build Tools, Windows SDK mit D3D11-Headern, CMake, Ninja und Python 3.
- Die geprüfte eigene SkyrimSE.exe 1.7.104.0 mit dem im Projekt dokumentierten SHA256; SKSE 2.3.1 zum Spieltest.
- Die Pfade in `tools/Build-Multicore.cmd`, CMake-Einstellungen und den INI-Dateien müssen zur lokalen Installation passen. Das vorhandene Buildskript ist auf die ursprüngliche Entwicklungsmaschine zugeschnitten.

Das feste [V8-Testpaket](../artifacts/render-worker-bridge-v8/README.md) enthält den unveränderten geprüften DLL-Build und eine INI mit neutralem Beispielpfad. Vor Nutzung auf einem anderen Rechner insbesondere `OutputDirectory` anpassen. Eine geänderte INI stimmt dann nicht mehr mit dem historischen INI-Hash im Paketmanifest überein; der DLL-Hash bleibt separat überprüfbar. Das Paketmanifest dokumentiert den Erstellungsstand und die Bereinigung des INI-Pfades; `live-test.json` dokumentiert den nachfolgenden Spieltest.

Für Tests ohne erneute Spielmessung dient CTest im Buildverzeichnis. Die v8-Laborprüfung bestand 17/17 Tests und 164 vollständige Bildvergleiche; aktuelle Nachweise liegen im Repository. Die Feststellung des Nutzers, dass Flackern auch bei Bewegung verschwunden war, ist als Beobachtung des aktuellen Spieltests dokumentiert und keine automatische Prüfung sämtlicher Szenen.
