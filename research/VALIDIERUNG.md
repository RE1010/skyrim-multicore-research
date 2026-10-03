# Validierung der vorbereiteten Diagnosewerkzeuge

2. Oktober 2026. Diese Prüfungen sind **keine Skyrim-Leistungsmessung**.

- Beide PowerShell-Skripte wurden vom PowerShell-Parser ohne Syntaxfehler gelesen.
- `Collect-Setup.ps1` wurde gegen den vom Nutzer genannten Spielpfad ausgeführt. Ergebnis: `local-setup.json`, Runtime 1.7.104.0 und EXE-Hash. Unlesbare aktive Modliste wurde als unbekannt gespeichert.
- `Sample-Threads.ps1` wurde zwei Sekunden gegen einen kontrollierten PowerShell-Testprozess mit zwei künstlich ausgelasteten C#-Threads ausgeführt. Die beiden Worker wurden mit ungefähr 99,6 % und 98,1 % eines logischen Prozessors erfasst.
- Die tatsächliche Aufnahmedauer, mehrere Thread-Datensätze, nichtleere CSV-Ausgabe sowie nichtnegative CPU-Differenzen und positive Zeitintervalle wurden geprüft.
- Ein nicht existierender Prozessname wurde mit der erwarteten Fehlermeldung abgewiesen; dabei wurde keine Aufnahme gestartet.

Die Rohdaten des synthetischen Tests liegen lokal unter `measurements/selftest/` und sind durch `.gitignore` von der Versionsverwaltung ausgeschlossen. Die Worker-Ergebnisse validieren die Zählerberechnung. Sie belegen weder geringen Sampler-Aufwand noch die korrekte Identifikation eines Engine-Threads. Diese Punkte bleiben Teil der eigentlichen Baseline.

Zum Zeitpunkt dieser ursprünglichen Vorabprüfungen war noch keine Spielaufnahme erfolgt. Inzwischen liegen zwei tatsächliche Aufnahmen vor; die erste hatte einen ungeeigneten CPU-Trace, die zweite ist ohne Ereignisverluste auswertbar. Siehe `BASELINE-001.md` und `BASELINE-002.md`. Anschließend wurde ein eigener SKSE-Diagnose-Adapter installiert; siehe `docs/ENGINE-ANBINDUNG.md`. Ein beschleunigender Engine-Ersatz bleibt offen.

## Ergänzung nach der zweiten Aufnahme

- Alle aktuellen PowerShell-Skripte bestehen die Parserprüfung.
- `Summarize-CPUExports.ps1` wurde mit den echten xperf-Exporten ausgeführt. Referenz: Thread 39068, 10.303.271 µs CPU-Zeit über 10,7508936 Sekunden, 95,83641494 % eines logischen Prozessors.
- Eine Kopie der Qualitätsdaten mit einem verlorenen Event wird vor CPU-Auswertung abgewiesen; dabei entsteht keine CPU-Zusammenfassung. Testdaten liegen unter `measurements/selftest/cpu-exports/`.
- `Inspect-PeEvidence.py` liest die echte EXE ausschließlich vom Datenträger. Importnamen stimmen mit dumpbins Importliste überein; die aufgelösten Tabelleneinträge werden durch die disassemblierten Aufrufe verwendet. EXE-Hash stimmt mit den Aufnahmemetadaten überein.
- Die rekonstruierte Timer-Initialisierung und Float-Konstanten belegen Millisekunden als Einheit der Vergleichsgrenze. Dies ist Code-Analyse, kein Benchmark eines veränderten Spiels.
