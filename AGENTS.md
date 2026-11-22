# Butchies USB Recovery Studio AI – Master-Spezifikation

Verbindlich für alle weiteren Arbeiten. Keine eigenen Zusatzfunktionen oder Abweichungen ohne Freigabe.

## Ziel
Windows-Programm (Windows 10/11), das defekte USB-Sticks erkennt, selbst auswertet, warum sie nicht mehr funktionieren,
Daten rettet und Reparaturversuche durchführt. Der Benutzer soll keine Protokolle selbst interpretieren müssen.

## Edition
- Zunächst nur Windows (C++20, Qt 6, QML). Eine bootfähige Linux-Edition kann später folgen.
- Ausschließlich per USB angeschlossene Datenträger. Interne Laufwerke werden nie als Ziel angeboten.

## KI-Agent (von Programmstart an aktiv)
- Sammelt Messwerte: Geräteerkennung (VID/PID, Controller-Familie), SCSI-Abfragen (INQUIRY, READ CAPACITY,
  MODE SENSE Schreibschutzbit, Sense-Codes), Lese-Proben, Oberflächentest, Dateisystem- und Partitionsanalyse.
- Liest Protokolle selbst aus: Windows-Ereignisanzeige (disk, Ntfs, volmgr, partmgr, Kernel-PnP, USB-Hub),
  setupapi.dev.log, Geräteproblemcodes (z. B. Code 43), Registry-Schreibschutzrichtlinie.
- Bewertet alles als Hypothesen mit Belegen und Sicherheit in Prozent und zeigt in Klartext, was es ist,
  ob Daten rettbar sind und ob der Stick reparierbar ist.
- Unternimmt Reparaturversuche selbst, von harmlos nach riskant. Nach jedem Schritt prüft er selbst das Ergebnis.
- Lernt aus jedem Versuch (lokale Wissensbasis je VID/PID/Controller/Symptom) und probiert bei ähnlichen Sticks
  zuerst, was schon funktioniert hat. Benutzer kann Diagnosen bestätigen oder verwerfen.
- Optional: Anbindung an eine lokale KI (OpenAI-kompatibler Endpunkt, z. B. Butchies KI Studio) für Erklärungen
  und Rückfragen, ohne Internet.
- KI-Live-Monitor: Messungen, Hypothesen, Entscheidungen, Aktionen, Prüfungen und Lernschritte nachvollziehbar anzeigen.

## Sicherheitsregeln
- Zuerst immer lesend arbeiten. Rettungsabbild vor jedem Schreibzugriff empfehlen; der Kapazitätstest
  (Fake-Erkennung) ist ohne vorhandenes Abbild oder ausdrückliche Bestätigung „Stick ist leer“ gesperrt.
- Jeder Schreibzugriff nur nach ausdrücklicher Freigabe im Dialog mit verständlicher Risikoangabe.
- Vor jedem Schreiben werden die betroffenen Originalsektoren in eine Rückgängig-Datei gesichert.
- Controller-Neuflash nur über die Hersteller-Werkzeuge (nicht mitgeliefert): das Programm benennt Controller,
  passendes Werkzeug und Risiko, führt aber nichts davon selbst aus.

## Funktionen
- Geräte: USB-Sticks inklusive „kein Medium“, 0-MB-Geräte und nicht erkannte USB-Geräte mit Problemcode.
- Diagnose mit Befund, Sicherheit, Datenrettungs-Chance, Reparierbarkeit, Belegen und übersetzten Protokolleinträgen.
- Datenrettung: Rettungsabbild (mehrere Durchgänge, Mapdatei, fortsetzbar, kurze Zeitlimits),
  Dateien aus FAT12/16/32 und exFAT inklusive gelöschter Einträge, Signatursuche (Carving) für alle Dateisysteme.
- Reparatur: Windows-Schreibschutzattribut entfernen, USB-Gerät neu starten, Bootsektor aus Sicherung
  wiederherstellen (FAT32/exFAT/NTFS), Partitionstabelle neu schreiben, Fake-Stick auf echte Größe einrichten,
  Neuformatierung (FAT32/exFAT nach Wahl über Windows), Controller-Neuflash-Anleitung.
- Fortschritt bei langen Vorgängen in Prozent.

## Gestaltung
Helle, aufgeräumte Oberfläche im Stil von NVMe Recovery Studio AI: Navigation links, Arbeitsfläche, KI-Assistent rechts.

## Arbeitsweise
- Bei „weiter“ direkt die nächste konkrete Funktion umsetzen.
- Keine Tests, Installer oder ZIPs ohne ausdrückliche Aufforderung.
- Keine Funktion als fertig darstellen, solange sie nicht tatsächlich existiert.
