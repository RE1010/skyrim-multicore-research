# Uncapped-Messung für Skyrim 1.7.104.0

Der bisherige normale Test lag bei etwa 60,55 Presents/s. Die Warteschleife des Engine-Limiters dominierte die CPU-Stichproben. Diese Messung kann einen Gewinn durch weniger Render-Vorbereitung nicht zuverlässig beurteilen. `UncappedBenchmark` hebt im geladenen Spiel den Engine-Limiter, `iFPSClamp` und das DXGI-SyncInterval auf. Ein zusätzlicher Treiber-Limiter bleibt außerhalb dieses Plugins.

**Dies ist kein Multicore-Patch.** Ein FPS-Anstieg nach Aktivierung allein belegt die Entfernung des Limits, keinen Leistungsgewinn durch Parallelisierung. Zuerst benötigen wir eine uncapped serielle Referenzaufnahme. Danach vergleichen wir einen tatsächlich ersetzten Engine-Pfad unter denselben Bedingungen.

## Konkrete Anbindung

Alle Adressen gelten ausschließlich für den vollständig SHA-256-geprüften Zielbuild. Vollständige Physikfunktion, deren Caller, Presentfunktion, Present-CALL, Timercode und sechs Setting-Namen werden vor dem Patch geprüft. Zwei CALLs werden nur beim SKSE-Start auf gepinnte Weiterleitungen umgebogen. Die Originalfunktionen laufen jeweils genau einmal, synchron und mit unveränderten Argumenten.

- Physikbudget-Funktion `0x104ab70`, CALL `0x659db6`: vor dem Original werden `fMaxTime:HAVOK` und `fMaxTimeComplex:HAVOK` angepasst. Der unskalierte Timerwert `0x327560c` ist die Grundlage; die ursprüngliche skalierte Zeit als Funktionsargument bleibt unverändert. Die Engine multipliziert das Budget selbst mit dem Zeitfaktor.
- Die Physikbudgets werden auf ein Intervall von 1/240 bis 1/60 Sekunden begrenzt. Das begrenzt **nicht** die Render-FPS. Engere ursprüngliche Einstellungen bleiben erhalten. Die Schrittzahlen 3/1 bleiben erhalten; andere Schrittzahlen führen zur deaktivierten Anpassung.
- Ungewöhnlich kleine Zeitfaktoren, ungültige Timerwerte oder Budgets unter der im Original geprüften Mindestschritt-Grenze führen zur ursprünglichen Konfiguration zurück. Pausierte Zeit wird nicht durch einen künstlichen Zeitschritt ersetzt.
- Hauptframe-Present-CALL `0x656bb1`: Engine-Lock und Renderer-SyncInterval werden nach gültiger Physikberechnung im geladenen Spiel auf 0 gesetzt. Laden und deaktivierter Modus stellen die ursprünglichen Werte wieder her. Es erfolgt kein Write aus einem externen Prozess; die Datenänderungen geschehen in den Engine-Callbacks.

Die Form der dynamischen Budgets orientiert sich am [Havok-Modul von SSE Display Tweaks](https://github.com/SlavicPotato/SSEDisplayTweaks/blob/master/SSETweaks/havok.cpp). Die Adressen dieses Projekts stammen aus der lokalen Prüfung von 1.7.104; es wird kein altes Display-Tweaks-Binary installiert. Das Plugin implementiert dessen weitere Bewegung-, Schadens- oder andere High-FPS-Korrekturen nicht. Die Zeitbudget-Prüfung ersetzt daher keinen vollständigen Spiel- und Physiktest bei hohen FPS. Diese Version dient einem kontrollierten Benchmark.

## Laborprüfung

`tests/uncap_physics.cpp` führt die echten, lokal extrahierten Bytes der Originalfunktion in einem privaten Test-Image mit privaten Globals aus. Geprüft werden Zeitbudgets, skalierte Originalargumente und Verhalten über Folgen von Framezeiten bei 60/120/240/500 Render-FPS und Zeitfaktoren 0,25/1/2. Zusätzlich: engere Originalbudgets, ungültige Timer, Wiederherstellung von Limiter/Sync/Clamp, Laden und ein echter Win64-CALL mit gemischten Float- und Byteargumenten. Die gesamte Havok-Simulation wird damit nicht ausgeführt.

## Installation und Umschaltung

`tools/Install-UncappedBenchmark.ps1` verweigert eine Installation bei laufendem Skyrim, prüft den EXE-Hash und sichert vorhandene DLL/INI-Dateien samt Installationsmanifest. Start erfolgt über SKSE. `measurements/live-uncap/current-session.json` nennt die aktive Sitzung; deren `summary.json` meldet unter anderem `settingsReady`, `worldLoaded`, `uncappedActive` und `invalidTimes`.

`tools/Set-UncappedMode.ps1 -Mode off` stellt die ursprünglichen Werte in den Engine-Callbacks wieder her. `-Mode uncapped` aktiviert die Benchmark-Anpassung erneut. Die Datei steuert ausschließlich vorhandene Callbacks; es erfolgt keine Code-Umschreibung während des Spiels.

Für den nächsten Schritt brauchen wir dieselbe normale Ansicht, ohne neue NPCs und ohne Kamerawechsel, mit bestätigtem `uncappedActive=true`. PresentMon und CPU/GPU-Trace liefern die serielle Referenz und zeigen, ob Render-Vorbereitung, bestehende Worker oder GPU den Frame begrenzen. Die nächste Worker-Anbindung muss genau den relevanten Engpass ersetzen und dabei Ressourcen und Zustände unabhängig besitzen.
