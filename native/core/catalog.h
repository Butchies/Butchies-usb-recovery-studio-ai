#pragma once
// Eingebautes Startwissen: USB-Controller-Hersteller, SCSI-Sense-Codes, Windows-Ereignisse, Kernel-Meldungen.
// Die Wissensbasis (knowledge_store.h) ergänzt das mit selbst gelernten Fällen.
#include "common.h"

namespace usbr {

struct ControllerHint {
    uint16_t vid;
    int pid;                 // -1 = alle
    const char *vendor;      // Hersteller des Controllers bzw. der USB-Kennung
    const char *family;      // Controllerfamilie
    const char *tool;        // Werkzeug für Neuflash/Wiederherstellung (nicht mitgeliefert)
    int fakeRisk;            // 0..3: wie oft diese Kennung bei Fake-Sticks auftaucht
    const char *note;
};

inline const std::vector<ControllerHint> &controllerHints() {
    static const std::vector<ControllerHint> v = {
        {0x0000, 0x0002, "–", "Gerätebeschreibung fehlgeschlagen", "", 0, "Windows konnte den Stick nicht ansprechen (Code 43). Controller antwortet nicht oder Kontakt/Strom fehlt."},
        {0x13FE, -1, "Phison", "Phison PS22xx", "Phison MPALL (passende Version zum exakten Controller, z. B. PS2251-07/-09)", 1, "Häufig in Kingston, Patriot, Corsair, PNY, Verbatim."},
        {0x0951, -1, "Kingston", "Phison oder SMI (je nach Modell)", "Phison MPALL bzw. SMI MPTool – Controller vorher mit ChipGenius bestimmen", 0, ""},
        {0x154B, -1, "PNY", "Phison (meist)", "Phison MPALL", 0, ""},
        {0x1B1C, -1, "Corsair", "Phison (meist)", "Phison MPALL", 0, ""},
        {0x090C, -1, "Silicon Motion", "SMI SM32xx/SM3257", "SMI MPTool (Version zu Controller und NAND passend)", 1, "Auch in Samsung-, Intenso- und vielen No-Name-Sticks."},
        {0x058F, -1, "Alcor Micro", "Alcor AU69xx/FC8xxx", "AlcorMP / FC MpTool", 2, "Oft in günstigen Sticks; Notmodus zeigt sich gern als 0 MB."},
        {0x1F75, -1, "Innostor", "Innostor IS90x/IS91x", "Innostor MPTool", 0, ""},
        {0x1E3D, -1, "Chipsbank", "Chipsbank CBM2xxx/CBM209x", "Chipsbank UMPTool / APTool", 2, ""},
        {0x0204, -1, "Chipsbank", "Chipsbank CBM2xxx", "Chipsbank UMPTool", 2, ""},
        {0x048D, -1, "ITE", "ITE IT1167/IT1172", "ITE DtMPTool / MPTool", 2, "ITE-Controller sind bei Fake-Kapazität sehr häufig."},
        {0x346D, 0x5678, "No-Name", "unbekannt (oft Fake 'Disk 2.0')", "Kein Hersteller-Werkzeug bekannt", 3, "Diese Kennung ist typisch für Sticks mit gefälschter Kapazität."},
        {0xABCD, -1, "No-Name", "unbekannt", "Kein Hersteller-Werkzeug bekannt", 3, "Platzhalter-Kennung, typisch für Billig- und Fake-Sticks."},
        {0x1307, -1, "USBest", "USBest UT16x", "USBest MPTool", 1, ""},
        {0x0781, -1, "SanDisk", "SanDisk-eigener Controller", "Kein öffentliches Werkzeug", 0, "SanDisk schaltet verschlissene Sticks dauerhaft auf 'nur lesen' – Daten lesbar, Reparatur nicht möglich."},
        {0x8564, -1, "Transcend", "Transcend JetFlash", "Transcend 'JetFlash Online Recovery' (offizielles Herstellerwerkzeug)", 0, ""},
        {0x1005, -1, "Apacer", "Apacer Handy Steno", "Apacer 'USB3.0 Repair Tool' (offizielles Herstellerwerkzeug)", 0, ""},
        {0x0930, -1, "Toshiba/Kioxia", "Toshiba-Controller", "Kein öffentliches Werkzeug", 0, ""},
        {0x125F, -1, "ADATA", "verschiedene (oft SMI/Phison)", "ADATA 'USB Flash Drive Online Recovery' bzw. Controller-MPTool", 0, ""},
        {0x18A5, -1, "Verbatim", "verschiedene", "Controller vorher bestimmen", 0, ""},
        {0x0BDA, -1, "Realtek", "Kartenleser", "", 0, "Das ist ein Kartenleser: Fehler liegt oft an der Speicherkarte, nicht am Leser."},
        {0x05E3, -1, "Genesys Logic", "Kartenleser/Hub", "", 0, "Kartenleser oder Hub – Speicherkarte bzw. angeschlossenes Gerät prüfen."},
        {0x14CD, -1, "Super Top", "Kartenleser", "", 0, "Kartenleser – Speicherkarte prüfen."},
        {0x3538, -1, "PQI", "verschiedene", "Controller vorher bestimmen", 1, ""},
        {0x13FD, -1, "Initio", "USB-SATA-Bridge", "", 0, "Eher eine USB-Festplatte/SSD als ein Stick."},
        {0x152D, -1, "JMicron", "USB-SATA/NVMe-Bridge", "", 0, "Eher ein Gehäuse mit Festplatte/SSD als ein Stick (für NVMe: NVMe Recovery Studio AI)."},
        {0x174C, -1, "ASMedia", "USB-SATA/NVMe-Bridge", "", 0, "Eher ein Gehäuse mit Festplatte/SSD als ein Stick (für NVMe: NVMe Recovery Studio AI)."},
        {0x0BC2, -1, "Seagate", "Festplatte", "", 0, "Externe Festplatte – kein USB-Stick."},
        {0x1058, -1, "Western Digital", "Festplatte", "", 0, "Externe Festplatte – kein USB-Stick."},
    };
    return v;
}

inline const ControllerHint *findController(uint16_t vid, uint16_t pid) {
    const ControllerHint *best = nullptr;
    for (const auto &c : controllerHints()) {
        if (c.vid != vid) continue;
        if (c.pid == int(pid)) return &c;
        if (c.pid == -1 && !best) best = &c;
    }
    return best;
}

// SCSI Sense Key / ASC / ASCQ in Klartext
inline std::string senseText(uint8_t key, uint8_t asc, uint8_t ascq) {
    struct E { uint8_t asc; int ascq; const char *t; };
    static const E table[] = {
        {0x04, 0x00, "Gerät nicht bereit (Ursache unbekannt)"},
        {0x04, 0x01, "Gerät wird gerade bereit"},
        {0x04, 0x02, "Gerät nicht bereit – Startbefehl nötig"},
        {0x04, -1, "Gerät nicht bereit"},
        {0x11, 0x00, "Nicht korrigierbarer Lesefehler (Speicherzellen defekt)"},
        {0x11, -1, "Lesefehler"},
        {0x0C, -1, "Schreibfehler"},
        {0x03, -1, "Schreibfehler des Mediums"},
        {0x10, -1, "ID-/CRC-Fehler"},
        {0x14, -1, "Aufgezeichneter Bereich nicht gefunden"},
        {0x21, 0x00, "Adresse außerhalb des Bereichs (Kapazitätsangabe stimmt nicht?)"},
        {0x24, 0x00, "Ungültiges Feld im Befehl (Befehl nicht unterstützt)"},
        {0x20, 0x00, "Befehl nicht unterstützt"},
        {0x25, 0x00, "Logische Einheit nicht unterstützt"},
        {0x27, 0x00, "Schreibgeschützt (vom Controller gemeldet)"},
        {0x27, -1, "Schreibgeschützt"},
        {0x28, 0x00, "Medium gewechselt / neu bereit"},
        {0x29, -1, "Gerät wurde zurückgesetzt (Power-On/Reset)"},
        {0x3A, -1, "Kein Medium vorhanden ('Bitte Datenträger einlegen') – typisch für Controller im Notmodus"},
        {0x44, -1, "Interner Controllerfehler"},
        {0x31, -1, "Medium-Format beschädigt (Controller-Übersetzungstabelle?)"},
        {0x5D, -1, "Ausfallvorhersage: Gerät wird bald versagen"},
    };
    std::string k;
    switch (key & 0x0F) {
    case 0x0: k = "Kein Fehler"; break;
    case 0x1: k = "Korrigierter Fehler"; break;
    case 0x2: k = "Nicht bereit"; break;
    case 0x3: k = "Mediumfehler"; break;
    case 0x4: k = "Hardwarefehler"; break;
    case 0x5: k = "Ungültige Anfrage"; break;
    case 0x6: k = "Gerätehinweis"; break;
    case 0x7: k = "Datenschutz (Schreibschutz)"; break;
    case 0xB: k = "Befehl abgebrochen"; break;
    default: k = "Sense-Key " + hex2(key);
    }
    for (const auto &e : table)
        if (e.asc == asc && (e.ascq < 0 || e.ascq == ascq)) return k + ": " + e.t;
    return k + fmt(" (ASC %02X/%02X)", asc, ascq);
}

// Windows-Ereignisse aus dem Systemprotokoll
struct EventInfo { const char *provider; int id; const char *category; int weight; const char *text; };
inline const std::vector<EventInfo> &eventCatalog() {
    static const std::vector<EventInfo> v = {
        {"disk", 7, "bad", 3, "Fehlerhafter Block auf dem Datenträger – Speicherzellen defekt"},
        {"disk", 11, "controller", 3, "Controllerfehler am Datenträger"},
        {"disk", 15, "notready", 2, "Datenträger noch nicht bereit für Zugriffe"},
        {"disk", 51, "io", 2, "Fehler bei Auslagerungs-/E-A-Vorgang"},
        {"disk", 52, "predict", 3, "Datenträger meldet bevorstehenden Ausfall"},
        {"disk", 153, "io", 2, "E/A-Vorgang wurde wiederholt"},
        {"disk", 157, "connection", 2, "Datenträger wurde unerwartet entfernt"},
        {"Ntfs", 55, "fs", 2, "NTFS-Dateisystemstruktur beschädigt"},
        {"Ntfs", 98, "fs", 1, "NTFS-Volume muss geprüft werden"},
        {"Ntfs", 137, "fs", 1, "NTFS-Transaktionsfehler"},
        {"Ntfs", 140, "io", 2, "NTFS konnte Daten nicht schreiben (Gerät fehlerhaft/entfernt)"},
        {"Microsoft-Windows-Ntfs", 55, "fs", 2, "NTFS-Dateisystemstruktur beschädigt"},
        {"Microsoft-Windows-Ntfs", 98, "fs", 1, "NTFS-Volume muss geprüft werden"},
        {"Microsoft-Windows-Ntfs", 140, "io", 2, "NTFS konnte Daten nicht schreiben (Gerät fehlerhaft/entfernt)"},
        {"volmgr", 46, "io", 1, "Absturzabbild-Initialisierung fehlgeschlagen"},
        {"volmgr", 161, "io", 1, "Volume-Fehler"},
        {"partmgr", 58, "partition", 2, "Partitionstabelle ungültig oder beschädigt"},
        {"Microsoft-Windows-Partition", 1006, "info", 0, "Partitionsdiagnose (Information)"},
        {"Microsoft-Windows-Kernel-PnP", 219, "driver", 1, "Treiber konnte für das Gerät nicht geladen werden"},
        {"Microsoft-Windows-Kernel-PnP", 411, "pnp", 2, "Gerät konnte nicht gestartet werden (Problemcode)"},
        {"Microsoft-Windows-Kernel-PnP", 225, "connection", 1, "Gerät konnte wegen Zugriff nicht entfernt werden"},
        {"Microsoft-Windows-StorPort", 549, "io", 2, "Speicheranschluss meldet Fehler/Zeitüberschreitung"},
        {"USBHUB3", 43, "connection", 3, "USB-Gerätebeschreibung konnte nicht gelesen werden"},
        {"Microsoft-Windows-USB-USBHUB3", 43, "connection", 3, "USB-Gerätebeschreibung konnte nicht gelesen werden"},
        {"Microsoft-Windows-USB-USBHUB3", 10, "connection", 2, "USB-Port zurückgesetzt"},
        {"Microsoft-Windows-USB-USBXHCI", -1, "connection", 1, "USB-Controller meldet Problem"},
        {"UASPStor", -1, "io", 2, "USB-Attached-SCSI-Treiber meldet Fehler"},
        {"USBSTOR", -1, "io", 2, "USB-Massenspeichertreiber meldet Fehler"},
    };
    return v;
}

inline const EventInfo *findEvent(const std::string &provider, int id) {
    const EventInfo *any = nullptr;
    for (const auto &e : eventCatalog()) {
        if (provider != e.provider) continue;
        if (e.id == id) return &e;
        if (e.id == -1) any = &e;
    }
    return any;
}

// Windows-Geräte-Problemcodes (Geräte-Manager)
inline std::string problemCodeText(uint32_t code) {
    switch (code) {
    case 0: return "";
    case 10: return "Code 10: Gerät kann nicht gestartet werden";
    case 28: return "Code 28: Kein Treiber installiert";
    case 31: return "Code 31: Gerät funktioniert nicht richtig";
    case 39: return "Code 39: Treiber beschädigt oder fehlt";
    case 43: return "Code 43: Gerät hat Fehler gemeldet (USB-Beschreibung fehlgeschlagen)";
    case 45: return "Code 45: Gerät nicht angeschlossen";
    case 22: return "Code 22: Gerät ist deaktiviert";
    default: return fmt("Problemcode %u", code);
    }
}

// Kernel-Meldungen (Linux, dmesg) – für die spätere Linux-Edition und Entwicklung
struct KernelPattern { const char *needle; const char *category; int weight; const char *text; };
inline const std::vector<KernelPattern> &kernelPatterns() {
    static const std::vector<KernelPattern> v = {
        {"device descriptor read", "connection", 3, "USB-Gerätebeschreibung nicht lesbar"},
        {"error -71", "connection", 3, "USB-Protokollfehler (-71): Kontakt, Kabel oder Controller"},
        {"reset high-speed USB device", "connection", 2, "USB-Gerät wurde zurückgesetzt"},
        {"reset SuperSpeed USB device", "connection", 2, "USB-Gerät wurde zurückgesetzt"},
        {"Medium Error", "bad", 3, "Mediumfehler – Speicherzellen defekt"},
        {"Unrecovered read error", "bad", 3, "Nicht korrigierbarer Lesefehler"},
        {"critical medium error", "bad", 3, "Kritischer Mediumfehler"},
        {"I/O error", "io", 2, "E/A-Fehler"},
        {"Buffer I/O error", "io", 2, "Puffer-E/A-Fehler"},
        {"Write Protect is on", "writeprotect", 3, "Controller meldet Schreibschutz"},
        {"unable to read partition table", "partition", 2, "Partitionstabelle nicht lesbar"},
        {"Medium not present", "nomedia", 3, "Kein Medium – Controller im Notmodus?"},
        {"FAT-fs", "fs", 1, "FAT-Dateisystem meldet Fehler"},
        {"exFAT-fs", "fs", 1, "exFAT-Dateisystem meldet Fehler"},
        {"ntfs", "fs", 1, "NTFS meldet Fehler"},
    };
    return v;
}

} // namespace usbr
