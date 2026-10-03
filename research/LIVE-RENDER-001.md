# Erste Live-Anbindung der Renderpaket-Diagnose

3. Oktober 2026. `RenderPassProbe` Version 1 wurde bei geschlossenem Skyrim installiert, über SKSE geladen und in der vom Benutzer bestätigten normalen Szene getestet. Es sollten keine zusätzlichen Wachen gespawnt werden. Die genaue Position und Blickrichtung wurden nicht genannt. **Der originale Renderpfad blieb synchron; Renderarbeit wurde nicht auf Worker verlagert.**

Installationsmanifest: `measurements/render-installation-20261003-001118-468/manifest.json`. Prozess 43608; Sitzung `measurements/live-render/session-43608-53956046/`. Die geladenen Moduldateien und der installierte DLL-Hash wurden geprüft. SKSE bestätigt Version 1 als korrekt geladen.

## Aufnahme und Qualität

| Merkmal | Ergebnis |
|---|---:|
| Aufnahmedauer | 20,1795 s |
| Abgeschlossene Originalaufrufe an CALL-RVA `0x15601cf` | **3.144.070** |
| Aufrufrate | 155.805,2/s |
| Periodische Stichproben, erster und jeder weitere 16. Aufruf | **196.505** |
| Erwartete Stichproben | **196.505** |
| Wegen voller Puffer verworfen | 0 |
| Abgebrochene Originalaufrufe | 0 |
| Überlauf der Thread-Tabelle / aktive Aufrufe nach Ende | 0 / 0 |
| Aufrufender Thread | **41520**, primärer Prozess-Thread |
| Erneute Caller-/Callee-Codeprüfung vor Aufnahme | bestanden |

Die Stichprobenmenge entspricht exakt `ceil(3.144.070 / 16)`. Thread- und Gesamtzähler stimmen überein; alle CSV-Sequenzen und Zeitintervalle bestanden die Berichtsprüfung. Der Thread startet als erster Prozess-Thread. Das bestätigt zusammen mit dem bekannten Call-Pfad die Main-Thread-Zuordnung.

Die DLL ruft die Originalfunktion mit unveränderten vier Argumenten genau einmal synchron auf. Die Weiterleitung, einschließlich tatsächlich ausgeführter CALL-Umschreibung und mehreren gleichzeitigen Aufrufern, wurde zusätzlich im Labor geprüft. Elf CTest-Prüfungen bestehen einschließlich der Berichtsvalidierung. Das ist keine Live-Bildvergleichsmessung oder allgemeine Mod-Kompatibilitätsgarantie.

## Dauer der Originalfunktion

| Statistik der 196.505 Stichproben | QPC-Dauer |
|---|---:|
| Mittel | **0,5232 µs** |
| Median | **0,3 µs** |
| P95 | **1,2 µs** |
| Maximum | **161,7 µs** |

Gemessen wird die verstrichene Zeit des Originalaufrufs einschließlich Nachfahren, Unterbrechungen und eventueller Originalwartezeiten. Die Aufzeichnung verändert den Aufrufpfad geringfügig. Die Werte sind keine exklusive CPU-Vorbereitungszeit. Periodische Stichproben können Muster bevorzugen oder auslassen; aus diesen Zahlen wird kein FPS-Gewinn oder zuverlässig einsparbarer Anteil des Frames berechnet.

Die enorme Aufrufzahl bedeutet auch nicht dieselbe Anzahl ausgeführter D3D11-Draws: Die Originalfunktion kann abhängig von Zuständen und internen Prüfungen unterschiedliche Arbeit verrichten. Andere Aufrufstellen liegen außerhalb dieses Zählers.

## Gemeinsame Zustände

| Beobachteter globaler Wert | Vorher-/Nachheränderung in Stichproben |
|---|---:|
| Shader-Kandidat | 8.131 |
| Technik-Kandidat | 8.131 |
| Material-Kandidat | **131.059**, rund **66,695 %** |

Die Bezeichnungen sind aus dem geprüften Code abgeleitet. Die Aufnahme vergleicht kopierte skalare Vorher-/Nachherwerte; sie beweist keine vollständig rekonstruierte Engine-Schnittstelle. Ein unveränderter Wert kann innerhalb des Aufrufs dennoch geschrieben worden sein. Alle Daten wurden auf dem aufrufenden Thread erfasst. Keine Worker dereferenzieren die beobachteten Engine-Zeiger.

Die Stichproben enthalten 5.169 verschiedene Paket-Tokens und 1.686 Geometrie-Tokens. Tokens bezeichnen kopierte Zeigerwerte innerhalb dieser Sitzung. Sie sind kein stabiler Ressourcenbesitz und dürfen bei Adresswiederverwendung nicht mit unveränderlichen Objektidentitäten gleichgesetzt werden. Der häufigste beobachtete Technikwert ist `0x2046` mit 42.166 Stichproben.

## Bedeutung für die nächste Implementierung

Der untersuchte Pfad ist jetzt tatsächlich an Skyrim angebunden und live erfasst. Die häufigen Änderungen gemeinsamer Zustände bestätigen, dass eine unveränderte parallele Ausführung der Originalfunktion konkurrierende Zugriffe erzeugen würde. Die hohe Zahl sehr kurzer Aufrufe spricht zudem für **größere Pakete** mit eigener Zustandsverwaltung: Einen Worker-Auftrag für jede einzelne Originalfunktion zu erzeugen wäre angesichts der Labor-Dispatchkosten kein begründeter Ansatz.

Die nächste tatsächliche Verlagerung braucht eine geprüfte Paketgrenze, stabile Ressourcenreferenzen und eine Vorbereitung mit getrennten Shader-/Materialzuständen. Geordnete Befehlsaufzeichnung ist im separaten D3D11-Labor vorhanden; die Engine-Anbindung dieses Backends bleibt offen. Ein genereller Multicore-Enabler oder FPS-Gewinn wurde mit dieser Diagnose noch nicht implementiert.

## Dateien und Hashes

In der Sitzung: `summary.json`, `samples.csv`, `analysis.json`, `quality-check.json`, `scene-confirmation.json`, `startup-verification.json`, `thread-origins.json`, `plugin.log`. CSV-SHA-256: `1F6BA1DD58B97CCA7AE371FD5ED65724CA3664DAFE5D77E0913485150C617095`.

Die nach Aufnahmeende verbleibende Weiterleitung erfasst keine zusätzlichen Stichproben. Originalaufrufe laufen weiterhin auf ihrem ursprünglichen Thread. Weitere Aufnahmen erfordern einen regulären Plugin-/Spielneustart; laufende Engine-Instruktionen werden dafür nicht verändert.

[Anbindung und Installation](../docs/RENDER-ANBINDUNG.md), [Entwurf und Engine-Abhängigkeiten](MAIN-THREAD-004.md), [separater D3D11-Backend](D3D11-PARALLEL-LAB.md).
