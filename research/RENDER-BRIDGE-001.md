# RenderWorkerBridge — tatsächliche Draw-Ersetzung

Stand: 03.10.2026. Experimentelle Anbindung für das exakt geprüfte SkyrimSE.exe 1.7.104.0, SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`.

## Implementierter Eingriff

Startup-CALL RVA `1521289` → `155ff40`. Der Wrapper erhält fünf Argumente inklusive des fünften Win64-Stackarguments und gibt das ursprüngliche AL unverändert zurück. Die Originalfunktion bereitet weiterhin Skyrims Shader und globale Engine-Zustände auf dem aufrufenden Thread vor. Keine Skyrim-Packetzeiger werden an Worker weitergegeben.

Der Adapter ersetzt ausschließlich `DrawIndexed` auf dem geprüften Immediate-Context-Objekt innerhalb dieser Renderpass-Grenze. Die übrigen Draw-Varianten bleiben original. Übernommen werden tatsächliche VS-/PS-Pipeline-Zustände, alle 14 Constant-Buffer-Slots je Shader, 128 SRV-Slots und 16 Sampler je Shader, Vertex-/Indexbuffer, Topologie, acht Renderziele, Tiefenziel, Blend-/Depth-/Rasterzustände, Viewports und Scissors. COM-Referenzen sichern die Ressourcenlebensdauer.

Constant-Buffer-Inhalte stammen aus den echten CPU-Uploads (`Map`/`Unmap` und vollständige `UpdateSubresource`-Aufrufe). Jede Änderung erzeugt eigene unveränderliche Bytes. Unbekannte Inhalte und nicht unterstützte Ausschnitte fallen auf den Original-Draw zurück. Worker nutzen eigene dynamische Constant Buffer, getrennt nach ursprünglichem Buffer; zwei gleich große VS-/PS-Buffer werden nicht zusammengelegt.

Bei GPU-Kopien, Änderungen an Geometrie oder Texturen, Clear-Aufrufen, Renderzielwechseln, nicht unterstützten Draws und anderen GPU-Befehlen wird die vorherige Warteschlange zuerst vollständig abgespielt. Aktive Queries, Predication, GS/HS/DS, Stream Output, OM-UAVs und Shader-Klasseninstanzen verhindern die Ersetzung. Fremde schreibende Context-Aufrufe innerhalb eines Renderpasses spielen zuerst dessen Warteschlange seriell ab und verhindern weitere Ersetzung bis zum Passende. Zwischen vollständig abgeschlossenen Pässen darf der neue Aufrufer den Context übernehmen; Skyrim verwendet beim Laden andere Aufrufthreads. Die erste Version ist auf die geprüfte Installation beschränkt; Interop mit fremden Render-Mods oder extern geänderten Shared Resources ist nicht verifiziert.

Die Vtable wird nur für das eine Context-Objekt kopiert, mit 149 typisierten Weiterleitungen aus dem installierten Windows-SDK. Der Code verändert nicht die gemeinsam genutzte D3D11-Vtable. `ID3D11DeviceContext4` muss dieselbe Objektadresse zurückliefern. Der tatsächliche Context liegt in RVA `3331f30` und in `Renderer + 40h`; RVA `3330198` verweist hingegen auf den Fensterhandle-Slot und ist kein Context.

Ab 16 geeigneten Draws zeichnen vier Worker jeweils zusammenhängende Teilbereiche auf. Alle Listen müssen erfolgreich fertig sein, bevor der Main Thread sie in Originalreihenfolge mit `ExecuteCommandList(..., TRUE)` abspielt. Kleine Gruppen werden seriell über denselben Snapshot-Pfad ausgeführt und getrennt gezählt. Bei einem Worker-Aufzeichnungsfehler wird vor jeder Wiedergabe seriell zurückgefallen. Falls auch dieser Pfad nicht mehr funktioniert, wird ein fataler Gerätefehler ausgelöst; unterdrückte Draws werden nicht still verworfen.

## Nachweise

`measurements/20261003-014722-516-render-bridge-tests/`: jeweils elf vollständige 256×256-Bilder mit und ohne D3D-Debug-Layer. Je Lauf 21.568 ersetzte Draws, 21.371 davon auf echten Workern; keine Bildabweichungen, keine Debug-Meldungen oder Fehler. Tests enthalten Konstantenänderungen zwischen Draws, GPU-Kopien, unterschiedliche Buffer an VS-Slot 3 / PS-Slot 7, DEFAULT-Buffer-Updates, veränderliche Geometrie, nicht indizierte Draws, unbekannte Inhalte, Queries und unveränderten Immediate-Context-Zustand. Weitere Bilder prüfen Threadwechsel zwischen Pässen, einen fremden GPU-Zugriff innerhalb eines Passes und danach wieder aktive Worker-Ersetzung. Ein ausführbarer CALL mit dem echten SKSE-Tailstub prüft fünf Argumente, genau einen Originalaufruf und AL `a5h`.

CTest: 16/16 bestanden, einschließlich EXE-/Runtime-Abweisung der DLL.

## Erster Spieltest: echte Auslagerung, aber nicht brauchbar

Aufnahme `measurements/20261003-014925-716-baseline`, PID 46788: 16 gültige Beobachtungen, während der Aufnahme 914.544 tatsächliche Worker-Draws und 167.939 serielle Snapshot-Draws. Vier eigene Worker-IDs: 25720, 47068, 29536, 40420. Keine gemeldeten Adapterfehler, keine verlorenen ETW-Ereignisse oder Puffer. 415 Presents ergeben 27,87 Presents/s, mittlere Frametime 35,88 ms. Die vier zusätzlichen Worker belegen jeweils etwa 10,7–11,0 % eines logischen Prozessors. CPU-Scheduling des Gesamtprozesses: etwa 1,95 logische Prozessoräquivalente.

Der Nutzer beobachtete hell-dunkel wechselnde Oberflächenfarben, etwa auf einer Mauer. Das Flackern verschwand nach dem Ausschalten der Worker-Ersetzung. Deshalb ist der Spieltest trotz echter Auslagerung **keine korrekte oder brauchbare Optimierung**. Der genaue Renderfehler ist noch nicht behoben; der Snapshot-/Bindungsaufwand ist zusätzlich zu hoch. Der Main-Thread-Stack enthält etwa 25,1 % D3D11-Stichproben und 40,1 % nicht symbolisierte Stichproben; Letztere dürfen ohne Symbolzuordnung nicht pauschal einem Teil des Adapters zugeschrieben werden.

Der Modus `verify-constants` belässt alle Draws im Originalpfad und vergleicht maximal 64 bekannte CPU-Constant-Buffer-Snapshots mit tatsächlichen GPU-Bytes über Staging-Readback. Das ist eine begrenzte Fehlerdiagnose, keine Optimierung; Readback kann kurzzeitig bremsen. Im Spiel wurden 64 Vergleiche ohne Byteabweichung oder Fehler abgeschlossen (`measurements/20261003-021002-094-live-gpu-constants`). Das bestätigt die geprüften Uploaddaten, aber weder sämtliche Buffer noch deren spätere Bindung oder die vollständige Renderzustandsübernahme. Danach wurde die Prüfung ausgeschaltet.

Version 8 ist installiert und startet mit `Enabled=0`. Aktuelles Manifest: `measurements/bridge-installation-20261003-150331-337/manifest.json`; Version 7 wurde dabei gesichert. Die erste Version (`bridge-installation-20261003-014155-232`) hat beim Laden aufgrund des konservativen Thread-Schutzes alle Draws original belassen; der oben dokumentierte Spieltest folgte nach der Korrektur. Die frühere `RenderPassProbe.dll` wurde beim ersten Installieren gesichert und entfernt, weil ihr Eingriff die nun vollständig geprüfte Originalfunktion verändert. Movement-Diagnose und UncappedBenchmark bleiben installiert.

## Vergleichsmodi zur Fehlerisolierung

Version 3 enthält zwei zusätzliche Kontrollen mit denselben Snapshots und privaten Constant Buffern wie der Worker-Pfad:

- `inline`: Jeder ersetzte Draw wird sofort auf dem aufrufenden Thread über einen Deferred Context aufgezeichnet und abgespielt. Es entsteht keine spätere Sammelwiedergabe und keine Worker-Aufzeichnung.
- `serial`: Die Draws werden gesammelt und in Originalreihenfolge auf dem aufrufenden Thread aufgezeichnet und abgespielt. Die Sammelgrenzen entsprechen dem Worker-Pfad; die Worker zeichnen nichts auf.
- `parallel`: Dieselben gesammelten Draws werden bei ausreichender Gruppengröße auf vier Workern aufgezeichnet.

Die Steuerdatei benötigt für jeden Ersetzungsmodus eine Ablaufzeit auf Basis von `GetTickCount64`. `Set-RenderBridgeMode.ps1` setzt standardmäßig zehn Sekunden; nach Ablauf schaltet der Reporter spätestens beim nächsten 500-ms-Durchlauf auf `off`. Die Umschaltung wirkt an der nächsten Renderpass-Grenze. Prozess-ID, Startzeit und Sitzungsverzeichnis werden vor dem Schreiben geprüft. Der Bericht nennt den angeforderten Modus ausdrücklich, damit serielle Kontrollen nicht als Worker-Auslagerung ausgewertet werden.

Native Prüfung von Version 3: `measurements/20261003-120814-157-render-bridge-controls`, 26 vollständige Bilder in zwei Läufen, null Bildabweichungen, Fehler oder Debugmeldungen. Je Lauf 24.640 ersetzte Draws, 18.306 Worker-Draws, 66 GPU-Konstanten-Prüfungen und eine korrekt erkannte absichtliche GPU-Änderung. Zusätzlich geprüft: sofortige und gesammelte serielle Wiedergabe ohne Worker-Draws sowie Resource-MinLOD.

Ein Fehler bereits bei `inline` würde den Snapshot-/Wiedergabepfad eingrenzen. Eine saubere sofortige Wiedergabe bei fehlerhafter serieller Sammelwiedergabe würde verzögerte Ressourcen- oder Zustandsabhängigkeiten nahelegen. Erst wenn beide Kontrollen korrekt aussehen, lässt sich der Unterschied zur Worker-Aufzeichnung getrennt untersuchen. Diese Zuordnung ist ein Diagnoseplan, noch kein Befund.

Live-Kontrollen von Version 3, PID 48904, jeweils zehn Sekunden aktiviert und automatisch ausgeschaltet:

| Aufnahme | Ersetzte Draws | Worker-Draws | Beobachtung des Nutzers |
| --- | ---: | ---: | --- |
| `20261003-121022-334-live-inline-control` | 509.469 | 0 | Kein Flackern bei zunächst stiller Ansicht. |
| `20261003-121121-779-live-serial-control` | 849.448 | 0 | Flackern bei schneller Kamerabewegung gemeldet. |
| `20261003-121307-861-live-inline-motion-control` | 519.193 | 0 | Kein Flackern bei Kameraschwenks, auch im anschließenden Originalpfad keines. |
| `20261003-121728-480-live-serial-motion-control` | 814.201 | 0 | Sehr leichtes Flackern an der **Tür**, ausdrücklich nicht an der Weißlauf-Mauer. |

Alle vier Kontrollen hatten null gemeldete Adapterfehler. Ein FPS-Abfall wurde beobachtet, noch ohne synchronisierte FPS-Aufnahme für diese Kontrollen. Die Bildbeobachtungen sprechen für eine Untersuchung der Verzögerung; sie belegen weder die konkrete Ursache noch einen Fehler ausschließlich durch mehrere Threads. Die aktive temporale Kantenglättung (`bUseTAA=1`) und die veränderte Bildrate sind weitere zu trennende Einflüsse. Die Einstellung wurde nur gelesen, nicht verändert.

Version 4 ergänzt getrennte QPC-Zeitbudgets für Snapshot-Erfassung, Uploadkopien, serielle Aufzeichnung, Warten auf Worker und Command-List-Abgabe. Jeder Worker hat zusätzlich ein eigenes Aufzeichnungsbudget. Das sind verstrichene Zeiten auf der CPU-Seite: Worker-Zeiten können sich überlappen; Abgabezeit ist keine GPU-Ausführungsdauer. Andere Engine-Arbeit und weitere Adapterkosten sind darin nicht vollständig enthalten. `Capture-BridgeControl.ps1` speichert diese Berichte gemeinsam mit den unabhängig alle 500 ms publizierten Present-Zählern des Uncapping-Plugins. Daraus errechnete Presents/s sind näherungsweise und ersetzen keine ETW-Frametime-Aufnahme.

Native Prüfung von Version 4: `measurements/20261003-122346-445-render-bridge-phase-tests`, wieder 26 vollständige Bilder ohne Abweichung, Debugmeldung oder Fehler sowie CTest 16/16 bestanden.

## Live-Zeitbudgets von Version 4

PID 49760, derselbe vom Nutzer bereitgestellte Blickpunkt vor Weißlauf. Originalpfad, serielle Sammelwiedergabe und vier Worker wurden nacheinander mit angeforderter stiller Kamera gemessen. Die Present-Zähler sind unabhängig publiziert; die Raten sind näherungsweise, keine präzise ETW-Frametime-Messung. Die Beobachtungsintervalle enthalten nur geladene, uncapped Szene und den jeweils angeforderten Modus.

| Modus / Aufnahme | Intervall | Näherungsweise Presents/s | Worker-Draws |
| --- | ---: | ---: | ---: |
| Original / `20261003-122554-825-bridge-phase-off` | 12,84 s | 128,55 | 0 |
| Seriell / `20261003-122608-337-bridge-phase-serial` | 9,23 s | 38,25 | 0 |
| Vier Worker / `20261003-122654-196-bridge-phase-parallel` | 9,23 s | 30,23 | 754.516 |

In der seriellen Messung entfallen auf Snapshot-Erfassung 1.189 ms, erneute Aufzeichnung 2.800 ms und Command-List-Abgabe 176 ms. Geteilt durch die näherungsweise 353 Presents im Intervall sind das etwa 3,4 / 7,9 / 0,5 ms pro Bild. Hinzu kommen 433 ms Uploadkopien und weitere nicht getrennt instrumentierte Engine-/Adapterarbeit. Bereits im Originalpfad werden 4,88 GB Uploadbytes beobachtet und kopiert; dieses Budget beträgt 2.226 ms im 12,84-s-Intervall. Originalmodus der Bridge enthält also messbaren Diagnoseaufwand.

Vier Worker verringern im gemessenen Intervall die serielle Aufzeichnung auf 664 ms, während der Main Thread 1.196 ms auf Worker-Fertigstellung wartet. Snapshot-Erfassung bleibt mit 1.281 ms auf dem aufrufenden Thread; Command-List-Abgabe beträgt 246 ms. Jeder Worker zeichnet insgesamt etwa 816–853 ms auf. Die teilweise geringeren gemessenen Main-Thread-Budgets erklären die Gesamtbildrate nicht vollständig; insbesondere ist damit weder die GPU-Ausführungszeit noch sämtliche Driver-/Engine-Arbeit abgegrenzt. Der Nutzer meldete **flackernde Schatten** beim Worker-Test. Danach wurde wieder auf `off` geschaltet. Diese Version ist keine brauchbare Optimierung.

## Wiederverwendung unveränderter Uploadversionen

Version 5 vermeidet einen erneuten privaten Constant-Buffer-Upload, wenn derselbe Recorder dieselbe unveränderliche, vom Adapter besessene Byteversion bereits hochgeladen hat. Die Zuordnung bleibt pro ursprünglichem Buffer und pro Recorder getrennt. Eine neue Byteversion wird immer neu hochgeladen, auch wenn frühere Daten später wieder gebraucht werden. Getter-/Bindungslogik und Reihenfolge der Draws bleiben ansonsten unverändert; eine Behebung des Schattenfehlers wird damit nicht behauptet.

Native Nachweise: `measurements/20261003-123332-593-render-bridge-reuse-tests`, 32 vollständige Bilder in zwei Läufen ohne Abweichung, Fehler oder Debugmeldung. Je Lauf 6.033 vermiedene private Uploads, 33.967 tatsächliche private Uploads und 21.378 Worker-Draws. Neue Bilder vergleichen Original-, serielle und Worker-Wiedergabe mit lange unveränderten PS-Konstanten, wechselnden VS-Geometriedaten und späteren PS-Änderungen über Command-List-Grenzen hinweg. CTest 16/16 bestanden.

## Live-Vergleich von Version 5

PID 40616, gleiche vom Nutzer bereitgestellte Ansicht vor Weißlauf. Die Raten stammen weiterhin aus unabhängig publizierten Zählern und sind näherungsweise.

| Modus / Aufnahme | Intervall | Näherungsweise Presents/s | Worker-Draws |
| --- | ---: | ---: | ---: |
| Original / `20261003-123703-413-bridge-phase-off` | 12,85 s | 135,91 | 0 |
| Seriell / `20261003-123716-926-bridge-phase-serial` | 9,24 s | 36,36 | 0 |
| Vier Worker / `20261003-123730-321-bridge-phase-parallel` | 9,75 s | 37,73 | 742.258 |

Im Worker-Intervall wurden 2.293.587 private Uploads ausgeführt und 5.255.651 durch Wiederverwendung vermieden, rund 69,6 % der ansonsten nötigen privaten Uploadaufrufe. Die Snapshot-Erfassung benötigt 1.323 ms, serielle Aufzeichnung 716 ms, Worker-Warten 1.162 ms und Abgabe 271 ms; die vier Worker zeichnen jeweils 747–833 ms auf. Die Wiederverwendung reduziert wiederholte Uploadarbeit, reicht aber nicht für einen Gewinn gegenüber dem Originalpfad. Ein belastbarer Versionsvergleich mit Version 4 ist wegen unterschiedlicher Sitzungen und schwankender Referenzrate nicht nachgewiesen. Der Nutzer konnte den letzten Worker-Abschnitt nicht sicher beobachten; deshalb ist dessen Bildfehlerstatus zunächst ungeklärt.

## Eigene Bildbeobachtung

Der direkte Fensterzugriff über den Computer-Use-Plugin funktioniert. Skyrim wurde darüber aktiviert und eine Bildfolge mit Live-Berichten verknüpft. `measurements/20261003-125131-303-visual-observation/frames.json` enthält einen Screenshot vor dem synchronisierten Test, zwölf mit angefordertem Parallelmodus und drei spätere Screenshots im Originalmodus. Während der zwölf parallelen Beobachtungen stieg der Worker-Draw-Zähler um 801.259. Die Folge umfasst etwa zehn Sekunden mit einem medianen Abstand von 0,911 Sekunden. Ein grober Hell-dunkel-Wechsel an Tor oder Mauer war darin nicht sichtbar; schnelles Flackern zwischen den Aufnahmen bleibt unbeurteilt.

`tools/Analyze-BridgeVisual.py` schreibt eine beschreibende Auswertung fester Bildschirmbereiche nach `brightness-analysis.json`. Im Parallelabschnitt schwankt der mittlere gewichtete RGB-Helligkeitswert der oberen Türfläche zwischen 118,46 und 122,54, der linken Mauer zwischen 141,33 und 142,21 und der gewählten Schattenfläche am Boden zwischen 123,66 und 124,35 auf einer Skala von 0–255. Das ist keine kalibrierte Lichtmessung und kein Korrektheitsnachweis: JPEG-Kompression, TAA, Beleuchtung, Bewegung und unterschiedliche Aufnahmezeiten können Pixel verändern. Die wenigen späteren Originalbilder ergeben keine ausreichende statistische Kontrolle. Der Worker-Pfad wurde nach der Aufnahme ausgeschaltet.

Ein vorangegangener Versuch (`20261003-124441-505-bridge-phase-parallel`) wurde verworfen: Trotz aktiven Parallelmodus änderten sich Render- und Present-Zähler nicht. Das Spiel renderte in diesem Intervall keine neuen beobachtbaren Frames; eine geladene Szene allein genügt nicht. `Capture-BridgeControl.ps1` wartet nun vor dem Test auf wachsende Renderpass- und Present-Zähler und verwirft Aufnahmen ohne tatsächliche Arbeit. Bildaufnahmen erzeugen zusätzlichen Aufwand und werden nicht als unverfälschter FPS-Vergleich verwendet.

## Zustandsabfragen nur bei Änderungen

Version 6 ist gebaut, geprüft und installiert. Ein begrenzt eingesetzter Agent hat die Cache-Invalidierung unabhängig geprüft; Umsetzung und Hardwareprüfung erfolgten im Hauptauftrag.

Der Adapter hält 13 Gruppen tatsächlicher Getter-Ergebnisse vor. Beobachtete Setter markieren betroffene Gruppen ungültig; der nächste Draw liest diese erneut beim Runtime-Context. Es werden keine Bindungen anhand bloßer Setterargumente vorhergesagt. Output-, UAV-, Stream-Output-Bindungen, externe Command Lists, Context-Swaps, ClearState, Queries und nicht eingeordnete mutierende Methoden verwerfen konservativ sämtliche Gruppen. Auch Setter von anderen Threads werden berücksichtigt. Änderungen an SRVs markieren beide erfassten Shaderstufen, Outputs und die Eignungsprüfung erneut zur Abfrage. Dies berücksichtigt insbesondere [automatische Entbindungen beim Setzen eines Renderziels](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets).

Die unveränderlichen Constant-Buffer-Byteversionen werden unabhängig vom Binding-Cache **für jeden Draw neu** aus der Uploadbeobachtung aufgelöst. Aktuell gemappte, unbekannte oder unvollständig erfasste Konstanten erlauben keine Ersetzung. Draw-Anzahl, Indexstart und Basisvertex bleiben pro Draw getrennt. Das erneute Laden von COM-Referenzen und die volle Kapazität für Viewport-/Scissor-Getter werden ausdrücklich korrekt behandelt.

`parallel-uncached` führt denselben Worker-Pfad ohne Binding-Cache aus; `parallel` aktiviert den Cache. Beide Modi benötigen weiterhin eine befristete Steuerdatei und schalten danach auf `off`. Der GPU-Konstanten-Diagnosemodus verwendet keinen Binding-Cache. Neue Zähler: `snapshotGetters`, `stateGroupRefreshes`, `stateGroupReuses`.

Native Prüfung: `measurements/20261003-132429-175-render-bridge-cache-tests`, 48 vollständige Bilder in zwei Hardwareläufen, jeweils mit und ohne Debug-Layer. Pro Lauf 40.128 ersetzte Draws, davon 30.719 auf Workern; keine Bildabweichungen oder Adapterfehler. Ein gezielt erzeugter Output-/SRV-Konflikt führt in Original-, uncached- und cached-Variante zur gleichen tatsächlichen NULL-Bindung und zum gleichen Bild. Seine sechs erwarteten Debugwarnungen werden separat gezählt; weitere Warnungen führen zum Testfehler. Die restlichen Prüfungen enthalten keine Debugmeldungen. CTest 16/16 bestanden.

Im gezielten Fall mit konstanten Bindungen, wechselnden Buffer-Inhalten und Scissors sinken die Getter von 73.728 auf 3.096 (95,8 % weniger). Die CPU-seitige Erfassung ohne Debug-Layer benötigt in diesem einzelnen Kontrollpaar 1,1761 ms statt 5,5703 ms (78,9 % weniger). Diese Werte gelten für den gezielten Labortest, nicht für Skyrim insgesamt; sie sind weder eine wiederholte statistische Leistungsmessung noch ein FPS-Nachweis. Shader-/Materialwechsel verringern das nutzbare Cache-Potenzial. Worker-Aufzeichnung, volle Pipeline-Bindungen und Synchronisation bleiben weiterhin Kosten.

## Live-Vergleich des Binding-Caches

SKSE bestätigt Version 6 als korrekt geladen, PID 25192. Nach einer Originalreferenz wurden zwei kurze Kontrollpaare derselben DLL in der Reihenfolge uncached/cached/cached/uncached aufgenommen. Der Nutzer wurde gebeten, die Kamera stillzuhalten; die Ansicht wurde während der FPS-Intervalle nicht kontinuierlich mitgeschnitten. Bildaufnahmen wurden getrennt durchgeführt.

| Modus / Aufnahme | Intervall | Näherungsweise Presents/s | Worker-Draws |
| --- | ---: | ---: | ---: |
| Original / `20261003-132903-589-bridge-phase-off` | 12,86 s | 134,61 | 0 |
| Ohne Cache / `20261003-132933-221-bridge-phase-parallel-uncached` | 9,27 s | 35,94 | 694.230 |
| Mit Cache / `20261003-132958-191-bridge-phase-parallel` | 9,24 s | 38,41 | 740.881 |
| Mit Cache / `20261003-133051-378-bridge-phase-parallel` | 9,21 s | 39,50 | 736.704 |
| Ohne Cache / `20261003-133115-387-bridge-phase-parallel-uncached` | 9,76 s | 36,56 | 683.677 |

Die beiden Paare zeigen näherungsweise 6,9 % bzw. 8,0 % höhere Present-Raten mit Cache. Der Mittelwert der zwei Raten je Modus beträgt 38,96 gegenüber 36,25 Presents/s, rund 7,5 % mehr **innerhalb des Worker-Pfads**. Die Erfassung benötigt gewichtet nach erfassten Draws 1,647 µs statt 2,386 µs je Draw (30,9 % weniger); Getter sinken von 24 auf durchschnittlich 15,02 je Erfassung (37,4 % weniger). Vier unterschiedliche Worker zeichnen während der vier Intervalle insgesamt 2.855.492 Draws auf, ohne gemeldete Adapterfehler. Das sind kleine Kontrollpaare mit unabhängig publizierten Zählern, keine statistisch abgesicherte ETW-Frametime-Messung. Ein Gewinn gegenüber dem Originalrenderer besteht weiterhin nicht.

Zusammenfassung und Herkunft: `measurements/20261003-132429-175-render-bridge-cache-tests/live-comparison.json`. Eine separate eigene Bildfolge enthält acht Beobachtungen in 2,441 Sekunden mit angefordertem Parallelmodus; darin steigt der Worker-Zähler um 195.538 und die Cache-Wiederverwendung wird ebenfalls bestätigt. Tor und Mauer zeigen in den beobachteten Bildern keinen groben Hell-dunkel-Wechsel. Kurzes Flackern zwischen den Bildern und das früher gemeldete Flackern bei schnellen Kameraschwenks bleiben ungeklärt. Die Blickrichtung unterscheidet sich zwischen der ersten Fensterkontrolle nach dem Start und der späteren Tor-Bildfolge; daraus wird kein exakt fixierter Kameraverlauf während aller Messungen behauptet.

Nach jeder Messung sowie dem Bildtest wurde `off` bestätigt; Skyrim nutzt wieder den Originalpfad. Die verbleibenden Kosten liegen unter anderem in der pro Draw kopierten Snapshot-Struktur, den vollständigen Worker-Bindungen, Command-List-Grenzen und Synchronisation. Die anschließende Version 7 setzt die unten dokumentierte Reduktion redundanter Worker-Bindungen um; diese Bindungen sind damit noch nicht als Ursache sämtlicher übrigen FPS-Verluste nachgewiesen.

## Weniger redundante Bindungen bei der Aufzeichnung

Version 7 ist gebaut, geprüft und installiert; SKSE bestätigt das Laden als Version `00000007`, PID 4640. Zwei begrenzt eingesetzte Agenten prüften read-only die Zustandsregeln und die sinnvollen Bildtests. Der Hauptauftrag führte Umsetzung, Hardwareprüfungen und Messvorbereitung aus; eine zweite kurze Gegenprüfung fand keine konkreten Fehler im neuen Recorder.

Jeder Aufruf von `Recorder::record` beginnt mit `ClearState` und einem ungültigen lokalen Binding-Cache. Der erste Draw setzt den vollständigen Zustand. Folgende Draws vergleichen Ressourcen und sämtliche zugehörigen numerischen Setterargumente: VB-Strides und Offsets, Indexformat und Offset, Blend-Faktoren und Sample-Mask, Stencil-Referenz, Viewport-/Scissor-Anzahl und aktive Einträge. Drawargumente werden weiterhin pro Draw ausgegeben. Der Cache überlebt keine Command-List-Grenze; [FinishCommandList(FALSE)](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-finishcommandlist) setzt den Deferred Context auf seinen Standardzustand zurück.

Outputs und beide SRV-Gruppen werden konservativ gekoppelt: Ändert sich eine der Gruppen, wird zuerst das Output-Set und danach beide SRV-Sets erneut gesetzt. Ein tatsächlicher Output-Wechsel erzwingt zudem das erneute Setzen von VB/IB. Somit wird eine implizite Entbindung von Ressourcen nicht mit einem unveränderten Sollzustand verwechselt. Private Constant-Buffer-Uploads laufen weiterhin vor jedem Draw über den bisherigen Versionscache. Ein neuer WRITE_DISCARD-Upload erzwingt die Bindung jeder betroffenen Shaderstufe, auch bei unverändertem Bufferzeiger und einem von VS/PS gemeinsam verwendeten Buffer.

`parallel-full-bindings` ist die direkte Kontrolle: Getter-Cache und Upload-Wiederverwendung bleiben aktiv, alle bisherigen Aufzeichnungs-Bindungen werden gesetzt. `parallel` überspringt redundante Aufzeichnungs-Bindungen. `parallel-uncached` deaktiviert weiterhin ausschließlich den Getter-Cache; es ist nicht die Kontrolle für diese neue Optimierung. Alle Ersetzungsmodi bleiben befristet, und die INI startet weiterhin ausgeschaltet. Neue Zähler: `recordingBindings`, `recordingBindingsSkipped`.

Native Prüfung: `measurements/20261003-135202-871-render-bridge-binding-tests`, 66 vollständige Bilder in zwei Hardwareläufen mit/ohne Debug-Layer, pro Lauf 61.632 ersetzte Draws und 46.079 tatsächliche Worker-Draws. Keine Bildabweichungen, Adapterfehler oder unerwarteten Debugmeldungen; die sechs absichtlich erzeugten Output-/SRV-Hazard-Warnungen sind separat bestätigt. CTest 16/16 bestanden. Die Materialkontrolle mit 3.072 Draws benötigt 23.040 statt 55.296 Aufzeichnungs-Setter, 58,3 % weniger Aufrufe. Dies ist ein Aufrufzahlvergleich im Labor, kein gemessener Skyrim-FPS-Gewinn.

Neue vollständige Bilder vergleichen native, serielle, Worker- und vollständige Worker-Bindungen. Pixelwirksame Ressourcen an t5/t97 und Sampler an s3/s11 wechseln einschließlich NULL-Zuständen; ein PS-Buffer an b7 wird entfernt und wieder gebunden, ohne einen fehlenden vom aktiven Shader verlangten Buffer künstlich als gültige Szene zu behandeln. Derselbe VB/IB wechselt Stride/Offset und Indexformat/Offset; derselbe Blend-State nutzt unterschiedliche Blend-Faktoren. Viewport-Werte ändern sich bei gleicher Anzahl. Die bestehenden Bilder für Konstantenänderungen ohne Original-Rebinding, unterschiedliche gleich große VS-/PS-Buffer, Command-List-Grenzen und automatische Entbindungen bestehen ebenfalls.

## Spielvergleich der Aufzeichnungs-Bindungen

Zusammenfassung: `measurements/20261003-135202-871-render-bridge-binding-tests/live-comparison.json`. Die erste Originalreferenz und zwei kurze Zählerpaare vor Weißlauf ergeben 121,56 Presents/s im Original sowie 35,02/38,04 und 35,49/35,12 für vollständige/reduzierte Bindungen. Das erste Paar begünstigt die neue Aufzeichnung, das zweite zeigt praktisch Gleichstand mit leicht umgekehrter Richtung. Zähler werden unabhängig alle 500 ms publiziert; diese Raten ersetzen keine genaue Framezeitmessung.

Die nachfolgende Kontrolle verwendet PresentMon 2.6.0 und jeweils 15 Sekunden Aufnahme. Der Nutzer bestätigt Tor und Mauer ohne Menü; die Kamera wird während dieser Intervalle nicht kontinuierlich mitgeschnitten. Alle drei Traces haben null verlorene Ereignisse und null verlorene Buffer. Vier feste Worker-IDs und wachsende Worker-Draw-Zähler bestätigen tatsächliche Ersetzung; alle Adapterfehlerzähler bleiben null. Die Kontrolle für vollständige Bindungen weist keine übersprungenen Setter auf.

| Modus / Aufnahme | Framezahl | Presents/s | Mittlere Framezeit | p95 Framezeit | Mittlere GPU Busy |
| --- | ---: | ---: | ---: | ---: | ---: |
| Vollständige Bindungen / `20261003-140830-937-baseline` | 492 | 32,94 | 30,35 ms | 33,92 ms | 5,00 ms |
| Reduzierte Bindungen / `20261003-140937-137-baseline` | 507 | 33,97 | 29,44 ms | 34,47 ms | 4,94 ms |
| Original / `20261003-141045-748-baseline` | 1.785 | 119,26 | 8,39 ms | 10,60 ms | 3,93 ms |

Das einzelne genaue Paar zeigt 3,1 % höhere Present-Rate, aber keinen entsprechend besseren p95-Wert. Wegen der gemischten vorherigen Paare und fehlender Wiederholung ist dies kein statistisch gesicherter FPS-Gewinn. Im reduzierten Intervall zeichnen Worker 1.230.949 Draws auf; 17.850.447 von 26.161.074 Aufzeichnungs-Bindungen entfallen (68,2 %). Gewichtet über die kurzen Zählerpaare sinkt die Worker-Aufzeichnungszeit von 3,944 auf 3,309 µs pro Worker-Draw (16,1 %); die serielle Aufzeichnung sinkt von 5,575 auf 5,107 µs (8,4 %). Diese CPU-seitigen Phasenzeiten können überlappen und sind keine GPU-Laufzeiten. Originalpfad und Bridge haben weiterhin einen erheblichen Leistungsabstand.

Die CPU-Exporte des reduzierten Intervalls zeigen für die vier bekannten Bridge-Worker jeweils 6,9 % Laufzeit eines logischen Prozessors; der dominierende Thread 13012 liegt bei 89,0 %. Im Original liegt derselbe Thread bei 92,1 %. Die Rangfolge allein beweist dessen Main-Thread-Rolle nicht. CPU-Samplegewichte des reduzierten Intervalls: Bridge 6,57 s, Skyrim 5,96 s, D3D11 5,11 s, Nvidia-User-Treiber 4,97 s in einem ETL-Intervall von 15,70 s. Dies sind Gewichte über sämtliche Threads, keine addierbaren Framephasen und keine Identifikation einer einzelnen teuren Funktion. Die kleine Bindungsoptimierung löst den seriellen Engpass nicht.

Ein separater zehnsekündiger Bildtest lieferte sechs eigene Beobachtungen mit etwa einer Sekunde Abstand. Kein grober Hell-dunkel-Wechsel war sichtbar; die Kamera wurde in den ersten zwei Bildern nachgerichtet. Der Worker-Zähler stieg über den begrenzten Test um 866.590. Es wurde nicht pro Bild ein Plugin-Snapshot gespeichert, daher ist nicht jedes Bild dem noch aktiven Ersetzungsmodus sicher zugeordnet. Schnelles Flackern und die frühere Schatten-/Türabweichung bleiben ungeklärt. Die Beobachtung wird ausdrücklich nicht als bestandene visuelle Skyrim-Prüfung gewertet. Nach dem Test ist `off` bestätigt.

Die Anschlussprüfung des Agenten benannte den nächsten gezielten Kandidaten: `d=state` kopiert pro Draw einen Snapshot mit mehreren hundert COM-Referenzplätzen (364 Slots, teils NULL); `queued.clear()` gibt die Draw-Referenzen nach Worker-Abschluss und Wiedergabe auf dem aufrufenden Thread frei. Gemeinsam genutzte, unveränderliche Binding-Gruppen sollten diesen Aufwand reduzieren. Drawargumente und Constant-Byte-Versionen müssen pro Draw bleiben, und ältere Gruppen müssen bis zum Worker-Abschluss leben. Die unten beschriebene Version 8 misst Capture- und Queue-Abbaukosten getrennt und vergleicht bei identischen Flush-Grenzen. Der nachfolgende Spielvergleich bestätigt die Kostenreduktion innerhalb der neuen Struktur.

## Version 8: gemeinsam genutzte unveränderliche Zustandsgruppen

Zunächst für einen späteren Test gebaut und im Labor geprüft, anschließend auf Nutzerwunsch installiert und live verglichen. Festes Paket: `artifacts/render-worker-bridge-v8`, DLL-SHA256 `D15A6E5D6CA0CDD0A87AA3A7C93B2784CDC59B0486CDAC6C5AE2F2F350A6C5B2`. SKSE bestätigt Version `00000008`, PID 38920; die INI startet mit `Enabled=0`. Das Paketmanifest dokumentiert den ursprünglichen Erstellungsstand; `live-test.json` ergänzt den späteren Spieltest. Laborherkunft: `measurements/20261003-143229-005-render-bridge-ownership-tests/analysis.json`.

Der mutable Getter-Cache bleibt beim Erfassungsthread. Jede der 13 Binding-Gruppen wird in einem `shared_ptr<const Group>` veröffentlicht und besitzt ihre COM-Referenzen. Neue Draws behalten diese Versionen. Ein eigener `unpublished_state`-Mask verhindert, dass vorzeitige Capture-Abbrüche eine noch nicht veröffentlichte Getteränderung verlieren. Nach einem Refresh vergleicht die Veröffentlichung tatsächliche Referenzen und vollständige numerische Argumente mit der bisherigen Gruppe: Erneut gesetzte, unveränderte Werte erzeugen keine Kopie. Fehlende Gruppen und ein Besitzmoduswechsel erzwingen neue Versionen. Kein Draw zeigt auf den weiter mutierten Getter-Cache.

Constant-Bytes werden anhand der veröffentlichten CB-Gruppe für jeden Draw frisch aus den beobachteten Upload-Versionen aufgelöst; NULL-Slots bleiben leer. Die Recorder vergleichen weiterhin Inhalte und numerische Argumente. Ungleiche Gruppenzeiger führen nicht automatisch zu zusätzlichen Settern. Unveränderte Flush-Grenzen: 256 Draws, Scope-Ende, GPU-Barrieren, Fallbacks und Thread-Konflikte. Die Queue wird erst nach Worker-Abschluss und geordneter Wiedergabe freigegeben.

`parallel` verwendet geteilte Gruppen; `parallel-owned-snapshots` kopiert alle 13 Gruppen für jeden Draw. Beide verwenden dieselbe neue Gruppenstruktur, dieselben Setterentscheidungen und dieselben Flush-Grenzen. Der Kontrollmodus ist wegen seiner Gruppenallokationen kein exakter Nachbau der flachen Version-7-Snapshots. Ein A/B-Vergleich isoliert die Wiederverwendung innerhalb der neuen Struktur; ein Vorteil gegenüber Version 7 und dem Originalrenderer benötigt eigene Messungen. Getter- und Setter-Kontrollen bleiben separat erhalten.

Neue Zähler: `sharedSnapshotBindings`, `snapshotGroupCopies`, `snapshotGroupReuses`, `snapshotPublishTicks`, `queueReleaseTicks`. Publication ist Teil von `captureTicks`; diese Zeiten dürfen nicht addiert werden. Queue-Freigabe wird nach der Wiedergabe gemessen und umfasst die Freigabe aller Draw-eigenen Byte-/Gruppenreferenzen. Die zusätzliche QPC-Instrumentierung verursacht ebenfalls Aufwand.

Vier vollständige Hardwareläufe (Shared/Owned jeweils mit/ohne Debug-Layer) ergeben 164 vollständige Bildprüfungen ohne Pixelabweichung, Adapterfehler oder unerwartete Debugmeldungen. Je Debug-Lauf sind sechs absichtlich erzeugte Hazard-Warnungen separat bestätigt; der bekannte ungetrackte GPU-Schreiber wird weiterhin erkannt. Jede Suite ersetzt 65.216 Draws, davon 49.663 auf Workern. CTest 17/17 bestanden; die Auswertung weist 17 ungültige Live-Evidenzfälle zurück. Ein Agent prüfte Publikationsmaske, Besitzregeln, Moduswechsel und Queue-Lebensdauer read-only und fand keine konkreten Fehler.

Neue pixelwirksame Szenen geben temporäre SRV-/Textur-Besitzer frei, während frühere Draws noch in der Queue liegen, und wechseln Shared → Owned → Shared unterhalb der Flush-Grenze. Beim letzten Wechsel werden Shader-, Geometrie-, CB- und SRV-Bindungen nicht erneut gesetzt. Native, Shared und Owned liefern identische vollständige Bilder. Bestehende Constant-only-, High-slot-, NULL-, Hazard- und numerische Tests laufen ebenfalls in beiden Besitzmodellen. Die Constant-only-Kontrolle bestätigt zusätzlich identische Binding- und Batchzahlen.

| Constant-only-Kontrolle, ohne Debug-Layer | Gruppen neu kopiert | Erfassung | Veröffentlichung, in Erfassung enthalten | Queue-Freigabe |
| --- | ---: | ---: | ---: | ---: |
| Owned, erster Lauf | 39.936 | 5,123 ms | 4,843 ms | 3,398 ms |
| Shared, erster Lauf | 3.074 | 0,764 ms | 0,498 ms | 0,575 ms |
| Owned, umgekehrte Reihenfolge | 39.936 | 4,662 ms | 4,381 ms | 3,334 ms |
| Shared, umgekehrte Reihenfolge | 3.085 | 0,766 ms | 0,498 ms | 0,537 ms |

Die Kontrolle umfasst jeweils 3.072 Draws mit überwiegend stabilen Bindungen und wechselnden Constant-Inhalten. Es entstehen 92,3 % weniger Gruppenkopien; die kleinen CPU-Zeitmessungen sind ein gezielter Laborbefund, kein statistisch gesicherter Skyrim-Leistungsnachweis. Die unterschiedliche Zahl von elf Kopien entsteht durch die bereits verfügbaren Gruppen beim Einstieg in die Kontrolle. Korrektheit der früheren Schatten-/Türabweichung, Spiel-FPS und der Main-Thread-Engpass bleiben offen. Das Paket startet ausgeschaltet und behält die bisherige Befristung von Ersetzungsmodi bei.

## Spielvergleich von Version 8

Auswertung mit reproduzierbarem Skript: [Messbericht](../measurements/20261003-v8-live-comparison/analysis.json). Fünf jeweils 15 Sekunden lange PresentMon-Aufnahmen, Reihenfolge Original → Kopierkontrolle → geteilte Gruppen → geteilte Gruppen → Kopierkontrolle. Der Nutzer bestätigte Tor und Mauer vor Weißlauf ohne Menü und zusätzliche NPC-Last. Die Kamera wurde während der FPS-Aufnahmen nicht kontinuierlich beobachtet; deshalb wird kein lückenlos nachgewiesener identischer Kameraverlauf behauptet. Alle Aufnahmen gehören zu derselben Spielsession, PID 38920, Startzeit 134355062115800952.

| Modus / Aufnahme | Frames | Presents/s | Mittlere Framezeit | p95 | Mittlere GPU Busy |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original / `20261003-150655-094-baseline` | 1.804 | 120,51 | 8,30 ms | 10,09 ms | 4,47 ms |
| Kopierkontrolle / `20261003-150815-301-baseline` | 506 | 33,91 | 29,49 ms | 33,08 ms | 7,16 ms |
| Geteilte Gruppen / `20261003-150930-923-baseline` | 621 | 41,56 | 24,06 ms | 28,10 ms | 6,96 ms |
| Geteilte Gruppen / `20261003-151139-070-baseline` | 638 | 42,69 | 23,42 ms | 26,23 ms | 7,10 ms |
| Kopierkontrolle / `20261003-151242-144-baseline` | 495 | 33,14 | 30,18 ms | 32,74 ms | 7,16 ms |

Die beiden Paare zeigen 22,6 % und 28,8 % höhere Raten mit geteilten Gruppen. Die Mittelwerte der beiden Raten sind 42,12 gegenüber 33,52 Presents/s, ein Vorteil von **25,7 % innerhalb des Version-8-Worker-Pfads**. Zwei kurze Paare in einer Sitzung sind keine breite statistische Absicherung. Der optimierte Pfad liegt weiterhin rund **65,0 % unter der Originalreferenz**; daraus folgt keine allgemeine Skyrim-FPS-Steigerung und kein direkter Vergleich zu Version 7.

Die optimierten Intervalle enthalten 1.404.916 und 1.557.962 tatsächliche Worker-Draws. Vier feste Worker-IDs: 12820, 1852, 37628, 49296. Die Kopierkontrollen bestätigen `sharedSnapshotBindings=false`, null Gruppenwiederverwendung und genau 13 neue Gruppen je ersetztem Draw. In den optimierten Intervallen werden rund 88,5 % der möglichen Gruppenkopien durch Wiederverwendung vermieden. Alle fünf Traces melden null verlorene Ereignisse und Buffer; alle Adapterfehler bleiben null, und während der gemessenen Intervalle wachsen keine fremden Context-Aufrufe.

Die getrennten QPC-Budgets bestätigen die beabsichtigte Kostenreduktion. Gewichtet über die Capture-Versuche der zwei Intervalle je Modus:

| CPU-Budget je Capture-Versuch | Kopierkontrolle | Geteilte Gruppen |
| --- | ---: | ---: |
| Erfassung | 3,035 µs | 1,787 µs |
| Veröffentlichung, bereits in Erfassung enthalten | 1,755 µs | 0,517 µs |
| Queue-Freigabe nach Wiedergabe | 1,503 µs | 0,508 µs |
| Warten auf Worker | 1,355 µs | 1,278 µs |
| Command-List-Abgabe | 0,370 µs | 0,351 µs |

Damit sinken Erfassung um 41,1 % und Queue-Freigabe um 66,2 %. Die Zustandsverwaltung war also ein relevanter Kostenanteil. Worker-Warten und Abgabe verändern sich pro Capture-Versuch deutlich weniger; sie sowie verbleibende Erfassung, Uploads, serielle Teilgruppen und übrige Engine-/Treiberarbeit bleiben Untersuchungskandidaten. Das identifiziert noch nicht die Ursache des gesamten Abstands zur Originalreferenz. Die Berichtszähler werden unabhängig publiziert und haben nicht exakt die PresentMon-Intervallgrenzen. Die Tabellenwerte sind CPU-seitige Normalisierungen pro Capture-Versuch; Worker-Zeiten überlappen, und Veröffentlichung darf nicht nochmals zur Erfassung addiert werden.

Ein separat angekündigter zehnsekündiger Bildtest lieferte sieben Fensterbeobachtungen, sechs davon mit bestätigt angefordertem Parallelmodus. Während dieser sechs Beobachtungen wächst der Worker-Zähler um 429.577; die aktive Bildfolge umfasst etwa 4,15 Sekunden. Metadaten: `measurements/20261003-v8-live-comparison/visual-observation.json`. Kein grober Hell-dunkel-Wechsel an Tor, Mauer oder Schatten war sichtbar. Der Nutzer bestätigte anschließend: „doch ist auch weg beim bewegen“. Im aktuellen Test wurde damit auch bei Bewegung kein Flackern mehr beobachtet. Die eigene Bildfolge prüft keine schnellen Kameraschwenks und Zwischenbilder; die genaue frühere Fehlerursache und eine Bestätigung in weiteren Szenen bleiben offen. Dies ist keine automatische visuelle Skyrim-Korrektheitsprüfung. Nach jedem Vergleich sowie dem Bildtest wurde `off` gesetzt; der abschließende Bericht bestätigt `requestedMode=off`, `enabled=false`, geladene Welt und null Fehler. Version 8 bleibt ausgeschaltet installiert.

## Live-Messung und Grenzen

`measurements/live-render-bridge/current-session.json` identifiziert PID und Startzeit. Der Sitzungsbericht zählt Original-Draws, ersetzte Draws, tatsächliche Worker-Draws, serielle Snapshots, Fehler und vier Worker-IDs getrennt. Für einen Live-Nachweis muss `workerRecordedDraws` während der geladenen Szene wachsen; eine bloß geladene DLL reicht nicht.

Umschalten im laufenden Test mit `tools/Set-RenderBridgeMode.ps1 -Mode off`, `-Mode inline`, `-Mode serial`, `-Mode parallel` oder `-Mode verify-constants`. Parallel derzeit wegen des Bildfehlers nur für kurze gezielte Diagnosen aktivieren. Der Originalmodus enthält weiterhin die Adapter-Weiterleitungen und Upload-Beobachtung; deshalb ist dieser A/B-Test kein völlig uninstrumentierter Vanilla-Vergleich. Normale Szene, feste Kamera und uncapped FPS verwenden. Messung mit `tools/Start-Baseline.ps1 -Scene ... -DurationSeconds 15 -RequireUncapped -BridgeMode parallel` bzw. `off` erfasst zusätzlich den Bridge-Zustandsverlauf. Für eine parallele Aufnahme einschließlich Vorlauf muss der Ersetzungsmodus ausdrücklich lange genug befristet werden, beispielsweise mit `-DurationSeconds 60`.

Die Ersetzung verschiebt nachweislich die D3D-Aufzeichnung geeigneter Draws auf Worker. Skyrims globale Shader-Vorbereitung, Sichtbarkeits-/Materialentscheidungen und geordnete GPU-Übermittlung bleiben zunächst auf dem Main Thread. Vollständige Zustandssnapshots und erneute Bindungen sind im ersten Spieltest zu teuer; zusätzlich besteht ein sichtbarer Bildfehler. Weder eine generelle Main-Thread-Auflösung noch ein FPS-Gewinn ist nachgewiesen.

Grundlage: [Microsoft: Immediate and Deferred Rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render), [Microsoft: ExecuteCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist). Worker benötigen getrennte Deferred Contexts; geordnete Wiedergabe erfolgt auf dem Immediate Context und TRUE stellt dessen Zustand wieder her.
