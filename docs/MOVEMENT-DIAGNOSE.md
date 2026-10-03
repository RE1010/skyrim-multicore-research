# Diagnose der Bewegungsnachrichten-Suche

Stand: 2. Oktober 2026. `MovementMessageProbe.dll` ist ein zusätzlicher SKSE-Diagnosebaustein für die lokal geprüfte SkyrimSE.exe 1.7.104. Er ersetzt keine Suchentscheidung, führt kein Caching ein und beansprucht noch keinen FPS-Gewinn. Der gefundene Engpass läuft bereits auf vorhandenen Engine-Workern; eine günstigere Suche könnte diese Jobs und dadurch den wartenden Main Thread entlasten.

Dieses Dokument beschreibt die erste Diagnoseversion und deren abgeschlossenen Live-Test. Version 2 ergänzt einen zusätzlichen **stichprobenartigen Scan unter dem Original-Lock**; damit gelten die folgenden Aussagen über fehlende zusätzliche Iteration nur für Version 1. Neue Felder, Tests und Grenzen: [Bewegungsscan-Prototyp](MOVEMENT-FAST-PROTOTYP.md). Die Auswertung unterstützt beide Berichtsformate.

## Messpunkte und Schutz

Vor Installation prüft das Plugin den EXE-SHA256, die vollständigen 226 Bytes der Suchfunktion bei RVA `0x797b70`, die 187 Bytes des originalen Lock-Konstruktors bei `0x199fa0` und den direkten Suchaufruf bei `0x673d68`. Das Build gewinnt diese Evidenz ausschließlich aus der erlaubten lokalen EXE; die Originalbytes bleiben im ignorierten Build-Verzeichnis.

Zwei bereits vorhandene CALL-Instruktionen werden ausschließlich beim SKSE-Start auf kleine Weiterleitungen im offiziellen SKSE-Branch-Pool umgebunden:

- Der Aufruf bei `0x673d68` misst die originale Suche und gibt unverändert deren Ergebnis zurück.
- Der Aufruf des Lock-Konstruktors innerhalb dieser Suche bei `0x797b9b` führt den originalen Konstruktor aus und liest anschließend die Elementzahl bei `controller + 0x168`. Dabei ist der originale Lock bei `controller + 0x150` bereits gehalten. Der Originalcode übernimmt auch das Freigeben des Locks.

Es gibt keinen zusätzlichen Engine-Lock, keine zusätzliche Iteration über die Liste und keine zusätzlichen virtuellen Nachrichtenabfragen. Die ursprüngliche Suchfunktion läuft weiterhin genau einmal pro erfasstem Aufruf. Die vorhandenen Lock- und Typresolver-Implementierungen bleiben erhalten. Die beiden CALL-Weiterleitungen und ihre DLL werden für die Prozesslebensdauer gehalten; Start und Ende einer Messung ändern nur Diagnoseflags, niemals laufenden Maschinencode.

Der offizielle SKSE-2.3.1-Quellcode im lokal heruntergeladenen SDK bestätigt Trampoline-Interface 7 und dessen ABI sowie Plugin-Load vor dem Spielstart. Es werden keine vermuteten Address-Library-IDs benutzt. Fremde Änderungen an den geprüften Funktionen führen zur Ablehnung des Plugins.

## Erfasste Größen

Alle durch **diese eine Aufrufstelle** gehenden, während der Messung abgeschlossenen Suchaufrufe werden je Thread gezählt, einschließlich wahr/falsch. Andere Aufrufstellen sind nicht vollständig erfasst.

Standardmäßig werden pro Thread der erste und anschließend jeder 64. Aufruf zeitlich gemessen. Maximal 65.536 numerische Stichproben und 128 Thread-Zähler sind vorab reserviert. Nach Ende der Aufnahme und Abschluss aller zugelassenen Schreiber veröffentlicht der Koordinator:

- Aufrufzahlen, Ergebnisse und Thread-Verteilung;
- QPC-Wandzeit der Originalfunktion und des Lock-Erwerbs;
- unter dem Original-Lock beobachtete Listenlänge;
- einen pro Prozess codierten Controller-Token und die Stichprobensequenz;
- verworfene Stichproben bei ausgeschöpfter Kapazität, Thread-Überläufe und abgebrochene Aufrufe.

Der Koordinator dereferenziert keine Engine-Objekte. Die Token sind keine Actor-FormIDs. Dauerwerte enthalten Diagnoseaufwand, Scheduling und mögliche Lock-Wartezeit; sie sind keine exklusive CPU-Zeit. Systematische Stichproben können mit periodischer Arbeit zusammenfallen. Exakte Mutationsstellen, gleiche-lange Ersetzungen, Anzahl durchsuchter Einträge und Typen einzelner Nachrichten werden noch nicht aufgezeichnet. Unterschiedliche beobachtete Listenlängen zeigen Veränderungen zwischen Stichproben, beweisen aber keine konkrete Ursache.

## Prüfung und Installation

Alle sieben CTest-Tests bestehen. Der neue native Test führt **4.026 Suchen mit dem originalen Maschinencode** im isolierten Testprozess aus, mit Null-Einträgen, fehlenden und früh/spät gefundenen Typen, Listen bis 10.000 Einträge, ursprünglichem lazy Typresolver-Zweig und dem originalen rekursiven Lock-Konstruktor. Acht Threads prüfen 4.000 gleichzeitige Suchaufrufe auf einem gemeinsamen Controller. Zusätzlich werden Ablehnung falscher CALL-Bytes, Sampling bei voller Kapazität, Ausnahmeweitergabe und Last-Error-Erhalt geprüft. Das Labor verwendet kontrollierte Objekte und relocierte WinAPI-/Typresolver-Abhängigkeiten; es ersetzt keinen Spieltest.

Ein eigener DLL-Guard lehnt falsche Version/EXE und Null-ABI ab. Die Auswertung prüft Zähler, Stichprobensequenzen, Zeitwerte und genau einen beobachteten Original-Lock pro Datensatz. Sie lehnt unvollständige Aufnahmen ab. Die PowerShell-Skripte sind syntaktisch geprüft.

Bei regulär beendetem Skyrim:

```powershell
.\tools\Install-MovementProbe.ps1 -PauseVisibility
```

Es werden ausschließlich DLL und INI des neuen Diagnosebausteins installiert, mit Hashprüfung, Vorgängersicherungen und Manifest unter `measurements/movement-installation-*/`. `-PauseVisibility` sichert und benennt die ältere Sichtbarkeits-DLL vorübergehend auf `.dll.movement-test-disabled` um, damit deren wartender Diagnosepfad diese Aufnahme nicht zusätzlich beeinflusst. Die INI und SKSE bleiben erhalten.

Nach Start über SKSE eine reproduzierbare Szene laden. Der neue Bericht liegt unter `measurements/live-movement/current-session.json`. Erst bei `waiting-for-trigger`:

```powershell
.\tools\Start-MovementCapture.ps1
# Nach capture-complete:
$session=(Get-Content .\measurements\live-movement\current-session.json | ConvertFrom-Json).sessionDirectory
python .\tools\Summarize-Movement.py $session
```

Die Aufnahme läuft ungefähr 20 Sekunden zuzüglich Abschluss bereits zugelassener Aufrufe. Zum Rückbau bei beendetem Spiel die neue DLL aus dem SKSE-Plugins-Verzeichnis verschieben und die pausierte Sichtbarkeits-DLL auf ihren ursprünglichen Namen zurücksetzen; das Installationsmanifest enthält Pfade und Sicherungen. Die EXE-Datei auf der Festplatte wird nicht geändert.

Ein bestandener Live-Test setzt nichtleere gültige Datensätze, konsistente Zähler und keine Diagnosefehler voraus. Erst danach lässt sich entscheiden, ob Listenlängen, viele Wiederholungen oder Lock-Wartezeiten eine Optimierung rechtfertigen. Ein Cache braucht zusätzlich nachgewiesene Änderungs- und Lebensdauerregeln.

## Lokaler Startnachweis

Am 2. Oktober um 22:23 Uhr wurden die zwei Dateien installiert und die Sichtbarkeits-DLL gesichert/pausiert. Manifest: `measurements/movement-installation-20261002-222352/manifest.json`. Die installierte Bewegungs-DLL besitzt SHA256 `642FC226B91BA3A12BF6B85C14A5FCF8A5D55979B2990BC1D7CD54B4EDEBC7EC`.

Der SKSE-Start um 22:24 Uhr erzeugte Skyrim-Prozess 1464. Die aktuelle Modulliste enthält SKSE und `MovementMessageProbe.dll`, ohne die pausierte Sichtbarkeits-DLL. Das neue Plugin bestätigte alle Runtime-/Funktions-/Aufrufstellenprüfungen. Sitzung: `measurements/live-movement/session-1464-47523937/`, mit `startup-verification.json`, `plugin.log` und `summary.json`.

Die bestätigte Außenszene wurde um 22:27 Uhr aufgenommen: 384.888 abgeschlossene Originalaufrufe und 6.016 gültige Stichproben, keine Kapazitätsverluste oder abgebrochenen Aufrufe. Listenmedian 11.019, Maximum 50.755 Einträge; fünf positive Ergebnisse insgesamt. Einzelheiten, Grenzen und nächster Optimierungsansatz: [Live-Test](../research/LIVE-MOVEMENT-001.md).
