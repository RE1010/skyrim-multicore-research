# RenderWorkerBridge Version 8 – geprüftes Testpaket

Dieses feste Paket enthält die geprüfte DLL und die lokale INI. Es wurde am 3. Oktober installiert und im Spiel getestet; die Ersetzung ist danach wieder ausgeschaltet. Version 8 besteht 17 Tests und 164 vollständige Labor-Bildvergleiche. Zwei kurze Spielvergleichspaare ergeben 41,56–42,69 Presents/s mit geteilten Gruppen gegenüber 33,14–33,91 mit Kopierkontrolle, im Mittel 25,7 % mehr innerhalb des Worker-Pfads. Die Originalreferenz bleibt mit 120,51 deutlich schneller; ein allgemeiner Skyrim-FPS-Gewinn ist nicht erreicht. Der Nutzer bestätigt, dass das frühere Flackern im aktuellen Test auch bei Bewegung verschwunden war; weitere Szenen sind noch nicht geprüft. [Live-Bericht](../../measurements/20261003-v8-live-comparison/analysis.json).

Version 8 teilt unveränderte Binding-Gruppen zwischen Draws. Geänderte Gruppen erhalten eigene, unveränderliche Versionen. Konstanteninhalte und Drawargumente bleiben pro Draw gespeichert. Paketgrenzen, GPU-Barrieren und Wiedergabereihenfolge bleiben erhalten. Neue Messwerte trennen Gruppenerzeugung und Queue-Freigabe; Gruppenerzeugung ist bereits in der Erfassungszeit enthalten.

Kompatibilität: die hier geprüfte SkyrimSE.exe 1.7.104.0 mit SKSE 2.3.1. Der ausführbare Datei-Hash wird weiterhin strikt geprüft. Datei-Hashes und Laborherkunft stehen in `manifest.json`. Die INI ist für dieses lokale Projekt eingerichtet und startet mit `Enabled=0`, vier Workern.

## Installation oder spätere Wiederinstallation

Skyrim und den SKSE-Loader regulär beenden. Im Projektverzeichnis kann das vorhandene Installationsskript dieses feste Paket auswählen; Schreibzugriff auf den Steam-Ordner benötigt gegebenenfalls Administratorrechte:

```powershell
Set-Location 'C:\SkyrimMulticoreResearch'
.\tools\Install-RenderWorkerBridge.ps1 -SourceDirectory 'C:\SkyrimMulticoreResearch\artifacts\render-worker-bridge-v8'
```

Das Skript verweigert den Austausch bei laufendem Spiel, prüft die Skyrim-Datei und legt vor dem Austausch eine Sicherung mit Manifest unter `measurements/bridge-installation-...` an. Danach Skyrim über den vorhandenen SKSE-Loader starten. Im SKSE-Log muss `RenderWorkerBridge.dll` als Version `00000008` korrekt geladen sein.

## Kurzer Vergleich

Dieselbe Ansicht vor Weißlauf laden, Konsole und Menü schließen. Zunächst bleibt der Originalpfad aktiv. Für erste Beobachtungen jeweils zehn Sekunden verwenden:

```powershell
# Kontrolle: jede Gruppe pro Draw neu kopieren.
.\tools\Set-RenderBridgeMode.ps1 -Mode parallel-owned-snapshots -DurationSeconds 10

# Bei wieder aktivem Originalpfad: Gruppen gemeinsam nutzen.
.\tools\Set-RenderBridgeMode.ps1 -Mode parallel -DurationSeconds 10

# Jederzeit zurück zum Originalpfad.
.\tools\Set-RenderBridgeMode.ps1 -Mode off
```

Die Kontrolle verwendet dieselbe neue Gruppenstruktur und dieselben Recorder-Regeln, kopiert aber alle 13 Gruppen für jeden Draw. Sie entspricht nicht exakt der flachen Snapshot-Struktur aus Version 7. Der Vergleich misst die Wiederverwendung innerhalb von Version 8. Ein Gewinn gegenüber Skyrim muss zusätzlich am Originalpfad gemessen werden. `parallel-full-bindings` ist weiterhin die separate Kontrolle der Setter-Reduktion; `parallel-uncached` prüft den Getter-Cache.

Der durchgeführte genaue Vergleich verwendet `Start-Baseline.ps1`, PresentMon und gespeicherte Bridge-Zustandsverläufe. Für weitere Vergleiche müssen Originalpfad, `parallel-owned-snapshots` und `parallel` an derselben festen Szene gemessen werden. Keine zusätzliche NPC-Last während einer Aufnahme verändern. Bildfehler und FPS getrennt beurteilen; einzelne Bildschirmbilder können schnelles Flackern übersehen. `manifest.json` bleibt der historische Erstellungsstand, `live-test.json` dokumentiert den späteren Test.

Laborbericht: `measurements/20261003-143229-005-render-bridge-ownership-tests/analysis.json`. Implementierungs- und Messdetails: `research/RENDER-BRIDGE-001.md`.
