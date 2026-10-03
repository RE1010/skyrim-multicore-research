# Erste Baseline und Auswahl eines Engine-Eingriffs

## Zweck

Wir bestimmen, welcher Thread und welche Arbeit die Frame-Ausgabe begrenzen. Erst danach entscheiden wir zwischen weniger Arbeit, Parallelisierung und einer Änderung der API-Anbindung. Gesamte CPU-Auslastung und GPU-Prozentwerte allein reichen dafür nicht.

## Ausgangszustand herstellen

1. Ein separates Testprofil mit festgehaltener Runtime, aktiver Modliste, INIs, Treiber und Grafikmodus verwenden. Den Setup-Bericht vor jeder Vergleichsserie erneut erzeugen. Er erfasst Datenträgerdateien, nicht automatisch Modmanager-Overrides.
2. Drei reproduzierbare Testszenen anlegen: Stadtblick mit vielen Objekten, Wald-/Außenbereich, kontrollierte Actor-Szene. Zunächst eine einzige Szene vollständig messen. Gleiche Position, Kamera, Tageszeit, Wetter und Actor-Situation dokumentieren; relevante Unterschiede bleiben als Einschränkung im Ergebnis.
3. Shader und Streaming aufwärmen. 30–60 Sekunden Aufnahme nach stabiler Ladephase; mindestens drei Wiederholungen. Capture nicht in einem Menü starten. Bild und Kamera zur Wiederherstellung der Szene festhalten.
4. Für die CPU-/GPU-Abgrenzung Auflösung beziehungsweise Renderauflösung ändern, andere Einstellungen konstant halten. Danach gezielt Schatten-/Objektlast einzeln variieren. Mehrere Einstellungen gleichzeitig verhindern eine klare Zuordnung.
5. FPS-Limit, VSync und Frame Generation dokumentieren. Für die Kapazitätsmessung ein kontrolliertes Testprofil ohne begrenzendes FPS-Limit verwenden. Framerate-abhängiges Verhalten der Physik muss vorher geprüft sein; ein ungeprüfter Vanilla-Test über dem vorgesehenen Framerate-Bereich ist keine saubere Baseline. Für bloße Thread-Traces kann das Spiel zunächst mit bestehendem Limit laufen.

## Vorbereitete Bestandsaufnahme

```powershell
.\tools\Collect-Setup.ps1
```

Die Datei `research/local-setup.json` enthält Version und EXE-Hash, Registry-Hardwarenamen, sichtbare DLLs sowie ausgewählte INI-Werte. Fehlende oder unlesbare Daten sind gekennzeichnet. Nichts wird im Spielordner geschrieben.

## Grobe Thread-Messung ohne Injection

Das Spiel manuell starten und die Testszene laden. Dann im Projektverzeichnis:

```powershell
.\tools\Sample-Threads.ps1 -DurationSeconds 30 -IntervalMs 500 -Label city-baseline
```

CSV und JSON entstehen in `measurements/`. **100 % bedeutet CPU-Zeit entsprechend einem einzelnen logischen Prozessor**, unabhängig davon, auf welchem Kern Windows den Thread ausgeführt hat. Der Sampler liest kumulative Thread-CPU-Zähler und bildet Differenzen über tatsächliche Zeitintervalle. Thread-ID und Erstellungszeit verhindern eine Vermischung wiederverwendeter Thread-IDs innerhalb der Aufnahme. [Microsoft: Thread-CPU-Zeit](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processthread.totalprocessortime)

Das Werkzeug misst weder Frames noch GPU-Zeit, Aufrufstapel, Core-Platzierung oder Abhängigkeiten. Kurze Threads können verpasst werden. Es benennt keinen Main Thread. Ein stark ausgelasteter Thread kann auf dem kritischen Pfad liegen; der Beweis braucht den nächsten Schritt. Auch eine wartende Engpasskette kann geringe CPU-Zeit haben. Sampler-Aufwand durch einen separaten Vergleich mit/ohne Sampler prüfen.

## Aufrufstapel und Warteketten

Windows Performance Recorder und Windows Performance Analyzer sind lokal vorhanden. Die folgenden Kommandos sind **vorbereitete Anleitung**, bisher nicht als Spielmessung ausgeführt:

```powershell
wpr -status
wpr -start GeneralProfile -start GPU -filemode
# Reproduzierbare Szene für 30 Sekunden ausführen.
wpr -stop .\measurements\city-baseline.etl
```

Vor dem Start `measurements` anlegen, falls noch nicht vorhanden. WPR kann erhöhte Rechte benötigen. Wenn bereits eine fremde Aufnahme läuft, diese nicht abbrechen oder übernehmen. Die aktive Aufnahme anschließend zeitnah stoppen; ETL-Dateien können groß werden. Welche Events und Stacks enthalten sind, nach dem Öffnen kontrollieren. [Microsoft: WPR-Kommandos](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)

In WPA auf `SkyrimSE.exe` filtern, CPU Usage (Sampled) nach Thread/Stack ansehen und mit CPU Usage (Precise) für Running/Ready/Wait-Zeiten abgleichen. Grafikaktivität und Frame-Ausgabe zeitlich zuordnen. Ein wartender Render-Thread kann auf einen Worker warten; dann ist dieser Worker Teil des zu optimierenden Pfads. Windows-Symbole helfen bei System-/Treiberanteilen. Für unbekannte Skyrim-Adressen verwenden wir zunächst `SkyrimSE.exe+RVA` zusammen mit dem EXE-Hash; Engine-Funktionsnamen benötigen zusätzliche Reverse-Engineering-Zuordnung. [Microsoft: CPU-Analyse](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis)

## Frame-Daten ergänzen

PresentMon kann separat Frame-Ereignisse aufnehmen. Die Konsolenanwendung wurde hier nicht heruntergeladen oder installiert. Nach Auswahl einer geprüften Version und Kontrolle ihres `--help`:

```powershell
.\PresentMon.exe --process_name SkyrimSE.exe --timed 30 --terminate_after_timed --output_file .\measurements\city-frames.csv
```

Die aktuell dokumentierte CLI verwendet diese Optionen. Relevant sind Frametime-Verteilung und CPU-/GPU-Metriken entsprechend dem Datenformat der eingesetzten Version. Angezeigte Frames mit Frame Generation nicht mit real gerenderten Frames gleichsetzen. [PresentMon CLI](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)

Community-Shaders-Overlay und Pass-Profiler können zusätzlich Draw Calls und instrumentierte Render-Pässe untersuchen. Ihre kompatible Installation ist ein gesonderter Schritt; der Wechsel des gesamten Render-Setups würde eine andere Baseline erzeugen.

## Vergleich und Entscheidungsgrenzen

| Beobachtung | Nächste Untersuchung |
|---|---|
| Weniger Pixel ändern die Frametime kaum, ein CPU-Pfad dominiert | CPU-Kandidat mit Stack und Frame-Bezug untersuchen |
| Weniger Pixel verkürzen deutlich die GPU-Zeit und Frames | Zuerst GPU-Engpass bestätigen; CPU-Patch hat dort begrenzten Nutzen |
| Viel Zeit in D3D11-/Treiberaufrufen | Draw-/State-Kosten, Ressourcenupdates und ggf. API-Vergleich |
| Teure Engine-Funktionen vor der API-Submission | Redundanz, lokale Daten und Parallelisierbarkeit prüfen |
| Viel Wait-/Ready-Zeit | Abhängigkeiten, Limiter, I/O und Scheduling untersuchen |

Das sind Hinweise; mehrere Engpässe können gleichzeitig auftreten und nach einem Eingriff wechseln.

Der erste Prototyp hat drei Modi: aus, serieller neuer Pfad, paralleler neuer Pfad. Gleiche Ausgaben und visuelle Ergebnisse zunächst gegen die alte Implementierung prüfen. Anschließend A/B/A/B-Serien mit durchschnittlicher Frametime, P95/P99, realen Frames, GPU-Zeit und relevanten CPU-Abschnitten vergleichen. Keine FPS-Versprechen aus einem isolierten Mikrobenchmark ableiten.

Als vorab gewähltes Projektkriterium: mindestens 10 % niedrigere durchschnittliche Frametime in der ausgewählten CPU-begrenzten Szene, keine systematische P99-Verschlechterung und keine sichtbare Ergebnisänderung. Das ist eine Entscheidungsschwelle, keine erwartete Verbesserung. Wenn der Effekt im Streubereich liegt, mehr Wiederholungen; wenn Kosten oder Abhängigkeiten den Gewinn aufheben, Kandidaten wechseln.

Vor einer Veröffentlichung: Zellwechsel, Innen-/Außenwechsel, Kämpfe, Save/Load, lange Sitzungen, unterschiedliche Modprofile und reproduzierbare Absturztests. Ein kurzer Benchmark beweist keine allgemeine Stabilität oder Savegame-Sicherheit.
