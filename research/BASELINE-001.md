# Erste Aufnahme: Frame-Daten vorhanden, CPU-Trace verworfen

Aufnahme am 2. Oktober 2026, etwa 19:45 Uhr (Europe/Berlin).

Szene laut Nutzer: Testfigur per `coc WhiterunOrigin` aus dem Hauptmenü. Erster stationärer Blickpunkt in Weißlauf; Blickrichtung und Bildinhalt wurden nicht per Screenshot bestätigt. Die Aufnahme nutzt Skyrim 1.7.104.0, PID 11664.

## Vorläufige Frame-Daten

PresentMon 2.6.0 lief 30 Sekunden und endete mit Exit 0. Eine Swap Chain, 1804 Zeilen; keine ungültigen Present-Intervalle.

| Metrik | Wert |
|---|---:|
| Mittleres Present-Intervall | 16,606 ms |
| Median | 16,571 ms |
| P95 | 16,652 ms |
| P99 | 16,799 ms |
| Rechnerische Present-Rate | 60,22/s |
| Mittlere GPU-Busy-Zeit laut PresentMon | 4,054 ms |
| Present-Aufruf | 0,148 ms |
| DXGI SyncInterval / PresentMode | 0 / Hardware: Independent Flip |

Die sehr gleichmäßige Rate ist ein Hinweis auf eine Framerate-Begrenzung. Welcher Mechanismus begrenzt, ist nicht geklärt. Der INI-Bericht **nach** der Aufnahme enthält `iVSyncPresentInterval=1`; das tatsächliche DXGI-SyncInterval im CSV ist 0. Diese Abweichung muss untersucht werden, statt allein aus der INI auf aktives VSync zu schließen.

Die PresentMon-Metrik `MsCPUBusy` (~16,46 ms) ist kein Nachweis für 16,46 ms geplante CPU-Ausführung eines Main Threads. Ein eigenständiger Thread-/Stack-Trace ist dafür erforderlich. Frame- und GPU-Zahlen dieses Laufs sind vorläufig: Der gleichzeitig laufende breite WPR-Recorder kann das Verhalten beeinflusst haben.

## Qualitätsprüfung: CPU-/GPU-ETL ungeeignet

WPR beendete die Aufnahme mit Exit 0 und erzeugte eine 11.620.319.232 Byte große ETL. Der erfolgreiche Abschluss der Datei reicht nicht als Datenqualitätsprüfung.

`xperf` verweigerte die normale Analyse wegen **38.738.001 verlorenen Ereignissen**. Die Diagnose mit `-tle` bestätigte diese Zahl; sie wurde ausschließlich zur Prüfung der Verlustmeldungen eingesetzt. Aus diesem Trace werden **keine** CPU-Hotspots, Thread-Lasten oder Warteketten als belastbare Ergebnisse abgeleitet.

Rohdaten liegen lokal in `measurements/20261002-194447-377-baseline/`. `frame-summary.json` und `loss-diagnostics.txt` enthalten die zugrunde liegenden Zahlen. Die ETL umfasst inklusive Recorder-Rundown etwa 56,6 Sekunden; das ist nicht gleich der 30-Sekunden-Frame-Stichprobe.

## Korrektur für die nächste Aufnahme

`tools/SkyrimCPU.wprp` reduziert WPR auf Prozess-/Modulereignisse, CPU-Samples mit Stacks sowie Scheduling-Ereignisse. Es aktiviert keine breit angelegten GPU-Eventprovider und keine Stacks bei jedem Context Switch. PresentMon erfasst GPU-/Frame-Metriken separat.

Das Profil wurde von lokalem WPR als `SkyrimCPU.Verbose.File` akzeptiert. Konfiguration: 256 Puffer à 256 KB. Die nächste Aufnahme dauert zunächst zehn Sekunden. Die Verlustprüfung erfolgt erneut; geringe Dateigröße allein beweist keine vollständigen Daten. Ohne Context-Switch-Stacks ist die Zuordnung von Warte-Aufrufern begrenzt; CPU-Sample-Stacks und Running-/Ready-/Wait-Zeiten bleiben das erste Ziel.

Noch keine Engine-Änderung und kein Leistungsgewinn nachgewiesen.
