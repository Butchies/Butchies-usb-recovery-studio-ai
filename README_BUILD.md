# Butchies USB Recovery Studio AI – Entwicklungsstand 0.1.0

## Tatsächlich vorhandene Funktionen (programmiert)
- **Geräteerkennung** (`core/usb_inventory.h`): nur USB-Datenträger (Systemdatenträger ausgeschlossen), VID/PID, Seriennummer,
  Laufwerksbuchstaben, „kein Medium“, 0 Byte, Windows-Schreibschutzattribut, UAS; zusätzlich nicht erkannte
  USB-Massenspeicher mit Problemcode (z. B. Code 43). Registry-/Gruppenrichtlinien-Schreibschutz. Neue Sticks werden alle 4 s bemerkt.
- **SCSI-Abfragen** (`core/scsi_probe.h`): TEST UNIT READY, INQUIRY, Seriennummer (VPD 80), READ CAPACITY 10/16,
  MODE SENSE (Schreibschutzbit), READ(10) von Sektor 0, Sense-Codes im Klartext. Nur Standardbefehle, nichts Schreibendes.
- **Protokolle** (`core/event_logs.h`): Ereignisanzeige „System“ (disk, Ntfs, volmgr, partmgr, Kernel-PnP, StorPort, UASPStor,
  USBSTOR, USBHUB3, USBXHCI) und „Kernel-PnP/Configuration“ der letzten 14 Tage, Zuordnung zum Stick über Datenträgernummer,
  VID/PID und Geräte-IDs, setupapi.dev.log, Problemcode. Übersetzung und Gewichtung über `core/catalog.h`.
- **Lese-Probe und Oberflächentest** (`core/surface_scan.h`) mit Zeitlimits, Fehlerkarte und Geschwindigkeitskurve.
- **Partition/Dateisystem** (`core/partition_fs.h`): MBR, GPT, Superfloppy; FAT12/16/32, exFAT, NTFS-Bootsektoren
  samt Sicherungen; Suche nach verlorenen Partitionen und zerstörten Bootsektoren (Gegenprobe über die FAT).
- **Dateien retten** (`core/fs_reader.h`): FAT12/16/32 und exFAT mit Ordnern, langen Namen und gelöschten Einträgen,
  auch über die Parameter der Bootsektor-Sicherung.
- **Signatursuche** (`core/file_carver.h`): JPG, PNG, GIF, PDF, ZIP/DOCX/XLSX/PPTX/ODT/APK, MP4/MOV/HEIC/3GP/M4A, MKV/WebM,
  AVI/WAV/WEBP, BMP, MP3, 7z, RAR, DOC, TIFF.
- **Rettungsabbild** (`core/rescue_imager.h`): 4 Durchgänge, Mapdatei im ddrescue-Format, fortsetzbar.
- **Reparatur** (`core/repair_actions.h`): Rückgängig-Datei vor jedem Schreiben, Schreibschutzattribut entfernen,
  Registry-Schreibschutz zurücksetzen, USB-Gerät neu starten, Bootsektor aus Sicherung, MBR neu schreiben,
  eigene FAT32-Formatierung (auch > 32 GB und auf echte Größe begrenzt), exFAT/NTFS über diskpart, Schreibtest.
- **Kapazitätstest** (`core/capacity_test.h`) mit Wrap-around-Erkennung und Rückschreiben der Originaldaten.
- **KI-Agent** (`core/diagnosis_engine.h`, `core/agent.h`): Hypothesen mit Belegen (Noisy-OR), Datenrettungs-Chance,
  Reparierbarkeit, Aktionsplan von harmlos nach riskant, Freigabe-Dialog, Selbstprüfung nach jedem Schritt
  (neu erkennen und neu bewerten), automatischer Ablauf, Fallakten.
- **Wissensbasis** (`core/knowledge_store.h`): jeder Versuch und jede bestätigte/verworfene Diagnose je Kennung und
  Symptommuster; ähnliche Fälle verändern Gewichtung und Reihenfolge.
- **Oberfläche** (`main.cpp`, `Main.qml`): Geräte, Diagnose, Datenrettung, Reparatur, KI-Live-Monitor, Protokolle,
  Wissensbasis, Einstellungen, KI-Assistent (eingebaut oder lokale KI über OpenAI-kompatiblen Endpunkt), Fortschritt in Prozent.

## Geprüft
Der plattformunabhängige Kern wurde unter Linux mit Testabbildern durchgespielt: zerstörter FAT32-Bootsektor
(Befund, Dateirettung, Wiederherstellung, Selbstprüfung → „Kein Fehler gefunden“), gelöschte Partitionstabelle
(Neuaufbau), Lernen über zwei Läufe, Kapazitätstest mit Rückschreiben (Abbild danach bitgleich), Rettungsabbild
(bitgleich), Datei- und Signatursuche (bitgleiche Dateien).

## Noch nicht geprüft
- Der Windows-spezifische Teil (SetupAPI, SCSI-Durchreichung, Ereignisanzeige, diskpart) und die Qt-Oberfläche wurden
  noch **nicht unter Windows kompiliert** und nicht mit echten Sticks getestet.
- exFAT-Dateiliste noch nicht mit einem echten exFAT-Stick getestet.
- NTFS: nur Bootsektor-Prüfung/-Wiederherstellung und Signatursuche, keine Dateiliste.
- Kein generatives KI-Modell im Programm; die lokale KI ist optional über einen Endpunkt angebunden.
- Kein Installer.

## Bauen (Windows)
Voraussetzungen: Visual Studio 2022 (C++), Qt 6.5 oder neuer (MSVC 64 Bit), CMake ≥ 3.21.
```
cmake -S native -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
cmake --build build --config Release
C:\Qt\6.8.0\msvc2022_64\bin\windeployqt --qmldir native build\Release\Butchies-USB-Recovery-Studio-AI.exe
```
Das Programm verlangt beim Start Administratorrechte (Rohzugriff auf `\\.\PhysicalDriveN`).
Daten: `%APPDATA%\Butchies\Butchies USB Recovery Studio AI\` (Wissensbasis, Fallakten mit Rückgängig-Dateien).
