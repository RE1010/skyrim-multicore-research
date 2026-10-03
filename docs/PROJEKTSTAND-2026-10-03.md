# Bisher erledigt: Skyrim-Multicore-Projekt

Stand: 3. Oktober 2026. Ziel ist, geeignete Engine-Arbeit vom Main Thread auf zusätzliche CPU-Kerne zu verlagern und dabei die Bildrate bei korrekter Darstellung zu verbessern.

**Echte Renderarbeit läuft bereits auf vier eigenen Worker-Threads. Ein nutzbarer Leistungsgewinn gegenüber dem Originalrenderer ist bisher nicht erreicht.** Version 8 ist installiert und live getestet; die Ersetzung ist wieder ausgeschaltet. Zwei Vergleichspaare zeigen im Mittel 25,7 % mehr Bildausgaben durch geteilte Zustandsgruppen innerhalb des Worker-Pfads. Der Originalrenderer bleibt deutlich schneller.

## Untersuchung und Messgrundlage

- Die lokale Installation, Hardware und Entwicklungswerkzeuge wurden untersucht. Ziel ist die konkret geprüfte SkyrimSE.exe **1.7.104.0**, mit Intel Core i5-14400F und NVIDIA RTX 5060 Ti.
- SKSE **2.3.1** wurde heruntergeladen, installiert und erfolgreich verwendet. Die Plugins prüfen Spielversion und EXE-Hash, bevor sie sich anbinden.
- CPU-Aufnahmen mit Windows Performance Recorder und Bildausgabemessungen mit PresentMon wurden eingerichtet. Berichte speichern Prozessidentität, Messbedingungen, Framezeiten und Trace-Verluste.
- Normale Szenen in und vor Weißlauf sowie Belastungsszenen mit zusätzlichen Wachen wurden untersucht. Der 600-Wachen-Test zeigte, dass bereits vorhandene Skyrim-Worker teuer arbeiten und der Main-/Frame-Thread auf Jobs wartet. Mehr NPCs belasten daher nicht ausschließlich den Main Thread.
- Ein serieller Renderpfad wurde als weiterer Kandidat identifiziert. Die untersuchte Geräteerzeugung enthält bereits keine D3D11-Single-Thread-Einschränkung. Es fehlt kein einfacher Schalter: Gemeinsame Zustände und die Reihenfolge der Engine-Arbeit müssen berücksichtigt werden.

## Erste Engine-Kandidaten

**Sichtbarkeit/Culling:** Ein C++20-Kern und ein SKSE-Diagnose-Plugin wurden gebaut. Echte Engine-Eingaben wurden gespeichert und seriell sowie auf vier Workern verglichen. Der vollständige Live-Vergleich enthält **68.739 Datensätze ohne Ergebnisabweichung**. Dieser Pfad wird bereits von mehreren Skyrim-Threads aufgerufen. Die Diagnose ersetzt die ursprünglichen Spielentscheidungen nicht und belegt keinen FPS-Gewinn.

**Bewegungsnachrichten:** Die bei hoher NPC-Last auffällige Suche wurde instrumentiert. Ein zustandsloser Suchprototyp wurde gebaut und live verglichen: **5.875 gültige Stichproben ohne Ergebnisabweichung**, darunter Listen mit bis zu 56.826 Einträgen. Ein Laufzeitgewinn wurde nicht nachgewiesen; die Originalsuche bleibt maßgeblich.

## Renderentwicklung und Anbindung

Ein eigener D3D11-Laborbackend wurde umgesetzt, der Befehle auf mehreren Workern aufzeichnet und geordnet abspielt. Vollständige Bildvergleiche prüfen Reihenfolge, wechselnde Materialien und Konstanteninhalte. Anschließend wurden eigene Renderpakete mit gesicherten Ressourcenreferenzen und kopierten Konstanten entwickelt.

Die **RenderWorkerBridge** bindet diese Arbeitsweise an den geprüften Skyrim-Renderpfad an. Sie übernimmt geeignete echte `DrawIndexed`-Befehle, hält deren Ressourcen fest, zeichnet sie auf vier getrennten Worker-Contexts auf und lässt den aufrufenden Thread die fertigen Listen in Originalreihenfolge abspielen. Nicht unterstützte Zustände bleiben im Originalpfad. Bei GPU-Änderungen werden bereits gesammelte Draws zuerst ausgeführt. Kleine Gruppen werden seriell aufgezeichnet.

Der erste Spieltest bestätigte mehr als **914.000 tatsächliche Worker-Draws während einer Aufnahme**. Damit ist die technische Auslagerung nachgewiesen. Gleichzeitig traten FPS-Verluste und Hell-dunkel-Flackern auf; die Umsetzung ist deshalb noch keine brauchbare Spieloptimierung.

| Bridge-Version | Erledigt |
| --- | --- |
| 1–2 | Engine-Anbindung, tatsächliche Draw-Ersetzung, korrigierte Thread-Zuordnung und begrenzter Vergleich beobachteter Constant-Bytes mit GPU-Daten. |
| 3 | Sofortige serielle, gesammelte serielle und parallele Wiedergabe als getrennte Diagnosemodi; zeitlich begrenzte Aktivierung. |
| 4 | Getrennte CPU-Zeitmessungen für Erfassung, Uploadkopien, Aufzeichnung, Worker-Warten und Wiedergabe. |
| 5 | Bereits hochgeladene unveränderliche Constant-Versionen werden pro Recorder wiederverwendet. Im Live-Intervall wurden rund 69,6 % wiederholter privater Uploads vermieden. |
| 6 | Getter-Cache: unveränderte Renderzustände werden nicht erneut beim Treiber abgefragt. Im Spiel rund 37,4 % weniger Getter pro erfasstem Draw und 30,9 % weniger Erfassungszeit. |
| 7 | Unveränderte Bindungen werden innerhalb einer Command List übersprungen. Eigener Kontrollmodus setzt weiterhin alle Bindungen. Im Labor und im Spiel geprüft; beim v8-Austausch gesichert. |
| 8 | Unveränderliche Zustandsgruppen werden zwischen Draws geteilt; geänderte Gruppen bekommen neue Versionen. Kontrollmodus kopiert jede Gruppe pro Draw. Im Labor und im Spiel geprüft, derzeit ausgeschaltet installiert. |

Drawargumente und Constant-Inhalte bleiben individuell. Ein gemeinsamer Zustandsblock darf ältere Draws nicht nachträglich verändern. Agenten haben unter anderem Cache-Invalidierung, numerische Bindungsargumente, Ressourcenlebensdauer und Testabdeckung unabhängig geprüft.

## FPS-Limit und bisherige Spielmessungen

Das Plugin **UncappedBenchmark** hebt die untersuchten Engine-/DXGI-Limits mit angepassten Physikzeitbudgets auf. Eine frühe Referenzaufnahme ergab 236,04 Presents/s. Das war ein Uncapping-Ergebnis, kein Multicore-Gewinn. Verschiedene Szenen und Sitzungen sind nicht unmittelbar miteinander vergleichbar.

Der frühere direkte Vergleich von Version 7 verwendet jeweils 15 Sekunden PresentMon-Aufnahme vor Weißlauf:

| Modus | Mittlere Bildausgaberate | Mittlere Framezeit |
| --- | ---: | ---: |
| Originalpfad mit vorhandener Diagnose | **119,26 Presents/s** | 8,39 ms |
| Worker mit vollständigen Bindungen | 32,94 Presents/s | 30,35 ms |
| Worker mit reduzierten Bindungen | **33,97 Presents/s** | 29,44 ms |

Im optimierten Intervall entfallen **68,2 % der Aufzeichnungs-Bindungen**. Alle drei Traces enthalten null verlorene Ereignisse und Buffer. Die rund 3,1 % höhere Rate gegenüber der Worker-Kontrolle ist ein einzelnes Paar; frühere kurze Paare waren uneinheitlich. Ein verlässlich bestätigter FPS-Gewinn oder eine Verbesserung gegenüber dem Originalrenderer folgt daraus nicht.

## Version 8: Laborprüfung und durchgeführter Spieltest

- Festes Paket mit DLL, INI, Hash-Manifest und Installationsanleitung erstellt: [RenderWorkerBridge v8](../artifacts/render-worker-bridge-v8/README.md).
- **17 von 17 Tests bestanden.** Vier Laborläufe mit beiden Besitzmodellen und mit/ohne Debug-Layer ergeben **164 vollständige Bildvergleiche ohne Abweichung**.
- Gezielt geprüft: temporäre Textur-/SRV-Besitzer werden freigegeben, bevor gesammelte Draws abgespielt werden; Besitzmodi wechseln bei gefüllter Queue; Konstanten ändern sich ohne erneutes Binding; hohe Slots, NULL-Bindungen und numerische Zustände bleiben korrekt.
- Im Laborfall mit überwiegend unveränderten Bindungen entstehen rund **92,3 % weniger Gruppenkopien**. Veröffentlichung und Queue-Freigabe werden nun getrennt gemessen.
- Die neue Kontrolle verwendet dieselbe Gruppenstruktur wie die optimierte Variante und ist kein exakter Nachbau der flachen Version-7-Snapshots. Die Laborzahlen belegen noch keinen Skyrim-FPS-Gewinn.

Anschließend wurde das feste Paket bei regulär beendetem Skyrim installiert. SKSE bestätigt Version 8. Der Nutzer stellte wieder Tor und Mauer vor Weißlauf bereit, ohne zusätzliche Wachen. Fünf genaue PresentMon-Aufnahmen zu jeweils 15 Sekunden vergleichen Originalpfad und zweimal beide Besitzmodelle, mit umgekehrter Reihenfolge im zweiten Paar:

| Modus | Mittlere Bildausgaberate | Mittlere Framezeit |
| --- | ---: | ---: |
| Originalpfad mit vorhandener Diagnose | **120,51 Presents/s** | 8,30 ms |
| V8-Kopierkontrolle, erstes / zweites Intervall | 33,91 / 33,14 Presents/s | 29,49 / 30,18 ms |
| V8 mit geteilten Gruppen, erstes / zweites Intervall | **41,56 / 42,69 Presents/s** | 24,06 / 23,42 ms |

Das ergibt 22,6 % und 28,8 % mehr innerhalb des Worker-Pfads, beim Mittelwert beider Raten **25,7 %**. Rund **88,5 % der Gruppenkopien** entfallen. Die Erfassungszeit sinkt gewichtet pro Capture-Versuch um **41,1 %**, die Queue-Freigabezeit um **66,2 %**. Der vorgesehene Kostenanteil wurde also tatsächlich reduziert. Vier feste Worker zeichnen in den beiden optimierten Intervallen zusammen 2.962.878 echte Draws auf. Alle fünf Traces enthalten null verlorene Ereignisse und Buffer; alle Adapterfehler bleiben null.

Die optimierte Rate liegt dennoch rund **65 % unter der Originalreferenz**. Zwei kurze Paare in einer Sitzung sind keine breite statistische Absicherung. Die Kamera wurde für die FPS-Messung vom Nutzer bereitgestellt und nicht kontinuierlich mitgeschnitten. Ein Versionsvergleich mit v7 wird wegen der geänderten Kontrollstruktur nicht behauptet. Vollständige Herkunft und CPU-Zeitbudgets: [V8-Messbericht](../measurements/20261003-v8-live-comparison/analysis.json).

Ein separat angekündigter zehnsekündiger Bildtest lieferte sechs eigene Beobachtungen mit bestätigtem Parallelmodus. Kein grober Hell-dunkel-Wechsel war sichtbar. Der Nutzer bestätigte anschließend ausdrücklich, dass das Flackern **auch bei Bewegung verschwunden war**. Damit wurde im aktuellen Test kein sichtbarer Bildfehler mehr beobachtet. Die eigenen Einzelbilder erfassen weder jede Zwischenänderung noch den vollständigen Bewegungsverlauf; die Nutzerbeobachtung ist keine automatische Bildgleichheitsprüfung. Nach dem Test wurde der Originalpfad wieder aktiviert und `off` bestätigt.

## Offen und nächster Schritt

Der zusätzliche Aufwand für Erfassung, Ressourcenverwaltung, Aufzeichnung und Synchronisation ist im Spiel noch zu groß. Shader-Vorbereitung, Material-/Sichtbarkeitsentscheidungen und geordnete Übermittlung bleiben teilweise seriell. Das Main-Thread-Problem ist insgesamt nicht gelöst.

Das früher gemeldete Flackern war im aktuellen Version-8-Test laut Nutzer **auch bei Bewegung nicht mehr sichtbar**. Eigene Bildschirmbeobachtungen zeigten ebenfalls keinen groben Hell-dunkel-Wechsel. Die genaue Ursache des früheren Fehlers ist noch nicht isoliert; weitere Szenen und längere Bewegungsprüfungen fehlen. Labor-Bildgleichheit und die aktuelle Beobachtung beweisen keine vollständige visuelle Korrektheit in sämtlichen Skyrim-Szenen.

Version 8 bleibt mit `Enabled=0` installiert. Als nächstes muss der verbleibende Abstand zum Originalrenderer genauer zugeordnet werden: Worker-Warten, serielle Teilgruppen, Command-List-Grenzen, Uploadarbeit und übrige Engine-/Treiberkosten. Die neuen Phasenwerte belegen den reduzierten Verwaltungsaufwand, erklären aber noch nicht sämtliche FPS-Verluste. Die positive Bildbeobachtung sollte in weiteren Szenen und längeren Bewegungsprüfungen bestätigt werden.

Details und Messherkunft: [Render-Bridge-Bericht](../research/RENDER-BRIDGE-001.md), [Sichtbarkeitsvergleich](../research/LIVE-CULLING-001.md), [NPC-Belastung](../research/NPC-STRESS-002.md), [Bewegungssuche](../research/LIVE-MOVEMENT-003.md), [Uncapping](../research/UNCAPPED-001.md).
w