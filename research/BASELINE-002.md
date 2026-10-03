# Zweite Aufnahme: CPU-Trace und Framerate-Warteschleife

2. Oktober 2026, 19:52 Uhr, Europe/Berlin. **Die Aufnahme gelingt ohne verlorene Ereignisse. In dieser Szene stammt ein großer Teil der CPU-Last des Frame-Threads aus aktivem Warten vor der Bildausgabe. Ein Engpass durch zu viel Spielarbeit und ein Gewinn durch Parallelisierung sind noch nicht nachgewiesen.**

## Aufnahme und Qualität

Rohdaten: `measurements/20261002-195209-439-baseline/`. SkyrimSE.exe 1.7.104.0, PID 11664, EXE-SHA-256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`.

Szene laut Nutzer: derselbe stationäre Blickpunkt nach `coc WhiterunOrigin`, Spiel im Vordergrund, Menüs geschlossen. Blickrichtung und Bildinhalt wurden nicht visuell bestätigt. Dies ist eine kurze technische Baseline, kein reproduzierbarer Vergleich mehrerer Szenen.

Das schmale Profil erfasst CPU-Samples mit Stacks sowie Scheduling ohne Context-Switch-Stacks. PresentMon läuft zehn Sekunden; die ETL umfasst 10,7508936 Sekunden einschließlich Aufnahmegrenzen. CPU-Prozentwerte unten verwenden das gesamte ETL-Intervall, nicht einzelne PresentMon-Frames.

| Qualitätsmerkmal | Ergebnis |
|---|---:|
| ETL-Größe | 105.381.888 Byte |
| Verlorene Ereignisse / Puffer | 0 / 0 |
| WPR-Stop / PresentMon-Exit | 0 / 0 |
| PresentMon-Zeilen / Swap Chains | 603 / 1 |
| Ungültige Present-Intervalle | 0 |

Xperf akzeptiert die normale Analyse ohne Verlust-Override. Grundlagen: `trace-stats.txt`, `trace-stats-detail.txt`, `cpu-profile.txt`, `cpu-threads.txt`, `cpu-summary.json`, `frame-summary.json`. Der unveränderte Aufnahmebericht `capture-state.json` bleibt auf seinem damaligen Stand „traceQuality: not checked“; die nachträgliche Prüfung steht hier und in `cpu-summary.json`. Null Verluste beweisen weder fehlenden Recorder-Aufwand noch repräsentative Spielbedingungen.

## Frames und geplante CPU-Zeit

| Frame-Metrik | Wert |
|---|---:|
| Mittleres Present-Intervall | 16,5173 ms |
| Median / P95 / P99 | 16,5137 / 16,5508 / 16,5844 ms |
| Rechnerische Present-Rate | 60,5425/s |
| Mittlere GPU-Busy-Zeit laut PresentMon | 3,9575 ms |
| Mittlere Dauer im Present-Aufruf | 0,0971 ms |
| DXGI SyncInterval / PresentMode | 0 / Hardware: Independent Flip |

Die Rate ist keine unabhängige Messung angezeigter oder einzigartig gerenderter FPS. Die GPU-Metrik spricht gegen GPU-Auslastung als Begrenzung dieser Szene; sie beweist allein keinen CPU-Engpass. `MsCPUBusy` von 16,4202 ms ist **keine** geplante Main-Thread-CPU-Zeit.

Scheduling ergibt für Skyrim insgesamt 20,451342 CPU-Sekunden im 10,750894-Sekunden-Trace: ungefähr 1,902 gleichzeitig belegte logische Prozessoren beziehungsweise 11,889 % der 16 logischen Prozessoren. Das sind Laufzeitanteile, keine nach P-/E-Core-Leistung gewichteten Kapazitätswerte.

| Thread-ID | CPU-Sekunden | Anteil eines logischen Prozessors |
|---|---:|---:|
| 39068 | 10,303271 | 95,836 % |
| 9000 | 2,060323 | 19,164 % |
| 24284 | 1,242161 | 11,554 % |
| 37672 | 1,241926 | 11,552 % |
| 38060 | 1,238276 | 11,518 % |
| 36288 | 1,238238 | 11,518 % |
| 33532 | 1,231421 | 11,454 % |
| 39492 | 1,223549 | 11,381 % |

Skyrim arbeitet bereits auf mehreren Threads. Die Lastverteilung allein bestimmt weder ihre Aufgaben noch den kritischen Frame-Pfad.

## Aufrufstapel und Thread-Rollen

Öffentliche Microsoft-Symbole wurden lokal geladen; private Bethesda- und Nvidia-Funktionsnamen fehlen. Xperf benutzt interne Modulbezeichnungen wie `TESV.exe` und `nvwgf2um.dll`. Die geladenen Dateien laut Aufnahmemetadaten sind `SkyrimSE.exe` und `nvwgf2umx.dll`; es handelt sich weiterhin um das x64-Spiel.

Thread 39068 führt D3D11-Context-Aufrufe und den unten rekonstruierten Ausgabe-/Limiterpfad aus. Sein Start liegt nur etwa sieben Mikrosekunden nach dem Prozessstart; dies wurde nach der Aufnahme am unverändert laufenden Prozess geprüft (`thread-origins.json`). Zusammen mit dem Bootstrap-Stack ist das ein starker Hinweis auf den primären Frame-/Main-Thread. Die Rolle wird nicht nur aus der CPU-Rangfolge abgeleitet.

Der symbolisierte Report enthält 10.293 Sample-Stacks für diesen Thread. **54,44 % enthalten `Kernelbase!SleepEx`**, einschließlich Unterfunktionen; **11,51 % treffen exklusiv `ntdll!RtlQueryPerformanceCounter`**. Verschachtelte Funktionen wie SleepEx, NtDelayExecution und Kernel-Eintritt dürfen nicht als unabhängige Kosten addiert werden. Sample-Anteile schätzen laufende CPU-Arbeit, nicht blockierte Wartezeit. Der Report ohne Symbolauflösung hat einen geringfügig anderen Nenner; diese Zahlen stammen ausschließlich aus `hot-thread-symbols.html`.

Thread 9000 startet und arbeitet fast vollständig im Nvidia-Usermode-Treiber: 98,73 % seiner symbolisierten Stacks enthalten einen entsprechenden Treiberpfad. Thread 24284 hat überwiegend Skyrim-interne Stacks. Für ihn und die ähnlich belasteten weiteren Threads ist noch keine konkrete Aufgabe wie Sichtbarkeit oder Animation belegt. Reports: `second-thread-symbols.html`, `worker-thread-symbols.html`.

## Bestätigung am Code der installierten EXE

Die EXE wurde ausschließlich gelesen. Das vorhandene Microsoft-Tool dumpbin disassembliert begrenzte Bereiche; `tools/Inspect-PeEvidence.py` liest zugehörige Import-Tabelleneinträge und Float-Konstanten aus derselben, per Hash identifizierten Datei. Kein Prozess wurde verändert.

Folgende Angaben sind **RVA ausschließlich für diesen Build**, keine universellen Hook-Adressen:

- Funktion bei `0x1009F70`: Ein bedingter Pfad vergleicht vergangene Zeit mit einer festen Grenze.
- Bei `0x1009FC0` setzt der Code ecx auf null; der folgende Import `0x17C8448` ist `KERNEL32!Sleep`. Rücksprungadresse `0x1009FC8` erscheint häufig im Trace.
- Anschließend wird Zeitgeber `0xCE43D0` aufgerufen. Dessen Import `0x17C84C8` ist `QueryPerformanceCounter`; Rücksprungadresse `0x1009FD5` erscheint ebenfalls häufig.
- Initialisierung `0xCE4380` verwendet `QueryPerformanceFrequency`, Import `0x17C81A0`. Die Konstanten sind 1,0 und 0,001. Der Faktor ist `1 / (Frequenz * 0,001)`, also Millisekunden pro Tick.
- Vergleichskonstante `0x1A74488`: `16.393442153930664` ms, ungefähr `1000 / 61`.
- Nach Erreichen dieser Grenze folgt ein virtueller Aufruf am Vtable-Offset `0x40`, passend zu `IDXGISwapChain::Present`; danach wird der Ausgangszeitstempel erneuert. Stack und PresentMon-Daten stützen die Einordnung als Bildausgabepfad.

Belege: `limiter-disassembly.txt`, `clock-disassembly.txt`, `clock-init-disassembly.txt`, `kernel32-imports.txt`, `pe-evidence.json`. Die gemessenen 16,5173 ms sind mit Zeitgrenze plus Present- und sonstigem Aufwand vereinbar. DXGI SyncInterval null schließt einen eigenen Engine-Limiter nicht aus.

Vereinfachter beobachteter Pfad:

```text
solange (QPC - vorherigerZeitstempel) * MillisekundenProTick < 16,393442:
    Sleep(0)
Bild ausgeben
vorherigerZeitstempel = QPC
```

`Sleep(0)` gibt die verbleibende Zeitscheibe ab, lässt den Thread aber ausführbar. Ohne geeignete andere Arbeit kann er sofort weiterlaufen. Die Schleife kann deshalb viel CPU-Zeit verbrauchen. Dies passt zu dokumentiertem Windows-Verhalten. [Microsoft: Sleep](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)

## Konsequenz und nächster Versuch

**Zielpräzisierung:** Der Nutzer hat anschließend ausdrücklich eine Multicore-Lösung verlangt. Die folgende Limiter-Idee wird daher nicht als Entwicklungsziel verfolgt. Ein erster paralleler Sichtbarkeits-Rechenkern ist inzwischen gebaut und getestet; Stand und noch fehlende Engine-Anbindung stehen in `docs/MULTICORE-PROTOTYP.md`.

**Konkreter Kandidat: CPU-Verbrauch der Framerate-Warteschleife senken und die eigentliche Frame-Arbeit getrennt messen.** Ein Prototyp könnte zunächst blockierend bis kurz vor dieselbe Deadline warten und nur für den Rest aktiv warten. Zeitgrenze und Simulation bleiben dabei zunächst gleich. CPU-Zeit, P95/P99 und verpasste Deadlines müssen gemeinsam verglichen werden; weniger CPU-Verbrauch bei schlechterem Frame-Pacing wäre kein Erfolg. Die passende Runtime-Anbindung und ein eigener Plugin-Prototyp fehlen noch.

Framerate-Limiter sind bereits Thema existierender Mods. SSE Display Tweaks installiert einen Limiter vor oder nach Present. Der betrachtete upstream Quellstand ist älter als unsere Runtime; seine Kompatibilität mit 1.7.104 ist hier nicht bestätigt. [Upstream, Commit 41668a7](https://github.com/SlavicPotato/SSEDisplayTweaks/blob/41668a7/SSETweaks/render.cpp)

Für die gewünschte Multicore-Verbesserung brauchen wir anschließend eine präzise dokumentierte, stärker belastete Szene und Instrumentierung der verbleibenden Spiel-/Renderphasen. Erst ein nachgewiesener, abtrennbarer Rechenpfad rechtfertigt den Vergleich einer seriellen Referenz mit einem Snapshot-/Worker-Prototyp. Einfach die Framerate-Begrenzung zu lösen beweist keine Parallelisierung.

Noch offen: Job-Rollen, Running-/Ready-/Wait-Verlauf pro Frame, Recorder-Aufwand, mehrere Szenen und Wiederholungen, Plugin-Anbindung, Zellwechsel und Mod-Kompatibilität. **Bisher kein Engine-Patch, keine FPS-Steigerung und keine umfassende Main-Thread-Lösung. Fortschritt: ein überprüfter CPU-Trace und ein am Zielbuild bestätigter dominanter Pfad.**
