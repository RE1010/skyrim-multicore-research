# Parallele D3D11-Befehlsaufzeichnung auf der lokalen Hardware

2. Oktober 2026. Implementierung: [benchmarks/d3d11_parallel.cpp](../benchmarks/d3d11_parallel.cpp). **Eigenständiger Prozess und eigenes D3D11-Gerät; keine Skyrim-Renderfunktion wird ersetzt.** Skyrim war bei der abschließenden Qualitätsprüfung geschlossen.

## Was tatsächlich implementiert wurde

Ein beständiger C++20-Worker-Pool zeichnet echte `DrawIndexed`-Befehle auf 1, 2, 4 oder 6 Threads auf. Jeder Worker besitzt einen eigenen Deferred Context. Der Hauptthread führt die fertigen Command Lists in ursprünglicher Paketfolge auf seinem Immediate Context aus. Beide Varianten verwenden dieselben unveränderlichen Ressourcen und erzeugen ein 512 × 512 RGBA-Bild.

Drei Laststufen mit 4.096, 16.384 und 32.768 Draws testen sowohl kleine Pakete als auch genügend Arbeit für eine Verteilung. Spätere Draw-Schichten überschreiben frühere Schichten; die Prüfung kontrolliert damit auch das sichtbare Resultat der Reihenfolge. Die Grafikpipeline ist einfach und bleibt gleich. Materialwechsel, dynamische Konstanten-Uploads und Skyrim-Weltobjekte gehören noch nicht zum Test.

Das lokale NVIDIA-Gerät meldet `DriverConcurrentCreates=true` und `DriverCommandLists=true`, Feature Level 11.1. Das gilt für das Laborgerät; Skyrims Live-Gerät wurde hier nicht abgefragt. [Microsoft: Threading-Fähigkeiten](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_threading)

## Korrektheitsprüfung

Beide vollständigen Läufe, einmal mit D3D11-Debugschicht und einmal ohne, bestehen:

- Je 12 Konfigurationen und 372 gelesene beziehungsweise geprüfte Bilder; zusammen **744 Bildprüfungen ohne Abweichung**. Es sind wiederholte Ausgaben der drei Laststufen, keine 744 unterschiedlichen Spielszenen.
- Eine unabhängige CPU-Referenz prüft 4.096 erwartete Kachelmittelpunkte pro Bild. Das verhindert, dass zwei identisch falsche oder leere Bilder als Erfolg gelten.
- Zusätzlich wird jede parallele Ausgabe vollständig byteweise mit der seriellen Referenz verglichen, einschließlich der Randpixel.
- Die D3D11-Debugschicht enthält **0 Meldungen**, also auch keine Warnungen oder Fehler.
- Alle Konfigurationen besitzen die erwartete Zahl verschiedener Worker-Thread-IDs und positive gemessene Worker-Zyklen für die Befehlsaufzeichnung.

Die rohe Ausgabe und abschließende Prüfung stehen in [Release](D3D11-PARALLEL-LAB.json), [Debug](D3D11-PARALLEL-LAB-debug.json) und [Qualitätsnachweis mit Hashes](D3D11-PARALLEL-quality.json). Die Prüfung belegt die implementierte synthetische Pipeline; sie beweist keine allgemeine Sicherheit beliebiger Skyrim-Pässe.

## CPU-Zeiten ohne Debugschicht

Pro Konfiguration wurden drei Aufwärmpaare und zwölf gemessene Paare ausgeführt. Die Reihenfolge seriell/parallel wechselt. Die folgenden Werte sind Mediane eines einzelnen kurzen Durchlaufs; sie sind keine kontrollierte Studie über Taktraten und Thread-Platzierung.

| Draws | Worker | Seriell, ms | Parallel inklusive Verteilung, Warten und Ausführung, ms | Main-Thread-Zyklen seriell → parallel |
|---:|---:|---:|---:|---:|
| 4.096 | 4 | 0,02720 | 0,06755 | 68.889,5 → 74.879 |
| 16.384 | 4 | 0,09125 | 0,09610 | 223.142,5 → 79.997 |
| 32.768 | 4 | 0,16160 | 0,11065 | 406.598,5 → 76.128 |
| 32.768 | 6 | 0,15985 | 0,10685 | 401.606,5 → 91.603,5 |

Mit vier Workern sinken bei der größten Last die Main-Thread-Zyklen des gemessenen Abschnitts um rund 81 %, während dessen verstrichene CPU-Abgabezeit nur um rund 32 % sinkt. Die absolute Ersparnis beträgt **0,05095 ms**. Bei 16.384 Draws wird Arbeit vom Hauptthread verlagert, ohne dass der Gesamtabschnitt schneller wird. Bei 4.096 Draws ist die parallele Variante deutlich langsamer. Diese Ergebnisse begründen eine Mindestpaketgröße und zeigen, weshalb geringere Main-Thread-Last allein kein Leistungsnachweis ist.

`QueryThreadCycleTime` misst verbrauchte Thread-Zyklen, keine Millisekunden; Frequenz und Kernplatzierung können die Werte beeinflussen. Die Zeitmessung enthält CPU-Aufzeichnung, Dispatch, Worker-Warten und geordnete Ausführung. GPU-Readback und Bildprüfung liegen außerhalb dieses Abschnitts. Die GPU wird für die Bildprüfung zwischen Ausgaben abgearbeitet. GPU-Framezeit, fortlaufender Frame-Durchsatz und Skyrim-FPS werden damit **nicht** gemessen. Die Gesamtzyklusfelder der Rohdaten enthalten beide Varianten, Aufwärmung und Validierung und dürfen nicht als reine Submission-Zeit interpretiert werden.

Debugwerte dienen der Fehlerprüfung. Ihre Laufzeitverhältnisse werden nicht als Release-Leistungsgewinn verwendet.

## Reproduzieren

Aus dem Projektverzeichnis in PowerShell auf dieser Windows-/Visual-Studio-Installation:

```powershell
.\tools\Build-Multicore.cmd
.\build\lab\d3d11_parallel_lab.exe
.\build\lab\d3d11_parallel_lab.exe --debug
```

Der zweite Aufruf benötigt die D3D11-Debugschicht. Ausgaben werden erst nach erfolgreicher Prüfung als JSON erzeugt; ein Fehler beendet das Programm mit einem Fehlerstatus. Der Hardwaretest ist bewusst kein automatischer CTest-Pflichttest für Rechner ohne passende Grafik-/Debugumgebung.

## Bedeutung für das Projekt

Der Worker-/D3D11-Ausführungspfad funktioniert auf der vorhandenen Hardware. Eine Engine-Anbindung muss nun echte Renderpakete stabil erfassen, gemeinsame Zustände auflösen und die ursprünglichen Pass-Abhängigkeiten erhalten. Das Labor ist der getestete technische Baustein dafür; es installiert keinen allgemeinen Multithreading-Enabler. [Breitere Engine-Untersuchung](MAIN-THREAD-004.md)
