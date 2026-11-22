# Butchies USB Recovery Studio AI

Windows-Programm, das defekte USB-Sticks selbst untersucht, erklärt, **warum** sie nicht mehr funktionieren,
Daten rettet und Reparaturversuche durchführt. Ein KI-Agent misst, liest die Windows-Protokolle aus, bewertet
alles mit Belegen und Sicherheit in Prozent, plant die Schritte, fragt vor jedem Schreibzugriff, prüft danach
selbst das Ergebnis und lernt aus jedem Fall.

Verbindliche Spezifikation: [`AGENTS.md`](AGENTS.md) · Entwicklungsstand und Bauen: [`README_BUILD.md`](README_BUILD.md)

## Was der Agent erkennt

| Befund | Woran er es erkennt | Daten | Reparatur |
|---|---|---|---|
| Stick wird nicht erkannt (Code 43, 0000:0002) | Geräte-Manager-Problemcode, USB-Protokolle | kaum per Software | Neustart des Geräts, andere Buchse – sonst Hardware |
| Controller im Notmodus | „kein Medium“, 0 MB / Mini-Größe, Sense 3A, nur 00/FF | gering | Neuflash mit Herstellerwerkzeug (Anleitung) |
| Speicher verschlissen, nur lesen | MODE-SENSE-Schreibschutzbit, Schreibtest | sehr gut | nicht dauerhaft |
| Schreibschutz durch Windows | Datenträgerattribut, Registry, Gruppenrichtlinie | vollständig | Attribut/Richtlinie entfernen |
| Partitionstabelle defekt | MBR/GPT ungültig, Dateisystem trotzdem gefunden | sehr gut | Tabelle neu schreiben |
| Dateisystem defekt (RAW) | Bootsektor ungültig, Sicherung intakt | sehr gut | Bootsektor aus Sicherung |
| Defekte Speicherbereiche | Lesefehler, Ereignis disk 7/11/153, Oberflächentest | gut | eingeschränkt |
| Fake-Kapazität | Kapazitätstest, typische Fake-Kennungen | im echten Bereich | auf echte Größe einrichten |
| Verbindungsproblem | Resets/Trennungen in Protokollen, Zeitüberschreitungen | gut | Buchse/Kabel, Neustart |
