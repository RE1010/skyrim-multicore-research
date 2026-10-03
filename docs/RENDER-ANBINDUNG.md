# Renderpaket-Diagnose in Skyrim

3. Oktober 2026. `RenderPassProbe.dll` ist als SKSE-Plugin für die lokal geprüfte SkyrimSE.exe 1.7.104.0 gebaut und installiert. Dies ist die erste Anbindung des untersuchten Renderpaket-Pfads an unser Projekt. **Der Originalrenderer bleibt synchron; keine Renderarbeit wird durch diese DLL auf Worker verlagert.** Der separate parallele D3D11-Backend ist weiterhin ein Laborbaustein.

## Eingriff und Versionsprüfung

Die DLL prüft den EXE-SHA-256 `846EFCCF0C1374D71F892907F46549560F2FCB0A75CB87A3EED438BAA0F1402F`, die SKSE-Runtime-Version und vollständige Codebytes von Caller und Callee. Sie ersetzt nur während `SKSEPlugin_Load` den geprüften fünf Byte langen CALL bei RVA `0x15601cf` durch eine Weiterleitung über einen von SKSE bereitgestellten Branch-Stub. Keine laufende Aufnahme schreibt Engine-Instruktionen um.

Der Caller liegt bei `0x155ff40..0x15602a8`, der Callee bei `0x1560340..0x15606a1`. Der Caller übergibt einen Paketzeiger in RCX, einen 32-Bit-Wert in EDX, einen aus einem Byte erweiterten Wert in R8D und einen weiteren 32-Bit-Wert in R9D. Der Callee verwendet diese Register und keine zusätzlichen Stack-/Float-Argumente im untersuchten Code. Der Caller verwendet keinen Rückgabewert. Das ist eine aus diesem Build rekonstruierte Schnittstelle, keine freigegebene Bethesda-API.

Die Weiterleitung führt die Originalfunktion mit denselben Argumenten **genau einmal auf dem ursprünglichen aufrufenden Thread** aus. Die DLL bleibt nach dem Eingriff im Prozess gebunden. Nach Aufnahmeende entfällt die zusätzliche Erfassung; der kleine Weiterleitungs-Hook bleibt bis zum regulären Prozessende bestehen.

Vor Start einer Aufnahme werden Caller, Callee und der eigene CALL erneut geprüft. Fremde Veränderungen führen zu `code-conflict` statt zur Aufnahme. Andere Builds oder bereits veränderte Funktionen werden beim Laden abgelehnt.

## Erfasste Daten

Für alle Aufrufe dieser einen Stelle werden pro Thread Versuch, Abschluss und Ausnahme gezählt. Standardmäßig werden der erste und jeder weitere 16. Aufruf pro Thread detailliert erfasst:

- QPC-Beginn und Ende der Originalfunktion, Technik-/Argumentwerte und Paketflags.
- Als opaque Tokens kopierte Paket-, Shader-, Property- und Geometriezeigerwerte.
- Vorher-/Nachherwerte der drei im Disassembly beobachteten globalen Shader-/Material-/Technik-Kandidaten.

Die Paketfelder bei `+0`, `+8`, `+0x10` und `+0x1e` werden auf dem ursprünglichen Thread kopiert; die Originalfunktion greift selbst auf diese Felder zu. Es werden keine Engine-Zeiger an Worker zur Dereferenzierung weitergereicht. Die Tokens gelten nur innerhalb einer Sitzung, halten keine Ressourcen am Leben und beweisen keine Objektlebensdauer. Die Namen der globalen Zustände sind aus dem Code abgeleitet.

Die Aufzeichnung verwendet einen vorab reservierten Puffer mit 262.144 Einträgen. Volle Puffer führen zu gezählten Verlusten, nicht zu Überschreiben oder Blockieren des Originalrenderers. Datei-I/O läuft auf dem Diagnose-Koordinator; die CSV entsteht nach deaktivierter Erfassung und abgeschlossenen laufenden Aufrufen. Die Threadsicherheit des Kollektors erlaubt mehrere Aufrufer, sie macht die Engine-Funktion nicht threadfähig.

QPC erfasst verstrichene Zeit einschließlich Nachfahren, Unterbrechungen und möglicher Original-Wartezeiten. Es ist keine exklusive CPU-Zeit der Vorbereitung. Periodische Stichproben und Erfassungsaufwand begrenzen die Aussagekraft. Auch unveränderte Vorher-/Nachherwerte beweisen nicht, dass innerhalb des Aufrufs keine gemeinsamen Zustände geändert wurden.

## Installations- und Startnachweis

Installation: `measurements/render-installation-20261003-001118-468/manifest.json`.

- DLL-SHA-256: `7824255C86896B699ACA5DF37E8E04F03BECAF79048A44F31CE0B30BFA131E24`.
- INI-SHA-256: `68A02E7DB5874BEF3C4F13A878ACA327289D84DA1B29A5622FFC90C2ACCDD5CD`.
- Erster Spielprozess: 43608; Sitzung `measurements/live-render/session-43608-53956046/`.
- SKSE bestätigt `RenderPassProbe` Version 1 als korrekt geladen; der Plugin-Log bestätigt die Prüfung und die Originalweiterleitung.

Diese Hashes bezeichnen die installierten Dateien. Ein später neu gebautes lokales DLL-Artefakt kann durch Build-Metadaten einen anderen Hash besitzen.

## Tests und Aufnahme

Zehn CTest-Prüfungen bestanden vor der Installation; mit der anschließend ergänzten Berichtsvalidierung bestehen elf. Die neue native Prüfung enthält mehr als 5.000 Originalweiterleitungen mit Kontrolle aller vier Argumente, einen wirklich ausgeführten Win64-CALL-Hook, Ablehnung abweichender CALL-Bytes, vier gleichzeitige Aufrufer, Pufferverlustzählung und Originalausnahmen mit korrekter Aufräumlogik. Die DLL-Prüfung lehnt falsche Runtime, falsche EXE und ungültige SKSE-Schnittstellen ab. Die Python-Berichtsprüfung testet Zeitwerte und Zustandswechsel sowie die Ablehnung von 14 ungültigen Berichten; Verluste werden ausdrücklich als unvollständige Abdeckung ausgewiesen.

Aus dem Projektverzeichnis in PowerShell, erst nach bestätigter geladener Szene:

```powershell
.\tools\Start-RenderCapture.ps1
# Nach capture-complete, mit dem aktuellen Sitzungsverzeichnis:
python .\tools\Summarize-RenderCapture.py .\measurements\live-render\session-PID-TICK
```

`current-session.json` enthält Prozess-ID, Prozessstart und Sitzung. Der Starthelfer prüft diese Identität, die geladene DLL, den Plugin-Zustand und den erlaubten Ausgabepfad. Eine Aufnahme dauert standardmäßig 20 Sekunden und ist pro Sitzung einmalig.

## Bedeutung für die Parallelisierung

Diese Anbindung liefert echte Paketmengen, Verarbeitungsdauern und Hinweise auf gemeinsame Zustandsänderungen. Daraus lassen sich geeignete Passgrenzen für einen nächsten Eingriff auswählen. Vor einer tatsächlichen Worker-Ausführung müssen Ressourcenlebensdauer und Shader-/Materialzustände getrennt sowie die Ausführungsreihenfolge erhalten werden. Eine Weiterleitung der unveränderten Originalfunktion auf mehrere Threads ist damit noch nicht gerechtfertigt. [Engine-Entwurf](../research/MAIN-THREAD-004.md), [Hardware-Backend](../research/D3D11-PARALLEL-LAB.md)

Die erste Live-Aufnahme erfasst 3.144.070 Originalaufrufe und 196.505 gültige Stichproben ohne Verlust oder Abbruch. Alle Aufrufe kamen vom Main Thread; häufige globale Materialzustandsänderungen bestätigen die Trennungsanforderung. [Live-Bericht](../research/LIVE-RENDER-001.md)
