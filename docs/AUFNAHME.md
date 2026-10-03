# Die erste tatsächliche Spielaufnahme

Die portable PresentMon-Konsole **2.6.0** liegt im Projekt unter `tools/vendor/presentmon`. Sie wurde aus dem offiziellen Intel-Projekt geladen. SHA-256 stimmt mit dem GitHub-Release-Digest überein; Authenticode meldet eine gültige Intel-Corporation-Signatur. Es wurde kein MSI installiert. Herkunft und Hash stehen in `origin.json`. [Offizieller Release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0)

Der Vorabtest von PresentMon endete mit **Exit 6 / access denied**. Der derzeitige PowerShell-Prozess ist nicht als Administrator erhöht. Für den CPU-/GPU-ETW-Trace benötigt der Aufnahmeprozess einmalig erhöhte Rechte. Es werden keine Benutzergruppen oder dauerhaften Berechtigungen geändert.

## Aufnahme starten

Skyrim muss bereits in einer geladenen Szene laufen. Blickpunkt und Kamera festhalten, Menüs und Konsole schließen, Streaming abwarten. Dann:

```powershell
.\tools\Start-Baseline.ps1 -Scene 'Whiterun: fester Blickpunkt, Kamera still' -DurationSeconds 30
```

Windows zeigt eine UAC-Abfrage für PowerShell. Sie muss vom Nutzer bestätigt werden; der Assistent bedient diesen Sicherheitsdialog nicht. Nach Bestätigung bleiben acht Sekunden, um das Spielfenster wieder zu fokussieren. Danach 30 Sekunden die Kamera stillhalten und im Spiel bleiben.

Die Skripte sind vor dem Start prüfbar:

- `Start-Baseline.ps1`: identifiziert Skyrim, schreibt eine konkrete Aufnahmeanforderung und startet den erhöhten Helfer.
- `Run-CaptureRequest.ps1`: liest die Anforderung und meldet Startfehler im Aufnahmeordner.
- `Capture-Baseline.ps1`: prüft Zielprozess, Hash und Intel-Signatur; startet das schmale WPR-Profil **SkyrimCPU** und PresentMon unter einem zufälligen eigenen Session-Namen; stoppt und speichert die Aufnahme automatisch.

Das Projektprofil enthält SampledProfile-Events mit Stacks sowie CSwitch-/ReadyThread-Ereignisse ohne Stacks. Der breite Standard-CPU-/GPU-Recorder hat im ersten Lauf zu massiven Ereignisverlusten geführt und wurde ersetzt. Die tatsächliche Qualität und verlorene Events werden nach jeder Aufnahme geprüft. Der Trace enthält auch systemweite Aktivität, die für Scheduling-Abhängigkeiten gebraucht wird; er bleibt lokal im Projekt. PresentMon protokolliert keine Eingabe-Latenzmetriken (`--no_track_input`) und schreibt Frame-Daten nur für die ausgewählte Skyrim-PID.

Der PowerShell-Thread-Sampler läuft während dieser Baseline **nicht zusätzlich**, damit sein Aufwand die Aufnahme nicht vergrößert. Thread-Zuordnung und CPU-Zeit werden aus ETW untersucht.

## Dateien und Abschluss

Jeder Lauf erhält einen neuen Ordner in `measurements/` mit:

- `request.json` und `capture-state.json`: Szenenbeschreibung, Zeiten, Version, EXE-Hash, geladene Module und Erfolg/Fehler.
- `frames.csv`: PresentMon-Frame-Ereignisse.
- `cpu.etl`: CPU-Samples und Scheduling; GPU-/Frame-Metriken stehen im PresentMon-CSV.
- Recorder-/PresentMon-Logs für die Diagnose.

`status: captured` bestätigt vorhandene Dateien und nichtleere Frame-Daten. Die Trace-Qualität ist dann noch ungeprüft. Der erste Lauf verwendete noch die ältere Statusbezeichnung `complete`, obwohl seine CPU-Daten wegen Ereignisverlusten ungeeignet waren. Kein Status beweist CPU-Begrenzung oder erkennt Ladebildschirme beziehungsweise eine bewegte Kamera. Diese Bedingungen werden mit dem Nutzer und dem Trace abgeglichen.

Bei Recorder-Fehlern wird ausschließlich die von diesem Lauf gestartete WPR-Instanz beendet beziehungsweise ihre Beendigung versucht. Fremde Aufnahmen werden nicht abgebrochen. Bei einem Host-Abbruch während der Aufnahme kann eine manuelle Beendigung der in `capture-state.json` genannten Instanz nötig sein.

Erste Frame-Zusammenfassung:

```powershell
.\tools\Summarize-Frames.ps1 -CsvPath '.\measurements\LAUF\frames.csv'
```

Sie trennt Swap Chains, gibt mittlere/Median-/P95-/P99-Frameintervalle aus und kennzeichnet ungültige Werte sowie die Anzahl gültiger Messwerte je Metrik. Die daraus berechnete Present-Rate darf nicht automatisch als reale oder angezeigte FPS verstanden werden. Erst danach folgt die Aufrufstapel- und Wartekettenanalyse in WPA.

## Status der Vorbereitung

**Aktualisierung nach der Wiederholung:** Der zehnsekündige Lauf mit dem schmalen Profil wurde ohne verlorene Ereignisse oder Puffer aufgenommen. Die Auswertung liegt in `research/BASELINE-002.md`. Aus vorhandenen xperf-Exporten erzeugt `tools/Summarize-CPUExports.ps1 -RunDirectory 'measurements/LAUF'` die CPU-Zusammenfassung; bei Ereignisverlusten verweigert es die Auswertung.

Skyrim wurde über Steam gestartet. PresentMon-Version und CLI wurden lokal überprüft. Parserprüfung aller Skripte und Prüfung der Abweisung ohne erhöhte Rechte bestanden. Die erste tatsächliche Aufnahme ist erfolgt; ihr CPU-Trace wurde wegen massiver Ereignisverluste verworfen. Details in `research/BASELINE-001.md`. Das Spielfenster ließ sich mit Computer Use trotz Aktivierungsversuch nicht als Screenshot erfassen (`no screenshot targets found`); die Auswahl der Szene bleibt deshalb beim Nutzer.
