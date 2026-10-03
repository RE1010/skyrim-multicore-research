# Dritter Bewegungstest: vollständiger Vergleich, kein Laufzeitgewinn

2. Oktober 2026. Vom Benutzer bestätigte Außenszene vor Weißlauf mit 600 zusätzlichen Wachen. Plugin-Version 3 wurde bei geschlossenem Skyrim installiert, die vorige DLL gesichert und anschließend über SKSE gestartet. Installationsmanifest: `measurements/movement-installation-20261002-231038/manifest.json`. Neue DLL-SHA256: `5B1A4514291FEDD84D2133B389F01FC4AB623BA6ECD2576BA095DE2DF6D8555F`.

Prozess 31304, Sitzung `measurements/live-movement/session-31304-50327421/`. Der Startnachweis bestätigt SKSE und die Bewegungs-DLL, ohne aktive Sichtbarkeits-DLL. Alle 1.123 akzeptierten Getter-Beschreibungen und die benötigten Initialisierungshelfer bestanden vor der Aufnahme die erneute Codeprüfung.

## Ergebnis und Gültigkeit

Die einmalige Aufnahme dauerte 20,1660 Sekunden. 375.792 Originalaufrufe wurden abgeschlossen, darunter drei positive und 375.789 negative Ergebnisse. 5.875 gültige Stichproben entsprechen exakt der erwarteten Summe der pro Thread ersten und anschließend jeden 64. Aufrufe. Keine verlorenen Stichproben, abgebrochenen Aufrufe, Thread-Überläufe oder aktiven Schreiber nach Ende.

**Alle 5.875 Kandidatenscans waren vollständig und stimmten mit dem Original überein.** Kein unbekannter Getter, kalter Getter oder kalter Zieltyp trat in diesen Stichproben auf. Davon enthielten 3.334 Listen mehr als 4.096 Einträge. Listenmedian 11.019, Maximum 56.826. Die Erweiterung für `MovementMessageActorCollision` beseitigt damit die Abdeckungslücke aus Test 2 für diese konkrete Aufnahme.

Sämtliche Stichprobenergebnisse waren negativ. Die drei positiven Originalergebnisse lagen außerhalb der Stichproben; positive Live-Ergebnisse sind deshalb weiterhin nicht mit dem Kandidaten verglichen. Positive, kalte und unbekannte Fälle werden im Labor getestet. Die Null-Abweichungszahl ist ein Ergebnis dieser Aufnahme, keine allgemeine Korrektheitsgarantie für alle Spielzustände oder Mods.

## Zeitwerte und Entscheidung

| Stichproben | Anzahl | Kandidat, Mittel µs | Originalschätzung, Mittel µs |
|---|---:|---:|---:|
| alle | 5.875 | 60,21 | 34,81 |
| Listen mit mehr als 4.096 Elementen | 3.334 | 103,55 | 60,22 |

Die Originalschätzung ist `FunctionTicks - FastTicks`, einschließlich Lock und verbleibendem Diagnoseaufwand. Der Kandidat läuft zuerst unter demselben Lock und wärmt Daten für den Originalscan vor. Er verlängert außerdem die Lock-Haltezeit der erfassten Stichproben. Diese Messanordnung liefert keinen unverzerrten Beschleunigungsfaktor. Zusammen mit den bereits ungünstigen Laborwerten liefert sie **keinen Beleg für einen Laufzeitgewinn**; ein tatsächlicher Ersatz der Originalsuche ist damit nicht gerechtfertigt.

Es wurde keine FPS-Aufnahme durchgeführt. Unterschiede der Aufrufrate zwischen den drei getrennten Sitzungen sind kein FPS- oder Performancebeleg. Der Kandidat bleibt ein Diagnosebaustein; jede Spielentscheidung stammt weiterhin von der exakt einmal ausgeführten Originalsuche. Nach Abschluss läuft der zusätzliche Scan nicht weiter. Die kleinen CALL-Weiterleitungen bleiben bis zum normalen Prozessende als Originalweiterleitung erhalten.

CSV-SHA256: `E73D48A845D35DBF4869B3DB89C49994ED1E01C60B9F54B6A7D7AEEEEDDB4B54`. `analysis.json`, `quality-check.json`, `scene-confirmation.json`, `startup-verification.json` und `plugin.log` liegen in der Sitzung.

## Nächster Forschungsansatz

Die vorhandenen Engine-Worker durchsuchen sehr lange Listen wiederholt und finden fast immer keinen New-Path-Typ. Der aktuelle Kandidat spart diese O(n)-Arbeit nicht; zusätzliche eigene Threads würden sie ebenfalls nicht automatisch beseitigen. Ein korrekt gepflegter Typindex oder Typzähler könnte die konkrete Existenzabfrage nach abgeschlossener Initialisierung verkürzen.

Dafür müssen alle Einfüge-, Entferne-, Ersetzungs- und Löschpfade sowie Controller-Lebensdauer und Sperrregeln nachgewiesen werden. Eine reine Listenlänge, Adresse oder Frame-Nummer genügt nicht zur Invalidierung. Bisher ist unter anderem eine gesperrte Verarbeitung mit anschließendem Zurücksetzen der Elementzahl belegt; die Mutationserfassung ist noch unvollständig. Das ist der nächste zu lösende technische Punkt, bevor ein solcher Index Spielentscheidungen übernehmen darf.
