# NPC-Stresstest: 200 zusätzliche Weißlauf-Wachen

2. Oktober 2026, Europe/Berlin. SkyrimSE.exe 1.7.104, gleicher gehashter Build, Prozess 37216, SKSE 2.3.1 und zuvor vollständig abgeschlossene Culling-Diagnose. Die Diagnose hatte ihre beiden Vtable-Slots bereits wiederhergestellt; während dieser Leistungsaufnahme wurden keine Sichtbarkeits-Snapshots mehr verarbeitet.

## Szene und Korrektur

Die Basis-ID `00099CE5` wurde direkt aus dem `NPC_`-Datensatz mit Editor-ID `GuardWhiterunCityGeneric` in der installierten Skyrim.esm gelesen. Der Nutzer spawnte zusätzliche Wachen per Konsole und bestätigte ihre Sichtbarkeit.

Der erste Durchlauf `measurements/20261002-212406-268-baseline/` wurde als 100 zusätzliche Wachen angefordert. Anschließend erklärte der Nutzer, während der Aufnahme weitere Wachen auf insgesamt 200 zusätzliche NPCs gespawnt zu haben. Die Korrektur steht in `scene-correction.json`. Dieser Durchlauf ist **kein Vergleich mit konstanter NPC-Zahl**; auch der Zustand der Konsole während des Spawnens ist nicht zeitlich dokumentiert.

Das danach vom Nutzer übermittelte Bild zeigt zahlreiche Wachen unmittelbar vor der Kamera. Es wurde als `scene-user-after-capture.png` in der ersten Aufnahme abgelegt. Das Bild bestätigt sichtbare NPCs, aber weder genau 200 noch die Blickrichtung während jeder Sekunde des Traces.

Für den zweiten Durchlauf wurde eine konstante Szene mit den jetzt vorhandenen 200 zusätzlichen Wachen, geschlossener Konsole und stiller Kamera angefordert. Erfassung: 21:28:34 Uhr, 20 Sekunden PresentMon, 21,112646 Sekunden ETL einschließlich Aufnahmegrenzen. Rohdaten: `measurements/20261002-212821-205-baseline/`. Keine weiteren Änderungen wurden während dieser Wiederholung gemeldet. Anzahl und Szenenbereitschaft stammen vom Nutzer, nicht von einem Engine-Zähler.

## Qualität und Bildausgabe

| Merkmal | Wiederholung mit 200 zusätzlichen Wachen |
|---|---:|
| WPR-Stop / PresentMon-Exit | 0 / 0 |
| Verlorene Events / Puffer | 0 / 0 |
| PresentMon-Zeilen | 1.207 |
| Mittleres Present-Intervall | 16,5331 ms |
| Median / P95 / P99 | 16,5250 / 16,5767 / 16,6796 ms |
| Rechnerische Present-Rate | 60,4846/s |
| Mittlere GPU-Busy-Zeit | 5,7860 ms |

Die frühere einfache Weißlauf-Baseline lag bei 60,5425 Presents/s und 3,9575 ms GPU-Busy. Die gemischte 100→200-Aufnahme lag bei 60,5399 Presents/s und 5,2580 ms GPU-Busy. Das sind beschreibende Messwerte verschiedener Szenen und Zeitpunkte, kein kontrollierter paarweiser Nachweis der Auswirkungen einer exakt bestimmten NPC-Zahl. Present-Rate ist nicht automatisch die Zahl tatsächlich angezeigter oder einzigartig gerenderter Bilder.

Die Wiederholung hält die bereits rekonstruierte Framerate-Grenze weitgehend ein. Die GPU-Busy-Zeit allein und eine stabile Rate beweisen keine bestimmte CPU-Reserve. Zusammen mit den weiterhin häufigen Limiter-Stacks ist jedoch kein anhaltender Durchsatzverlust unter die Grenze belegt.

## CPU-Verteilung

Skyrim erhielt im 21,112646-Sekunden-Trace insgesamt 80,683811 CPU-Sekunden, entsprechend **3,821587 gleichzeitig belegten logischen Prozessoren** beziehungsweise 23,8849 % der 16 logischen Prozessoren. Diese Laufzeitäquivalente berücksichtigen den unterschiedlichen Durchsatz von P- und E-Kernen nicht.

| Thread-ID | CPU-Zeit | Anteil eines logischen Prozessors |
|---|---:|---:|
| 30756 | 19,787235 s | 93,7222 % |
| 25780 | 8,701642 s | 41,2153 % |
| 17884 | 8,678728 s | 41,1068 % |
| 30096 | 8,648113 s | 40,9618 % |
| 21976 | 8,618120 s | 40,8197 % |
| 39608 | 8,592532 s | 40,6985 % |
| 12324 | 8,560030 s | 40,5446 % |
| 28100 | 7,078402 s | 33,5268 % |

Sechs dieser Worker-IDs stimmen mit den zuvor erfassten aufrufenden Culling-Threads überein. Das ordnet ihre gesamte aktuelle CPU-Zeit aber nicht automatisch Culling zu: Dieselben Threads können mehrere Jobarten ausführen. Der ausgewertete Worker 21976 hat 92,80 % seiner exklusiven Samples im Skyrim-Modul.

Die frühere einfache Baseline belegte etwa 1,902 logische Prozessoren. Die zusätzliche Arbeit im NPC-Test tritt bereits auf mehreren Threads auf. Der genaue Mehrbedarf durch NPCs ist wegen verschiedener Szenen, Zeitpunkte und Installation nicht allein aus diesem Vergleich isoliert.

## Main-/Frame-Thread und Limiter

Thread 30756 startete rund 6 Mikrosekunden nach dem Prozessstart. Seine Aufrufstapel enthalten Bootstrap, D3D11-Context-Aufrufe und denselben bereits am EXE-Code verifizierten Present-/Limiterpfad. Diese Belege stützen seine Rolle als primärer Main-/Frame-Thread; die Rolle wird nicht nur aus der höchsten CPU-Zeit abgeleitet.

Symbolisierter Main-Thread-Report: **19.695 Samples**. Der HTML-Parser verifiziert, dass die exklusiven Modul- und Funktionssummen mit der Zahl der Samples übereinstimmen.

- `Kernelbase!SleepEx` liegt in 4.892 Stacks, **24,84 %** der Samples. Die bekannte Limiter-Rücksprungadresse RVA `0x1009fc8` liegt in 4.909 Stacks, **24,93 %**.
- Die bekannte Zeitgeber-Rücksprungadresse RVA `0x1009fd5` liegt in 1.098 Stacks, **5,58 %**.
- `ntdll!RtlQueryPerformanceCounter` erhält exklusiv 984 Samples, **5,00 %**.
- `D3D11.dll` erhält exklusiv 1.288 Samples, **6,54 %**.

Inklusive und exklusive Anteile dürfen nicht wahllos addiert werden. Insbesondere SleepEx, NtDelayExecution und deren Kernel-Frames überlappen. Hohe Main-Thread-Auslastung enthält weiterhin Limiter-Arbeit und ist deshalb kein Beweis eines Engpasses durch nützliche Spielarbeit.

## Kandidat für weitere Untersuchung

Außerhalb des Limiters ist ein zusammenhängender Main-Thread-Aufrufpfad sichtbar: Rücksprungadressen RVA `0x152128e` und `0x15601d4` liegen in 32,74 % beziehungsweise 32,48 % der Samples und sind verschachtelt, nicht additive Kosten. Die Funktion von RVA `0x1560340` bis `0x15606a1` umfasst zwei häufige exklusive Sample-Stellen bei `0x1560377` und `0x15603ef`.

Das Disassembly in `research/integration/serial-candidate-1560340-disassembly.txt` zeigt mehrere globale Zustandsänderungen, zwischengespeicherte Objektzeiger und virtuelle Aufrufe. Aus Aufrufpfad und D3D11-Bezügen ist ein Bezug zur Render-Vorbereitung plausibel; der genaue Funktionsname und alle Objektlayouts sind noch nicht verifiziert. **Die ganze Funktion ist deshalb kein sicherer Worker-Auftrag.** Ein möglicher Ansatz müsste zunächst unveränderliche Eingaben und unabhängig berechenbare Teilschritte abgrenzen und die Reihenfolge der Zustandsänderungen erhalten.

Der NPC-Test ist damit als Belastungstest nützlich: mehr Arbeit wird sichtbar, bestehende Worker werden stärker beschäftigt und ein serieller Render-Kandidat lässt sich priorisieren. Ein FPS-Verlust unter die Grenze, eine beschleunigende Änderung oder eine Lösung des Main-Thread-Problems wurden noch nicht nachgewiesen.

## Belege und nächste Messung

`capture-state.json`, `trace-stats.txt`, `quality-check.json`, `frame-summary.json`, `cpu-summary.json`, `hot-thread-symbols.html/.json` und `worker-thread-symbols.html/.json` liegen in der zweiten Aufnahme. Öffentliche Windows-Symbole stammen aus dem lokalen Cache; private Bethesda-Symbole fehlen. Xperf verwendet im symbolisierten Report interne Namen wie TESV.exe und nvwgf2um.dll; die Aufnahmemetadaten bestätigen die geladenen x64-Dateien SkyrimSE.exe und nvwgf2umx.dll.

Für einen Skalierungstest müssen weitere Aufnahmen dieselbe konstante NPC-Zahl, denselben Kamerablick und geschlossene Konsole über das gesamte Intervall erhalten. Sinnvoll ist ein nächster, klar getrennt erfasster Lastschritt, bis entweder die Frame-Zeit die Grenze überschreitet oder ein bestimmter serieller Abschnitt genügend messbare Kosten hat. Die vorhandene FPS-Grenze zu entfernen würde einen separaten Eingriff und eine erneute Validierung verlangen; das wäre selbst noch keine Multicore-Lösung.
