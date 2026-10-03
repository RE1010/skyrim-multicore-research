# Zweiter Bewegungstest: Abdeckung des Kandidaten

2. Oktober 2026, SKSE-Prozess 38732, vom Benutzer bestätigte Außenszene vor Weißlauf mit 600 zusätzlich gespawnten Wachen. Plugin-Version 2, DLL-SHA256 `87DEBFF30BF5B8BAC4E99E0DC2A868B796ADEF5ED2641925DDD2D2B702DF9D76`. Sitzung: `measurements/live-movement/session-38732-49623171/`.

## Gültige Aufnahme, unzureichende Abdeckung

Die einmalige Aufnahme lief 20,1525 Sekunden und erfasste 426.263 abgeschlossene Originalaufrufe, fünf positive und 426.258 negative Ergebnisse. Die 6.665 Stichproben entsprechen exakt der Summe der pro Thread erwarteten Stichproben. Keine verlorenen Stichproben, abgebrochenen Aufrufe, Thread-Überläufe oder aktiven Schreiber nach Ende. Alle akzeptierten Getter-Codebytes bestanden die erneute Prüfung vor Messbeginn.

Listenmedian 11.019, Maximum 51.524. Es gab 1.321 vollständige Kandidatenvergleiche ohne Abweichungen; 1.308 davon betrafen leere Listen, 13 kurze Listen mit höchstens acht Elementen. Alle 5.344 übrigen Stichproben brachen wegen des unbekannten Getters `0x7a6b10` ab. Insbesondere wurde **keine lange Liste vollständig mit dem Kandidaten berechnet**. Deshalb ist die Null-Abweichungszahl keine Bestätigung des relevanten Engpasspfads. Alle fünf positiven Originalergebnisse liegen außerhalb der zeitlichen Stichproben.

Der vollständige Originalaufruf inklusive stichprobenartigem Kandidatenaufwand dauerte im Mittel 52,67 µs. Nach Subtraktion der Kandidatenzeit beträgt die Originalschätzung 51,99 µs, weiterhin Wandzeit einschließlich Lock und Diagnose. Die steigende Aufrufrate gegenüber Test 1 ist kein Performancebeleg: Szene, Scheduling und Last wurden nicht als kontrollierter A/B-Vergleich erfasst. Es gab hier keine FPS-Aufnahme und keinen tatsächlichen Ersatz der Originalsuche.

CSV-SHA256 `63A7F7C40008866B77C3FF55BA97CBC4003D2D679FE618284812986AAFF11344`. `analysis.json` und `quality-check.json` enthalten die geprüften Zahlen. Das Plugin ist nach Abschluss inaktiv und leitet Originalaufrufe weiter.

## Erklärung der fehlenden Abdeckung

Die lokale, unveränderte EXE zeigt bei `0x7a6b10` einen direkten JMP auf den vollständigen 131-Byte-Getter `0x7a6b20`. Dieser verwendet MSVC-TLS-Epoch-Prüfung und threadgesicherte Initialisierung für den Typ `MovementMessageActorCollision`. Der Typwert liegt bei `0x324fdf8`, sein Initialisierungs-Guard bei `0x324fdfc`. Die ursprünglich erkannten einfachen Getterformen deckten diese Form nicht ab.

Die vollständige Disassemblierung und die originalen Header-/Footer-Helfer bei `0x15a7d0c` und `0x15a7cac` zeigen Guard 0 für uninitialisiert, -1 für laufende Initialisierung und eine veröffentlichte Epoch nach erfolgreicher Initialisierung. Ein Kandidat darf einen bereits fertig initialisierten, nichtnulligen Typwert nur lesen, wenn der konservative Guard-Test einen Wert kleiner -1 ergibt. Bei allen anderen Guardzuständen bleibt er unvollständig. Microsoft dokumentiert TLS als Bestandteil der threadgesicherten Initialisierung lokaler statischer Variablen: [Microsoft Learn](https://learn.microsoft.com/en-us/cpp/build/reference/zc-threadsafeinit-thread-safe-local-static-initialization?view=msvc-170).

Diese neue Beschreibung ist weiterhin ausschließlich ein readonly Ergebnisvergleich. Das Original führt seine gesamte TLS-/Initialisierungsarbeit aus. Sie beweist nicht, dass TLS-Buchhaltung in einem späteren echten Ersatzpfad beliebig weggelassen werden darf.

## Vorbereitete Ergänzung

Plugin-Version 3 unterstützt zusätzlich diese eine live beobachtete Weiterleitung. Der JMP, der gesamte Getter-Körper und beide Initialisierungshelfer werden beim Start und vor der Aufnahme mit der privaten lokalen Evidenz verglichen. Die Kandidatenmenge beträgt nun 1.123 Körper/Beschreibungen. Die neue DLL ist gebaut, noch nicht im laufenden Spiel installiert.

Alle acht CTest-Prüfungen bestehen. Der native Kandidatentest enthält jetzt 4.041 Originalcode-Aufrufe. Vier zusätzliche Fälle prüfen den tatsächlichen relocierten 131-Byte-Getter mit GS/TLS-Fastpfad und kontrollierten kalten Helfern; TLS-Index und Epoch-Offset verweisen im Labor auf die eigene Testanwendung. Der Laborlauf `MOVEMENT-GUARDED-LAB.json` zeigt auch mit dieser Form bisher keinen Zeitgewinn. Der nächste Live-Vergleich dient deshalb der Abdeckung und Bewertung, nicht einer behaupteten Beschleunigung.

Ein erneuter normaler Spielstart ist notwendig, weil die bereits geladenen CALL-Weiterleitungen und ihre DLL bis zum Prozessende gehalten werden. Die vorhandene Szene wird nicht per laufendem Codeaustausch verändert.
