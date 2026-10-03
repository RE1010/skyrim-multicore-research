# Ruckelnde Außenszene: 600 zusätzliche Wachen nahe dem Tor

2. Oktober 2026, Europe/Berlin. Der Nutzer meldete sichtbares Laggen außerhalb der Stadt und präzisierte die Szene als „600 Wachen nahe Tor“. Eine zweite Aufnahme erfasst die Bildausgabe über fast das gesamte Intervall und zeigt einen deutlichen Einbruch. **Die vorhandenen Jobs sind teuer; der Main-/Frame-Thread wartet überwiegend auf sie.**

## Aufnahmebedingungen

Zwischen dem vorherigen 200-Wachen-Test und diesem Test wurde Skyrim neu gestartet. Aktuelle PID: 8148. Runtime 1.7.104, unveränderter EXE-SHA256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`.

In diesem laufenden Prozess sind **weder SKSE noch unser Diagnose-Plugin geladen**. Das ist durch die beim Aufnahmestart gespeicherte Modulliste belegt. Das aktuelle Culling-Diagnose-Verzeichnis gehört weiterhin zur alten PID 37216; dessen abgeschlossener Bericht ist kein Live-Bericht dieses neuen Prozesses. Der hier gemessene Einbruch ist deshalb kein Effekt einer laufenden parallelen Berechnung unseres Plugins.

Die erste Aufnahme (`measurements/20261002-214117-242-baseline/`, Start 21:41:30 Uhr) enthält 354 Frames mit ungefähr 9,78 Sekunden summierten Present-Intervallen, obwohl PresentMon 20 Sekunden lief. Der Nutzer beantwortete währenddessen eine Szenenfrage. Ein zeitweiliger Hintergrund-/Pause-Zustand ist plausibel, aber nicht durch einen Fokus-Recorder bestätigt. Median 32,47 ms und mittlere Present-Rate 36,20/s zeigen bereits langsame Frames; die CPU-Zeit über das ganze 20,99-Sekunden-Intervall darf aber nicht als durchgehend aktive Spielzeit behandelt werden.

Deshalb wurde ohne weitere Rückfrage während der Messung wiederholt: `measurements/20261002-214449-509-baseline/`, Aufnahmebeginn **21:45:02 Uhr**, 20 Sekunden PresentMon, 21,228035 Sekunden ETL einschließlich Aufnahmegrenzen. Der Nutzer sollte sofort nach der UAC-Abfrage zum Spiel wechseln, Konsole und Menüs schließen, Kamera und NPC-Zahl konstant halten. Anzahl und Ort stammen vom Nutzer; die Szene wurde während dieser Aufnahme nicht durch Screenshots oder Engine-Zähler unabhängig verifiziert.

## Ergebnis der vollständigen Wiederholung

| Merkmal | Messwert |
|---|---:|
| PresentMon-Zeilen | 217 |
| Mittleres Present-Intervall | 91,2331 ms |
| Median / P95 / P99 | 90,2757 / 103,8800 / 108,5503 ms |
| Rechnerische Present-Rate | **10,9609/s** |
| Mittlere GPU-Busy-Zeit | **9,1766 ms** |
| Mittlere Dauer im Present-Aufruf | 0,0968 ms |
| Verlorene Trace-Ereignisse / Puffer | **0 / 0** |
| PresentMon-Exit / WPR-Stop | 0 / 0 |

Die Bildausgabe erstreckt sich über ungefähr 19,72 Sekunden zwischen dem ersten und letzten erfassten Present. Zusammen mit 217 Frametimes deckt sie damit fast das gesamte 20-Sekunden-Intervall ab. Die Rechnerwerte sind Present-Metriken und nicht automatisch Zahlen einzigartig gerenderter oder tatsächlich angezeigter Bilder.

Etwa 91 ms zwischen Presents bei ungefähr 9 ms GPU-Busy sowie die nachfolgend belegten CPU-Stacks sprechen stark für einen Engpass im CPU-/Job-Pfad. Ein Diagramm einer GPU-Auslastung oder die Gesamt-CPU-Prozentzahl allein wäre kein ausreichender Nachweis. Die starke Verschlechterung gegenüber dem früheren Durchlauf ist beobachtet; ihre genaue Ursache, etwa die Entwicklung der Bewegungsszene oder einer Nachrichtenliste, ist noch nicht vermessen.

## CPU und Main-/Frame-Thread

Skyrim erhielt über den ETL-Zeitraum Laufzeit entsprechend **6,3000 gleichzeitig belegten logischen Prozessoren**, beziehungsweise 39,3752 % der 16 logischen Prozessoren. Das ist keine nach P-/E-Core-Durchsatz gewichtete Kapazitätsangabe.

| Thread-ID | Anteil eines logischen Prozessors |
|---|---:|
| 16320 | 86,9397 % |
| 41352 | 86,7223 % |
| 39428 | 86,6650 % |
| 36216, Main-/Frame-Kandidat | 86,3710 % |
| 30740 | 86,2493 % |
| 20888 | 86,1559 % |
| 41456 | 86,0296 % |
| 40084 | 16,9678 % |

Thread 36216 startete unmittelbar nach dem Prozessstart und weist Bootstrap-, D3D11-/Render- und Frame-Schleifen-Stacks auf. Das stützt die Zuordnung zum primären Main-/Frame-Thread. Seine symbolisierte Auswertung enthält **18.272 CPU-Samples**.

- `Kernelbase!SwitchToThread`: 12.666 inklusive Samples, **69,32 %**.
- Die Skyrim-Rücksprungadresse RVA `0xebc10d` liegt in 12.732 Samples, **69,68 %**.
- Die zugehörige übergeordnete Frame-Aufrufstelle RVA `0x656fe3` liegt in **71,79 %** der Samples.
- Der vorherige Render-Kandidat mit Rücksprungadresse RVA `0x15601d4` liegt in **19,43 %** der Samples.
- Die früher dominanten Limiter-Aufrufstellen `0x1009fc8` und `0x1009fd5` tauchen in dieser Auswertung nicht auf.

Die genannten inklusiven Anteile sind verschachtelt und dürfen nicht addiert werden. Sie messen Samples laufender CPU-Arbeit, nicht unmittelbar die blockierte Wartezeit.

Der lokal disassemblierte Abschnitt um RVA `0xebc09d` prüft wiederholt einen Zähler. Solange Arbeit aussteht, kann er einen vorhandenen Job bearbeiten oder `SwitchToThread` aufrufen und erneut prüfen. Die Importadresse RVA `0x17c83e8` wurde direkt aus der EXE als `SwitchToThread` aufgelöst, `0x17c8440` als `GetCurrentThreadId`. Codebeleg: `research/integration/job-wait-ebc09d-disassembly.txt`. Dies passt zu einem Warten auf den Abschluss vorhandener Jobs; es ist ein anderer Pfad als das aktive Warten auf die Framerate-Grenze.

## Konkreter Worker-Hotspot

Der einzelne Worker 16320 besitzt 18.454 Samples. **71,74 % exklusiv** treffen RVA `0x797bf6`. Eine zusätzliche Auswertung aller sechs stark beschäftigten Worker zusammen enthält **109.932 Samples** und bestätigt denselben Hotspot mit **78.554 exklusiven Samples, 71,46 %**. Damit ist der Befund nicht nur auf einen herausgegriffenen Worker gestützt.

Die Funktion von RVA `0x797b70` bis `0x797c52`:

1. Nimmt einen vorhandenen Lock bei `this + 0x150`.
2. Durchläuft einen Zeiger-Array bei `this + 0x158`, mit Elementzahl bei `this + 0x168`.
3. Überspringt leere Einträge und fragt den numerischen Nachrichtentyp über den virtuellen Slot `0x08` ab.
4. Vergleicht ihn mit dem einmal aufgelösten Typ für den in der EXE enthaltenen String **`MovementMessageNewPath`**.
5. Gibt zurück, ob ein passender Eintrag gefunden wurde, und gibt den Lock wieder frei.

Die direkte Aufrufstelle mit Rücksprungadresse RVA `0x673d6d` wurde auf ein `call 0x797b70` geprüft. Codebeleg: `research/integration/worker-hotspot-797b70-disassembly.txt`.

Diese Struktur passt zur veröffentlichten Definition von `MovementControllerNPC`: Lock bei `0x150`, `BSTArray<MovementMessage*>` bei `0x158`. Der lokale Code und die Typzeichenkette liefern dabei die Runtime-Belege; die Header allein wären kein ausreichender Nachweis für 1.7.104. [CommonLib: MovementControllerNPC](https://github.com/alandtse/CommonLibSSE-NG/blob/ng/include/RE/M/MovementControllerNPC.h)

Damit ist eine wiederholte Suche nach einer New-Path-Bewegungsnachricht der vorrangige Kandidat. Noch nicht gemessen sind die konkreten Listenlängen, die Häufigkeit der Suche pro Actor, Änderungsraten, Objektidentitäten, Lock-Contention und ein möglicher Nachrichtenrückstau. Der hohe Sample-Anteil am Einlesen eines Objekts und vor dem virtuellen Aufruf allein beweist noch keine bestimmte Speicherlatenzursache.

## Konsequenz für das Multicore-Projekt

Diese Szene hat einen tatsächlichen Frame-Einbruch. Sie belegt jedoch keinen ausschließlich auf einem Kern laufenden NPC-Pfad: Sechs vorhandene Worker sind stark beschäftigt, während der Main Thread auf Jobs wartet. Die zusätzliche Arbeit läuft schon parallel. Mehr neu angelegte Threads wären deshalb noch kein begründeter Fix.

Der nächste konkrete Prototyp sollte die Nachrichtensuche instrumentieren: Aufrufzahl, Listenlänge, Ergebnis, Mutationen und Thread-Rolle erfassen; zunächst immer die originale Entscheidung beibehalten. Eine schnellere Typprüfung oder ein Index könnte die Arbeit der bestehenden Jobs reduzieren und so den Main Thread früher freigeben. Ein Cache müsste Änderungen, Objektlebensdauer und die bestehenden Locks korrekt berücksichtigen. Ohne diese Prüfung darf er keine Ergebnisse ersetzen.

Für einen Plugin-Test müsste diese Szene anschließend über SKSE gestartet werden. Der aktuelle Vanilla-Prozess kann weiter ohne Änderungen profiliert werden. Bisher wurde weder die Bewegungssuche gepatcht noch ein FPS-Gewinn nachgewiesen.

## Lokale Belege

In `measurements/20261002-214449-509-baseline/`: `capture-state.json`, `quality-check.json`, `plugin-presence.json`, `frame-summary.json`, `cpu-summary.json`, `hot-thread-symbols.html/.json`, `worker-thread-symbols.html/.json`, `six-workers-symbols.html/.json` und die Rohdateien. Die exklusiven Sample-Summen wurden beim HTML-Export geprüft. Öffentliche Windows-Symbole stammen aus dem lokalen Cache; Bethesda-Funktionsnamen fehlen. Die symbolisierten Modulnamen TESV.exe und nvwgf2um.dll sind interne Report-Namen für die in den Aufnahmemetadaten bestätigten x64-Dateien.
