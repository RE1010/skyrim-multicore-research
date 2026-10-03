# Skyrim: Untersuchung serieller CPU-Engpässe

Stand: 3. Oktober 2026. Ziel: echte Parallelisierung eines geeigneten Engine-Pfads auf mehrere CPU-Kerne.

[Übersicht: bisher erledigt, Messwerte und offene Punkte](docs/PROJEKTSTAND-2026-10-03.md). [Repository-Nutzung](docs/REPOSITORY.md) und [Bereinigung vor Veröffentlichung](docs/VEROEFFENTLICHUNG.md).

**RenderWorkerBridge Version 8 live getestet, vorerst ausgeschaltet:** Echte Draws werden nachweislich auf vier eigenen Workern aufgezeichnet und anstelle der Original-Draws abgespielt. Zwei 15-Sekunden-Vergleichspaare mit PresentMon ergeben 33,14–33,91 Presents/s mit neuer Kopierkontrolle und 41,56–42,69 mit geteilten Zustandsgruppen. Das sind im Mittel 25,7 % mehr innerhalb des Worker-Pfads; die Originalreferenz liegt bei 120,51 Presents/s und bleibt deutlich schneller. Etwa 88,5 % der Gruppenkopien entfallen. Alle fünf CPU-Traces haben keine verlorenen Ereignisse oder Buffer; alle Adapterfehlerzähler bleiben null. Im separaten Bildtest ist kein grober Hell-dunkel-Wechsel sichtbar; der Nutzer bestätigt, dass das Flackern auch bei Bewegung verschwunden war. Weitere Szenen sind noch nicht geprüft. [Anbindung, Spielbefund und Grenzen](research/RENDER-BRIDGE-001.md).

**Version 6 geprüft und installiert:** Ein Cache fragt unveränderte Renderzustände nicht erneut beim Treiber ab; neue Buffer-Inhalte werden trotzdem pro Draw übernommen. 48 vollständige Bildprüfungen und alle 16 Tests bestehen, einschließlich gezielter automatischer Ressourcenentbindung. Im Labortest mit konstanten Bindungen entfallen 95,8 % der Getter und rund 78,9 % der Erfassungszeit. Der direkte Live-Vergleich über `parallel-uncached` bestätigt einen kleineren Gewinn für diesen Teilschritt. Die Werte sind keine allgemeine Skyrim-FPS-Steigerung.

**Version 7 geprüft und installiert:** Die Worker überspringen unveränderte Bindungen innerhalb einer Command List und setzen zu Beginn jeder Liste den vollständigen Zustand. Neue Buffer-Uploads erzwingen weiterhin die betroffenen Bindungen. 66 vollständige Bildprüfungen und alle 16 Tests bestehen. Die Materialkontrolle im Labor setzt 58,3 % weniger Bindungen; der Spielvergleich mit `parallel-full-bindings` bestätigt die Reduktion der Aufrufe. Zwei Agenten prüften Zustandsregeln und Bildtest-Abdeckung.

**Version 8 geprüft und installiert:** Unveränderliche Binding-Gruppen werden zwischen Draws geteilt; Drawargumente und Constant-Byte-Versionen bleiben individuell. Beide Besitzmodelle bestehen insgesamt 164 vollständige Bildprüfungen und alle 17 Tests. Im gezielten Laborfall entstehen rund 92,3 % weniger Gruppenkopien. Ein Agent prüfte Lebensdauer und Publikationsregeln. Der anschließende Spielvergleich bestätigt rund 41,1 % weniger Erfassungszeit und 66,2 % weniger Queue-Freigabezeit je Capture-Versuch gegenüber der neuen Kopierkontrolle. Das feste [Testpaket mit Anleitung](artifacts/render-worker-bridge-v8/README.md) ist installiert; `off` ist bestätigt. Die Kopierkontrolle entspricht nicht exakt Version 7; ein direkter Versionsgewinn wird nicht behauptet.

**Eigene Renderpakete umgesetzt:** Der vorherige Backend kopiert Konstanten in eigenen Speicher, bündelt Ressourcenreferenzen und zeichnet wechselnde Material-/Pipelinezustände auf 1/2/4/6 Workern auf. 753 vollständige Bildprüfungen bestehen. Große Pakete reduzieren Main-Thread-Zyklen, zeigen gegenüber einer seriellen Datenbasis ohne neue Übergabekosten aber noch keinen verlässlichen Gesamtlaufzeitgewinn. [Implementierung, Messung und Integrationsgrenzen](research/OWNED-RENDER-001.md).

**Uncapped-Referenz gemessen:** `UncappedBenchmark` ist installiert und hebt Engine-/DXGI-Limits mit angepassten Physikzeitbudgets auf. 3.535 gültige Frames ergeben **236,04 Presents/s**. Der angebundene Renderpaket-Pfad umfasst 32,41 % der Main-Thread-CPU-Stichproben inklusive Unteraufrufen; GPU Busy beträgt im Mittel 4,02 ms. Dies ist Uncapping, noch kein Multicore-Gewinn. [Live-Messung und nächster Eingriff](research/UNCAPPED-001.md), [Anbindung und Grenzen](docs/UNCAPPED-BENCHMARK.md).

**Aktueller Schwerpunkt:** Render-Vorbereitung vom Main Thread auf Worker verlagern. Die bisherigen Traces zeigen sowohl überlastete bestehende Engine-Worker als auch einen teuren seriellen Render-Pfad. Skyrims untersuchter D3D11-Gerätepfad setzt bereits keine Single-Thread-Einschränkung; die Hürde sind gemeinsame Engine-Zustände und Abhängigkeiten. [Breitere Untersuchung](research/MAIN-THREAD-004.md).

**Neuer getesteter Baustein:** Ein eigener D3D11-Backend zeichnet Renderbefehle auf bis zu sechs Worker-Threads auf und führt sie geordnet aus. Zwei Hardwareläufe bestehen insgesamt 744 Bildprüfungen ohne Abweichung und ohne Debugmeldungen. Bei großen synthetischen Paketen sinken Main-Thread-Arbeit und CPU-Abgabezeit; kleine Pakete werden langsamer. Dieser Backend verwendet ein eigenes Gerät und parallelisiert noch keine Skyrim-Renderfunktion. Ein FPS-Gewinn im Spiel ist nicht nachgewiesen. [Implementierung und Messgrenzen](research/D3D11-PARALLEL-LAB.md).

**Vorherige Render-Diagnose:** `RenderPassProbe` Version 1 wurde erfolgreich live gemessen. Beim Installieren der neuen Bridge wurde die Diagnose-DLL gesichert und deaktiviert, damit deren Code-Prüfung den unveränderten Original-Renderpass sieht. [Vorherige Anbindung und Tests](docs/RENDER-ANBINDUNG.md).

**Erste Live-Aufnahme:** 3.144.070 Renderpaket-Aufrufe ausschließlich auf dem Main Thread, 196.505 gültige Stichproben ohne Verlust oder Abbruch. Häufige gemeinsame Materialzustandsänderungen und sehr kurze Einzelaufrufe sprechen für getrennte Zustände und größere Pakete statt eines Worker-Auftrags pro Aufruf. [Live-Befund](research/LIVE-RENDER-001.md).

**Separater Bewegungskandidat:** Version 3 des zustandslosen Bewegungsscans ist installiert und vollständig live verglichen: 5.875 gültige Stichproben ohne Ergebnisabweichung, darunter 3.334 lange Listen bis maximal 56.826 Einträge. Labor und Live-Zeitwerte belegen bisher keinen Laufzeitgewinn. Die Originalsuche entscheidet weiterhin; der zusätzliche Scan ist nach Aufnahmeende inaktiv. Ein korrekt aktualisierter Typindex würde zusätzliche Prüfung seiner Mutations- und Lebensdauerregeln benötigen. [Prototyp und Grenzen](docs/MOVEMENT-FAST-PROTOTYP.md), [Live-Befund](research/LIVE-MOVEMENT-003.md).

**Frühere Sichtbarkeitsdiagnose:** Der C++20-Multicore-Kern und das SKSE-Diagnose-Plugin sind gebaut und getestet. SKSE 2.3.1 sowie das Plugin wurden lokal installiert; Skyrim bestätigt das erfolgreiche Laden und die Anbindung der geprüften Sichtbarkeitsfunktion. Dieser Diagnoseadapter zeichnet echte Engine-Eingaben und -Ergebnisse auf und vergleicht sie mit vier Worker-Threads. Die originalen Sichtbarkeitsentscheidungen bleiben erhalten; daraus folgt keine FPS-Steigerung. Die inzwischen umgesetzte RenderWorkerBridge ersetzt dagegen geeignete echte Draws wie oben beschrieben.

- [Machbarkeitsanalyse](docs/MACHBARKEIT.md): Quellen, Möglichkeiten, Grenzen und erster Prototyp.
- [Main Thread und Renderer](research/MAIN-THREAD-004.md): geprüfter Gerätecode, gemeinsame Zustände, weitere Engine-Kandidaten und Integrationsgrenzen.
- [Paralleler D3D11-Backend](research/D3D11-PARALLEL-LAB.md): echte Hardware-Befehlsaufzeichnung auf 1/2/4/6 Workern mit unabhängiger Bildprüfung.
- [Wiederholung und normale Spielszene](research/RENDER-TEST-001.md): 1.488 weitere Bildprüfungen; normale Szene mit 60,55 Presents/s und dominanter Framerate-Warteschleife. Noch kein paralleler Renderer im Spiel.
- `tools/Audit-RenderThread.py`: versionsgebundene Offline-Auswertung von Import-Aufrufkandidaten und vorhandenen Main-Thread-CPU-Stichproben.
- [Messplan](docs/MESSPLAN.md): reproduzierbare Szenen, CPU-/GPU-Abgrenzung und Erfolgskriterien.
- [Multicore-Prototyp](docs/MULTICORE-PROTOTYP.md): implementierter Rechenkern, Labormessung und Schritte zur Engine-Anbindung.
- [Engine-Anbindung](docs/ENGINE-ANBINDUNG.md): Diagnose-Plugin, Versionsprüfung, Aufnahme und noch offene Parallelisierung.
- [Vollständiger Live-Vergleich](research/LIVE-CULLING-001.md): 68.739 echte Engine-Ergebnisse, keine Abweichungen; der erfasste Pfad wird bereits von sechs Skyrim-Threads aufgerufen.
- [NPC-Stresstest](research/NPC-STRESS-001.md): Wiederholung mit 200 zusätzlichen Wachen, 60,48 Presents/s, mehr Arbeit auf vorhandenen Workern und ein serieller Render-Kandidat.
- [Ruckelnde Außenszene](research/NPC-STRESS-002.md): 600 gemeldete zusätzliche Wachen nahe dem Tor, 10,96 Presents/s; Main Thread wartet auf Jobs, sechs Worker werden von einer Suche nach New-Path-Bewegungsnachrichten dominiert.
- [Stressszene mit SKSE](research/NPC-STRESS-003.md): 600 gemeldete Wachen, 29,31 Presents/s bei verändertem Lastprofil; 68.889 Sichtbarkeitsvergleiche ohne Abweichungen. Kein nachgewiesener Leistungsgewinn durch SKSE oder das Plugin.
- [Bewegungsnachrichten-Diagnose](docs/MOVEMENT-DIAGNOSE.md): installierter SKSE-Baustein für Aufrufzahlen, Dauer und unter Original-Lock beobachtete Listenlängen; 4.026 Originalcode-Aufrufe im Labor.
- [Erster Live-Test der Bewegungsnachrichten-Suche](research/LIVE-MOVEMENT-001.md): 384.888 Originalaufrufe, 6.016 gültige Stichproben, sehr lange Listen und häufige negative Suchen auf mehreren Threads.
- [Auswertung der erfolgreichen Wiederholung](research/BASELINE-002.md): Qualität, Frames, Thread-Lasten und Bestätigung der Warteschleife im Code von Skyrim 1.7.104.
- [Erste Aufnahme](docs/AUFNAHME.md): vorbereitete Aufzeichnung von CPU-/GPU-Trace und Frames mit einmaliger UAC-Bestätigung.
- `tools/Collect-Setup.ps1`: liest Spielversion, Hash, sichtbare Plugins und relevante Einstellungen; schreibt nur einen Projektbericht.
- `tools/Sample-Threads.ps1`: misst die CPU-Zeit einzelner Threads eines bereits laufenden Prozesses. Keine Injection, keine Änderungen am Spiel.
- `research/local-setup.json`: lokal ermittelte Ausgangsdaten; kein Leistungsnachweis.

Aus dem Projektverzeichnis in PowerShell:

```powershell
.\tools\Collect-Setup.ps1
# Erst im Spiel in einer reproduzierbaren Szene starten:
.\tools\Sample-Threads.ps1 -DurationSeconds 30 -Label whiterun-baseline
```

Der Thread mit der höchsten CPU-Zeit ist damit noch nicht als Main Thread identifiziert. Dafür brauchen wir einen Trace mit Aufrufstapeln und zeitlichem Bezug zur Frame-Ausgabe. Details im Messplan.
