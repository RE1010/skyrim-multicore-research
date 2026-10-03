# Erster Live-Test der Bewegungsnachrichten-Suche

2. Oktober 2026, Europe/Berlin. Die über SKSE geladene Bewegungsdiagnose hat in der bestätigten Außenszene vor Weißlauf eine vollständige Aufnahme geliefert. **Sehr lange Nachrichtenlisten werden häufig mit negativem Ergebnis durchlaufen.** Der Lock-Erwerb ist in den Stichproben kurz; die Dauer steigt mit der Listenlänge. Ein Engine-Fix oder FPS-Gewinn wurde noch nicht umgesetzt.

## Bedingungen und Qualität

Der Nutzer bestätigte um 22:27:12 Uhr „bin jetzt da drin“, im Kontext der zuvor vereinbarten 600 zusätzlichen Wachen. Ort, Zahl, Kamera und Menüfreiheit wurden nicht unabhängig aufgezeichnet. Nach acht Sekunden Fokuszeit löste das Skript den Trigger um 22:27:21 Uhr aus; Ausgabeabschluss um 22:27:41 Uhr. QPC-Messdauer: **20,1851139 Sekunden**, Frequenz 10.000.000/s.

Skyrim-Prozess 1464, Runtime 1.7.104, unveränderter EXE-Hash. Geladen: SKSE und `MovementMessageProbe.dll`. Die ältere Sichtbarkeitsdiagnose ist gesichert und pausiert. Die neue DLL prüfte vor dem Startup-Hook die EXE, beide vollständigen Funktionen und die CALL-Aufrufstellen. Nach Aufnahmeende bleiben ausschließlich inaktive Weiterleitungen auf die Originalfunktionen bestehen.

Sitzung: `measurements/live-movement/session-1464-47523937/`.

| Merkmal | Ergebnis |
|---|---:|
| Status | `capture-complete` |
| Abgeschlossene Suchaufrufe | **384.888** |
| Aufrufrate an der geprüften Stelle | **19.067,91/s** |
| Ergebnis wahr / falsch | **5 / 384.883** |
| Anteil negativ | **99,9987 %** |
| Numerische Stichproben | **6.016** |
| Verworfene Stichproben | 0 |
| Abgebrochene Aufrufe / Thread-Kapazitätsüberläufe | 0 / 0 |
| Noch aktive Schreiber beim Abschluss | 0 |

Die Auswertung prüft abgeschlossenen Status, Zählersummen, Thread-IDs, eindeutige Sampling-Sequenzen, Zeitwerte und genau einen Original-Lock pro Stichprobe. Die Zahl 6.016 stimmt zusätzlich mit der Summe der je Thread erwarteten Stichprobenzahlen überein. Die Suche wird weiterhin exakt einmal aufgerufen; ihre Entscheidung ist maßgeblich. Anders als beim Sichtbarkeits-Replay wurde hier keine alternative Suchentscheidung berechnet und auf Abweichungen verglichen.

## Listenlängen und Dauer

| Stichprobenmetrik | Mittel | Median | P95 | Maximum |
|---|---:|---:|---:|---:|
| Listenlänge | 10.803,86 | **11.019** | 32.498 | **50.755** |
| Funktionsdauer | 50,177 µs | 39,1 µs | 157,8 µs | 688,6 µs |
| Lock-Erwerb | 0,104 µs | 0,1 µs | 0,2 µs | 1,3 µs |

Dauern sind QPC-Wandzeit inklusive Diagnoseaufwand und Scheduling, keine exklusive CPU-Zeit. Die Auflösung beträgt 0,1 µs. Funktionsdauer minus Lock-Erwerb enthält auch Prolog, Freigabe und Diagnosearbeit; sie ist kein exakt isolierter Scan-Timer.

| Listenlänge | Stichproben | Mittlere Funktionsdauer |
|---|---:|---:|
| 0 | 1.116 | 0,321 µs |
| 1–8 | 381 | 0,592 µs |
| 9–64 | 15 | 0,820 µs |
| 65–256 | 169 | 1,520 µs |
| 257–1.024 | 576 | 3,922 µs |
| 1.025–4.096 | 337 | 13,886 µs |
| Mehr als 4.096 | **3.422** | **85,936 µs** |

Alle 6.016 Stichproben hatten ein negatives Ergebnis. Die fünf positiven Originalaufrufe lagen zwischen den systematischen Stichproben. Ihr Fehlen in den Stichproben darf nicht als Beweis gewertet werden, dass es keine positiven Ergebnisse gibt.

Der Originalcode durchläuft bei negativem Ergebnis die gesamte Array-Länge, einschließlich des Überspringens leerer Einträge. Ob die langen Arrays hauptsächlich gültige Nachrichten, Null-Einträge oder eine bestimmte Nachrichtenart enthalten, wurde noch nicht erhoben. „50.755 Einträge“ bezeichnet daher keine unabhängig bestätigte Zahl aktiver Pathfinding-Aufträge.

## Wiederholungen und Threads

553 verschiedene Controller-Token erscheinen in den Stichproben; jeder davon wurde mehrfach beobachtet. **552 Token wurden auf mehreren Engine-Threads gesehen.** Die Token codieren pro Prozess eine Objektadresse und sind keine Actor-FormIDs oder abgesicherte Objektlebensdauer-Identität. Eine spätere Wiederverwendung derselben Adresse wäre nicht unterscheidbar.

Bei 119 Token wurden verschiedene Listenlängen beobachtet. Beispielsweise lieferte ein Token 17 Stichproben mit Längen zwischen 50.126 und 50.755, verteilt auf sechs Threads. Ein anderer blieb über 13 Stichproben bei 49.550 Einträgen. Gleiche Länge beweist keine unveränderten Elemente; unterschiedliche Längen identifizieren keine konkreten Mutationsstellen.

| Thread-ID | Abgeschlossene Aufrufe | Positive Ergebnisse |
|---|---:|---:|
| 40424 | 64.967 | 1 |
| 37796 | 64.410 | 0 |
| 16300 | 62.654 | 1 |
| 36860 | 65.280 | 0 |
| 39580 | 63.996 | 2 |
| 37192 | 62.885 | 1 |
| 36648 | 696 | 0 |

Thread 36648 startete unmittelbar nach dem Prozess und ist der primäre Thread-Kandidat. Eine neue CPU-Stack-/Frame-Aufnahme wurde in diesem Durchlauf nicht durchgeführt; eine vollständige Rollen- oder FPS-Zuordnung wird deshalb nicht abgeleitet. Die Verteilung bestätigt die starke Nutzung mehrerer Engine-Threads an dieser Aufrufstelle.

## Konsequenz für eine Optimierung

Der Befund rechtfertigt einen Prototyp, der wiederholte Typ-Prüfungen oder Vollscans günstiger macht. Zusätzliche neu angelegte Worker allein beseitigen diese Wiederholung nicht. Ein Typ-Index benötigt vollständig verifizierte Einfüge-, Entferne-, Ersetzungs- und Lebensdauerregeln sowie Synchronisation mit dem ursprünglichen Lock. Ein Cache nur nach Array-Adresse und Elementzahl wäre unzureichend: gleich lange Listen können andere Elemente enthalten, und die seltenen positiven Ergebnisse müssen zuverlässig erhalten bleiben.

Zusätzlich wurde die konkrete Typabfrage für `MovementMessageNewPath` im lokalen EXE-Code gefunden: RVA `0x11d5d30` liest den gecachten Typ-Identifier bei `0x3242fc4`, führt bei null den Original-Resolver aus und gibt den Identifier zurück. Sie nutzt dafür nicht den Nachrichteninhalt. Das ist ein weiterer prüfbarer Ansatz für günstigere Typ-Prüfungen; es belegt noch keine sichere Abkürzung für alle Nachrichtentypen. Der erstmalige Resolver-Aufruf und fremde/objektabhängige Getter dürfen nicht übersprungen werden. Belege stehen in `research/integration/new-path-type-references.json` und `new-path-type-getter-11d5d30-disassembly.txt`.

Die Änderungsstellen und Objektlebensdauer sind noch offen. In dieser Aufnahme wurden keine FPS gemessen und keine Arbeit durch einen schnelleren Algorithmus ersetzt. Eine Beschleunigung muss anschließend anhand identischer Spielszenen und korrekter Originalergebnisse nachgewiesen werden.

## Belege

`summary.json`, `samples.csv`, `analysis.json`, `controller-analysis.json`, `quality-check.json`, `scene-confirmation.json`, `startup-verification.json`, `thread-origins.json` und `plugin.log` in der Sitzung. CSV-Größe: 285.446 Bytes; SHA256 `0D173F31F16AEAB754044D55E91276C7F7DD90FEC9156C4C2EAA17C57A114F78`. Sampling: erster und jeder weitere 64. Aufruf pro Thread, nur über den geprüften CALL bei RVA `0x673d68`; andere Aufrufstellen bleiben außerhalb der Zähler.
