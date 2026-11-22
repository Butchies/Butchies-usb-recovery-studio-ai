#pragma once
// KI-Bewertung: führt alle Messwerte und Protokolle zusammen, bildet Hypothesen mit Belegen,
// berechnet die Sicherheit (Noisy-OR über gewichtete Belege, ergänzt um gelernte Erfahrung)
// und leitet Datenrettungs-Chance, Reparierbarkeit und einen Aktionsplan ab.
#include "usb_inventory.h"
#include "scsi_probe.h"
#include "surface_scan.h"
#include "partition_fs.h"
#include "event_logs.h"
#include "capacity_test.h"
#include "knowledge_store.h"
#include <map>
#include <optional>

namespace usbr {

struct DeviceFacts {
    UsbDevice dev;
    HostPolicy host;
    ScsiReport scsi;
    ProbeReport probe;
    LayoutReport layout;
    LogReport logs;
    std::optional<ScanReport> scan;
    std::optional<CapacityResult> capacity;
    std::optional<ActionResult> writeTest;
    bool imageAvailable = false;
    std::string imagePath;
    const ControllerHint *controller = nullptr;
};

enum class Risk { Lesend = 0, Harmlos = 1, Mittel = 2, Loeschend = 3 };
inline const char *riskName(Risk r) {
    switch (r) { case Risk::Lesend: return "nur lesend"; case Risk::Harmlos: return "harmlos"; case Risk::Mittel: return "schreibt Verwaltungsdaten (rückgängig machbar)"; default: return "löscht alle Daten"; }
}

struct PlannedAction {
    std::string id, title, why;
    Risk risk = Risk::Lesend;
    bool needsImage = false;      // erst nach Rettungsabbild
    bool needsTarget = false;     // braucht einen Zielordner
    bool manual = false;          // nur Anleitung, führt das Programm nicht selbst aus
    double learnedRate = -1;      // Erfahrungswert aus der Wissensbasis
    int learnedTries = 0;
};

struct Hypothesis {
    std::string id, title, explanation;
    double confidence = 0;        // 0..1
    std::vector<std::string> evidence;
    std::string dataChance;       // Klartext
    int dataPercent = 0;
    std::string repair;           // Klartext
    bool repairable = false;
    std::vector<std::string> actions;
};

struct Diagnosis {
    std::vector<Hypothesis> hypotheses;   // sortiert nach Sicherheit
    std::vector<PlannedAction> plan;
    std::string symptoms;                 // Symptomschlüssel für die Wissensbasis
    std::string headline;
    int dataPercent = 0;
    bool repairable = false;
    std::vector<std::string> facts;       // wichtigste Messwerte im Klartext
    const Hypothesis *top() const { return hypotheses.empty() ? nullptr : &hypotheses.front(); }
};

inline std::string symptomKey(const DeviceFacts &f) {
    std::vector<std::string> s;
    if (!f.dev.isDisk) s.push_back("nodisk");
    if (f.dev.problemCode) s.push_back("code" + std::to_string(f.dev.problemCode));
    if (f.dev.isDisk && !f.dev.mediaPresent) s.push_back("nomedia");
    if (f.dev.isDisk && f.dev.mediaPresent && f.dev.size < (64ull << 20)) s.push_back("tiny");
    if (f.scsi.writeProtectBit == 1) s.push_back("wpbit");
    if (f.dev.readOnlyAttr) s.push_back("roattr");
    if (f.host.registryWriteProtect || f.host.policyDenyWrite) s.push_back("hostpolicy");
    if (f.layout.tableDamaged) s.push_back("table");
    if (f.layout.fsDamaged) s.push_back("fs");
    if (f.layout.fsRepairableFromBackup) s.push_back("backup");
    if (f.layout.sector0Pattern == Pattern::Zeros) s.push_back("zero0");
    if (f.layout.sector0Pattern == Pattern::Ones) s.push_back("ff0");
    if (f.probe.errors || f.probe.timeouts) s.push_back("readerr");
    if (f.scan && f.scan->badBlocks) s.push_back("bad");
    if (f.capacity && f.capacity->fake) s.push_back("fake");
    if (f.layout.beyondEnd) s.push_back("beyond");
    if (f.logs.count("connection") >= 3) s.push_back("conn");
    std::string k; for (auto &x : s) { if (!k.empty()) k += ','; k += x; }
    return k;
}

class DiagnosisEngine {
public:
    explicit DiagnosisEngine(const KnowledgeStore *kb = nullptr) : kb_(kb) {}

    Diagnosis evaluate(const DeviceFacts &f, LogFn log = {}) {
        Diagnosis d;
        d.symptoms = symptomKey(f);
        facts(f, d);
        std::map<std::string, Hypothesis> H;
        auto add = [&](const std::string &id, double w, const std::string &why) {
            auto &h = H[id]; h.id = id;
            h.confidence = 1.0 - (1.0 - h.confidence) * (1.0 - std::clamp(w, 0.0, 0.99));
            h.evidence.push_back(fmt("%s (+%d %%)", why.c_str(), int(w * 100 + 0.5)));
        };
        const auto &dv = f.dev;
        const auto &sc = f.scsi;
        const auto *ct = f.controller;
        const bool readOk = f.probe.ok > 0;
        const int samples = f.probe.ok;
        const double blankShare = samples ? double(f.probe.zeros + f.probe.ones + f.probe.repeating) / samples : 0;

        // --- Gerät nicht oder nur als USB-Gerät erkannt
        if (!dv.isDisk) add("not_detected", 0.6, "Windows erkennt nur ein USB-Gerät, aber keinen Datenträger");
        if (dv.problemCode == 43) add("not_detected", 0.6, problemCodeText(43));
        if (dv.hasVidPid && dv.vid == 0 && dv.pid == 2) add("not_detected", 0.7, "Kennung 0000:0002 – Gerätebeschreibung konnte nicht gelesen werden");
        if (dv.problemCode && dv.problemCode != 43) add("not_detected", 0.3, problemCodeText(dv.problemCode));
        if (!dv.isDisk && f.logs.count("connection") >= 3) add("not_detected", 0.2, "Protokolle zeigen wiederholte USB-Verbindungsfehler");

        // --- Controller im Notmodus / Firmware verloren
        if (dv.isDisk && !dv.mediaPresent) add("controller_safe_mode", 0.65, "Datenträger meldet „kein Medium“ (Bitte Datenträger einlegen)");
        if (dv.isDisk && dv.mediaPresent && dv.size == 0) add("controller_safe_mode", 0.6, "Größe 0 Byte");
        if (sc.hasSenseFor(0x3A)) add("controller_safe_mode", 0.6, "Controller antwortet: " + sc.firstSenseText());
        if (dv.isDisk && dv.size > 0 && dv.size <= (64ull << 20) && !dv.imageFile) add("controller_safe_mode", 0.5, "Ungewöhnlich kleine Größe " + humanSize(dv.size) + " – typisch für Werks-/Notmodus des Controllers");
        if (readOk && blankShare >= 0.95 && f.layout.found.empty() && f.layout.volumes.empty()) add("controller_safe_mode", 0.35, fmt("%d von %d Stichproben liefern nur 00/FF – Zuordnungstabelle des Controllers verloren?", f.probe.zeros + f.probe.ones + f.probe.repeating, samples));
        if (sc.inquiryOk && (sc.product.find("Disk 2.0") != std::string::npos || sc.product.find("UDisk") != std::string::npos || sc.product.find("USB DISK") != std::string::npos) && dv.size <= (64ull << 20))
            add("controller_safe_mode", 0.2, "Allgemeiner Produktname „" + sc.product + "“ zusammen mit falscher Größe");

        // --- NAND verschlissen, Controller schaltet auf nur lesen
        const bool hostRo = f.host.registryWriteProtect || f.host.policyDenyWrite;
        if (sc.writeProtectBit == 1 && !dv.readOnlyAttr && !hostRo) add("nand_readonly", 0.75, "Controller meldet selbst Schreibschutz (MODE-SENSE-WP-Bit gesetzt)");
        if (sc.writeProtectBit == 1 && ct && ct->vid == 0x0781) add("nand_readonly", 0.15, "SanDisk-Sticks sperren sich bei Verschleiß dauerhaft");
        if (f.writeTest && !f.writeTest->ok && readOk) add("nand_readonly", 0.6, "Schreibtest: " + f.writeTest->message);
        for (auto &e : f.logs.entries) if (e.category == "writeprotect") { add("nand_readonly", 0.3, "Protokoll: " + e.meaning); break; }

        // --- Schreibschutz durch Windows
        if (dv.readOnlyAttr) add("windows_readonly", 0.85, "Windows-Datenträgerattribut „Schreibgeschützt“ ist gesetzt");
        if (f.host.registryWriteProtect) add("windows_readonly", 0.9, "Registry: StorageDevicePolicies\\WriteProtect = 1 (gilt für alle USB-Datenträger)");
        if (f.host.policyDenyWrite) add("windows_readonly", 0.9, "Gruppenrichtlinie verweigert Schreibzugriff auf Wechseldatenträger");

        // --- Partitionstabelle defekt
        if (f.layout.tableDamaged && !f.layout.found.empty()) add("partition_damaged", 0.75, "Partitionstabelle fehlt/defekt, Dateisystem aber gefunden");
        else if (f.layout.tableDamaged && f.layout.sector0Readable && f.layout.sector0Pattern == Pattern::Data) add("partition_damaged", 0.4, "Partitionstabelle ungültig");
        if (f.layout.sector0Pattern == Pattern::Zeros && !f.layout.found.empty()) add("partition_damaged", 0.3, "Sektor 0 wurde gelöscht");
        if (f.logs.count("partition") >= 2) add("partition_damaged", 0.2, "Protokolle: Partitionstabelle ungültig");

        // --- Dateisystem beschädigt (RAW)
        if (f.layout.fsDamaged) add("fs_damaged", 0.6, "Partition ohne gültiges Dateisystem (RAW)");
        if (f.layout.fsRepairableFromBackup) add("fs_damaged", 0.6, "Bootsektor zerstört, Sicherungskopie intakt");
        for (auto &v : f.layout.volumes) for (auto &i : v.issues) { add("fs_damaged", 0.2, i); break; }
        if (f.logs.count("fs") >= 2) add("fs_damaged", 0.3, "Protokolle melden Dateisystemfehler");

        // --- Defekte Speicherbereiche
        if (f.probe.errors) add("bad_sectors", std::min(0.3 + 0.1 * f.probe.errors, 0.8), fmt("%d Stichproben nicht lesbar", f.probe.errors));
        if (f.scan && f.scan->badBlocks) add("bad_sectors", 0.75, fmt("Oberflächentest: %u defekte Blöcke (%s)", f.scan->badBlocks, humanSize(f.scan->badBytes).c_str()));
        if (f.logs.count("bad") >= 3) add("bad_sectors", 0.45, "Ereignisanzeige: fehlerhafte Blöcke gemeldet");
        if (sc.hasSenseFor(0x11)) add("bad_sectors", 0.5, "Controller: " + sc.firstSenseText());
        if (f.scan && f.scan->slowBlocks > 20) add("bad_sectors", 0.2, fmt("%u sehr langsame Bereiche (Controller korrigiert stark)", f.scan->slowBlocks));

        // --- Gefälschte Kapazität
        if (f.capacity && f.capacity->fake) add("fake_capacity", 0.95, fmt("Kapazitätstest: echt höchstens %s statt %s", humanSize(f.capacity->real).c_str(), humanSize(f.capacity->claimed).c_str()));
        if (f.layout.beyondEnd) add("fake_capacity", 0.25, "Dateisystem/Partition größer als gemeldete Größe");
        if (ct && ct->fakeRisk >= 3) add("fake_capacity", 0.35, std::string("Kennung typisch für Fake-Sticks: ") + ct->note);
        else if (ct && ct->fakeRisk == 2 && dv.size >= (256ull << 30)) add("fake_capacity", 0.3, "Billig-Controller mit sehr großer Kapazitätsangabe");
        if (dv.size >= (1ull << 40) && ct && ct->fakeRisk >= 1) add("fake_capacity", 0.3, "Angeblich über 1 TB bei Stick-Controller");

        // --- Verbindung / Strom / Kontakt
        if (f.logs.count("connection") >= 3) add("connection", 0.45, "Protokolle: USB-Verbindung wurde wiederholt getrennt/zurückgesetzt");
        if (f.probe.timeouts >= 2) add("connection", 0.3, fmt("%d Zeitüberschreitungen beim Lesen", f.probe.timeouts));
        if (dv.problemCode == 43) add("connection", 0.25, "Code 43 tritt auch bei Wackelkontakt oder zu wenig Strom auf");

        // --- Kartenleser statt Stick
        if (ct && std::string(ct->family).find("Kartenleser") != std::string::npos) add("card_reader", 0.6, ct->note);

        // --- Alles in Ordnung
        if (dv.isDisk && dv.mediaPresent && readOk && f.probe.errors == 0 && f.layout.healthy() && sc.writeProtectBit != 1 && !dv.readOnlyAttr && !hostRo)
            add("healthy", 0.8, "Lesen fehlerfrei, Partition und Dateisystem gültig, kein Schreibschutz");

        // --- Erfahrung aus der Wissensbasis
        if (kb_) {
            auto prior = kb_->diagnosisPrior(dv.idString(), d.symptoms);
            for (auto &[hid, rate] : prior) {
                auto it = H.find(hid);
                if (it == H.end() || rate <= 0) continue;
                add(hid, 0.25 * rate, fmt("Wissensbasis: bei ähnlichen Sticks zu %d %% bestätigt", int(rate * 100)));
                if (log) log(LogKind::Lernen, fmt("Erfahrung erhöht „%s“ (bestätigt in %d %% ähnlicher Fälle)", hid.c_str(), int(rate * 100)));
            }
        }

        if (f.capacity && f.capacity->done && !f.capacity->fake) H.erase("fake_capacity");   // Test bestanden
        for (auto &[id, h] : H) { describe(h, f); d.hypotheses.push_back(h); }
        // „Alles in Ordnung“ schließt Fehlerhypothesen weitgehend aus
        if (H.count("healthy")) for (auto &h : d.hypotheses) if (h.id != "healthy" && h.id != "fake_capacity") h.confidence *= 0.5;
        std::sort(d.hypotheses.begin(), d.hypotheses.end(), [](auto &a, auto &b) { return a.confidence > b.confidence; });
        d.hypotheses.erase(std::remove_if(d.hypotheses.begin(), d.hypotheses.end(), [](auto &h) { return h.confidence < 0.1; }), d.hypotheses.end());

        if (d.hypotheses.empty()) {
            Hypothesis h; h.id = "unclear"; h.title = "Noch kein eindeutiger Befund"; h.confidence = 0.3;
            h.explanation = "Die Messwerte reichen noch nicht. Ein vollständiger Oberflächentest und ein Kapazitätstest bringen Klarheit.";
            h.dataChance = "unbekannt"; h.dataPercent = 50; h.repair = "nach weiteren Tests";
            h.actions = {"surface_scan", "image", "capacity_test"};
            d.hypotheses.push_back(h);
        }
        for (auto &h : d.hypotheses)
            if (log) log(LogKind::Hypothese, fmt("%s – %d %%", h.title.c_str(), int(h.confidence * 100 + 0.5)));

        const Hypothesis &t = d.hypotheses.front();
        d.headline = t.title;
        d.dataPercent = t.dataPercent;
        d.repairable = t.repairable;
        buildPlan(f, d, log);
        return d;
    }

private:
    void facts(const DeviceFacts &f, Diagnosis &d) {
        const auto &dv = f.dev;
        d.facts.push_back("Gerät: " + dv.displayName() + " (" + dv.idString() + ")" + (dv.imageFile ? " – Abbilddatei" : ""));
        if (f.controller) d.facts.push_back(std::string("Controller: ") + f.controller->vendor + " · " + f.controller->family);
        if (dv.isDisk) d.facts.push_back("Gemeldete Größe: " + (dv.mediaPresent ? humanSize(dv.size) : std::string("kein Medium")));
        if (f.scsi.capacityOk && dv.size && f.scsi.capacityBytes() != dv.size) d.facts.push_back("Controller meldet abweichend " + humanSize(f.scsi.capacityBytes()));
        if (f.scsi.writeProtectBit >= 0) d.facts.push_back(std::string("Schreibschutzbit des Controllers: ") + (f.scsi.writeProtectBit ? "gesetzt" : "nicht gesetzt"));
        if (!f.scsi.unitReady && !f.scsi.readySense.empty()) d.facts.push_back("Bereitschaft: " + f.scsi.readySense);
        if (f.probe.samples) d.facts.push_back(fmt("Lese-Probe: %d/%d lesbar, Ø %u ms, max. %u ms, %.1f MB/s", f.probe.ok, f.probe.samples, f.probe.avgMs, f.probe.maxMs, f.probe.readMBs));
        d.facts.push_back("Partitionsschema: " + f.layout.scheme);
        for (auto &x : f.layout.facts) d.facts.push_back(x);
        if (!f.logs.entries.empty()) {
            int matched = 0; for (auto &e : f.logs.entries) matched += e.matched;
            d.facts.push_back(fmt("Protokolle: %zu relevante Einträge, %d eindeutig diesem Stick zugeordnet", f.logs.entries.size(), matched));
        }
    }

    void describe(Hypothesis &h, const DeviceFacts &f) {
        const auto *ct = f.controller;
        std::string tool = ct && *ct->tool ? ct->tool : "Herstellerwerkzeug passend zum Controller (z. B. per ChipGenius bestimmen)";
        if (h.id == "not_detected") {
            h.title = "Stick wird nicht erkannt – Controller antwortet nicht";
            h.explanation = "Windows sieht nur ein fehlerhaftes USB-Gerät. Ursache ist meist ein Kontakt-/Lötstellenproblem am Stecker, ein defekter Quarz oder ein toter Controller.";
            h.dataChance = "Per Software kaum; Daten liegen aber meist noch im Speicherchip (Labor: Chip-Off)"; h.dataPercent = 10;
            h.repair = "Andere Buchse/Kabel ohne Verlängerung testen, USB-2.0-Buchse probieren; Gerät neu starten. Hardwaredefekt ist per Software nicht reparierbar.";
            h.actions = {"restart_device", "port_hint"};
        } else if (h.id == "controller_safe_mode") {
            h.title = "Controller im Notmodus – Firmware/Zuordnungstabelle verloren";
            h.explanation = "Der Controller findet seine Verwaltungsdaten im Speicher nicht mehr und meldet deshalb „kein Medium“, 0 MB oder eine Mini-Größe.";
            h.dataChance = "Gering – der Controller kann die Daten nicht mehr zuordnen (nur Speziallabor)"; h.dataPercent = 5;
            h.repair = "Neuflash mit " + tool + ". Danach funktioniert der Stick oft wieder, die Daten sind dabei verloren.";
            h.repairable = true;
            h.actions = {"restart_device", "reflash_guide"};
        } else if (h.id == "nand_readonly") {
            h.title = "Speicher verschlissen – Controller hat auf „nur lesen“ geschaltet";
            h.explanation = "Die Speicherzellen sind am Ende ihrer Lebensdauer. Der Controller schützt die Daten, indem er Schreiben dauerhaft sperrt.";
            h.dataChance = "Sehr gut – jetzt sofort ein Abbild ziehen bzw. Dateien kopieren"; h.dataPercent = 90;
            h.repair = "Nicht dauerhaft reparierbar. Ein Neuflash kann ihn manchmal kurz wiederbeleben, er fällt aber bald erneut aus – Stick ersetzen.";
            h.actions = {"image", "file_rescue", "carve", "reflash_guide"};
        } else if (h.id == "windows_readonly") {
            h.title = "Schreibschutz kommt von Windows, nicht vom Stick";
            h.explanation = "Ein Windows-Attribut oder eine Richtlinie sperrt das Schreiben. Der Stick selbst ist dabei in Ordnung.";
            h.dataChance = "Vollständig"; h.dataPercent = 100;
            h.repair = "Attribut bzw. Richtlinie entfernen – ohne Datenverlust."; h.repairable = true;
            h.actions = {};
            if (f.dev.readOnlyAttr) h.actions.push_back("clear_readonly");
            if (f.host.registryWriteProtect) h.actions.push_back("clear_host_policy");
            if (f.host.policyDenyWrite) h.actions.push_back("policy_hint");
        } else if (h.id == "partition_damaged") {
            h.title = "Partitionstabelle beschädigt – Daten sind noch da";
            h.explanation = "Der Eintrag, der Windows sagt, wo das Dateisystem beginnt, ist weg oder falsch. Das Dateisystem selbst wurde gefunden.";
            h.dataChance = "Sehr gut"; h.dataPercent = 90;
            h.repair = "Tabelle aus dem gefundenen Dateisystem neu schreiben (rückgängig machbar)."; h.repairable = true;
            h.actions = {"image", "file_rescue", "rebuild_mbr"};
        } else if (h.id == "fs_damaged") {
            h.title = "Dateisystem beschädigt (RAW / „muss formatiert werden“)";
            h.explanation = "Bootsektor oder Verwaltungsdaten des Dateisystems sind zerstört – meist nach Abziehen ohne Auswerfen oder Stromausfall.";
            h.dataChance = f.layout.fsRepairableFromBackup ? "Sehr gut – Sicherungskopie des Bootsektors ist intakt" : "Gut – Dateien per Dateiliste oder Signatursuche"; h.dataPercent = f.layout.fsRepairableFromBackup ? 90 : 70;
            h.repair = f.layout.fsRepairableFromBackup ? "Bootsektor aus der Sicherung wiederherstellen (rückgängig machbar)." : "Erst Dateien retten, danach neu formatieren."; h.repairable = true;
            h.actions = {"image", "file_rescue", "carve"};
            if (f.layout.fsRepairableFromBackup) h.actions.push_back("restore_boot");
            else h.actions.push_back("format_exfat");
        } else if (h.id == "bad_sectors") {
            h.title = "Defekte Speicherbereiche";
            h.explanation = "Teile des Speichers lassen sich nicht oder nur sehr langsam lesen. Das ist Verschleiß – es wird mit der Zeit schlimmer.";
            h.dataChance = "Gut für alles außerhalb der defekten Bereiche – Abbild mit Mehrfach-Durchgängen"; h.dataPercent = 75;
            h.repair = "Nur eingeschränkt: Neu formatieren kann der Controller nutzen, um Blöcke auszutauschen. Für wichtige Daten nicht mehr verwenden.";
            h.actions = {"image", "file_rescue", "carve", "surface_scan"};
        } else if (h.id == "fake_capacity") {
            h.title = "Gefälschte Kapazität (Fake-Stick)";
            h.explanation = "Der Controller wurde so programmiert, dass er mehr Speicher meldet, als verbaut ist. Alles oberhalb der echten Größe geht verloren oder überschreibt ältere Dateien.";
            h.dataChance = "Nur Dateien im echten Bereich"; h.dataPercent = 50;
            h.repair = "Auf die echte Größe einrichten – danach zuverlässig nutzbar (mit kleinerer Größe)."; h.repairable = true;
            h.actions = {"image", "file_rescue", "capacity_test", "fix_fake"};
        } else if (h.id == "connection") {
            h.title = "Verbindungs-, Kontakt- oder Stromproblem";
            h.explanation = "Der Stick verliert immer wieder die Verbindung. Das liegt oft an der Buchse, einem Hub/Verlängerungskabel, zu wenig Strom oder einem wackeligen Stecker.";
            h.dataChance = "Gut, sobald die Verbindung stabil ist"; h.dataPercent = 70;
            h.repair = "Direkt am PC (hinten) anschließen, USB-2.0-Buchse testen, ohne Hub. Neu starten des Geräts."; h.repairable = true;
            h.actions = {"restart_device", "port_hint", "image"};
        } else if (h.id == "card_reader") {
            h.title = "Kartenleser – die Speicherkarte ist das eigentliche Ziel";
            h.explanation = "Dieses Gerät ist ein Kartenleser. Fehler „kein Medium“ heißt hier oft nur: keine oder defekte Karte.";
            h.dataChance = "Abhängig von der Karte"; h.dataPercent = 50;
            h.repair = "Karte prüfen/neu einstecken; Diagnose mit eingesteckter Karte wiederholen.";
            h.actions = {"port_hint"};
        } else if (h.id == "healthy") {
            h.title = "Kein Fehler gefunden";
            h.explanation = "Lesen, Partition und Dateisystem sind in Ordnung. Wenn trotzdem Probleme auftreten: Oberflächentest und Kapazitätstest durchführen.";
            h.dataChance = "Vollständig"; h.dataPercent = 100; h.repair = "Nicht nötig"; h.repairable = true;
            h.actions = {"surface_scan", "capacity_test"};
        }
    }

    PlannedAction action(const std::string &id, const DeviceFacts &f) {
        PlannedAction a; a.id = id;
        if (id == "image") { a.title = "Rettungsabbild erstellen"; a.risk = Risk::Lesend; a.needsTarget = true; a.why = "Sichert alles Lesbare, bevor irgendetwas verändert wird"; }
        else if (id == "file_rescue") { a.title = "Dateien retten (Dateiliste, auch gelöschte)"; a.risk = Risk::Lesend; a.needsTarget = true; a.why = "Kopiert Dateien mit Namen und Ordnern"; }
        else if (id == "carve") { a.title = "Signatursuche (Fotos, Videos, Dokumente)"; a.risk = Risk::Lesend; a.needsTarget = true; a.why = "Findet Dateien auch ohne Dateisystem"; }
        else if (id == "surface_scan") { a.title = "Oberflächentest"; a.risk = Risk::Lesend; a.why = "Liest den ganzen Stick und kartiert defekte/langsame Bereiche"; }
        else if (id == "capacity_test") { a.title = "Kapazitätstest (Fake-Erkennung)"; a.risk = Risk::Mittel; a.needsImage = true; a.why = "Schreibt Prüfmuster und stellt die Originale wieder her"; }
        else if (id == "clear_readonly") { a.title = "Windows-Schreibschutzattribut entfernen"; a.risk = Risk::Harmlos; a.why = "Ändert nur ein Windows-Attribut"; }
        else if (id == "clear_host_policy") { a.title = "Registry-Schreibschutz (WriteProtect) zurücksetzen"; a.risk = Risk::Harmlos; a.why = "Betrifft alle USB-Datenträger dieses PCs"; }
        else if (id == "policy_hint") { a.title = "Gruppenrichtlinie prüfen lassen"; a.manual = true; a.why = "Richtlinie wird zentral verwaltet (z. B. Firmen-PC) – Administrator fragen"; }
        else if (id == "restart_device") { a.title = "USB-Gerät neu starten"; a.risk = Risk::Harmlos; a.why = "Wie Abziehen und Einstecken – setzt einen hängenden Controller zurück"; }
        else if (id == "port_hint") { a.title = "Andere Buchse/Kabel testen"; a.manual = true; a.why = "Direkt am PC, ohne Hub/Verlängerung, auch USB 2.0 probieren"; }
        else if (id == "restore_boot") { a.title = "Bootsektor aus Sicherung wiederherstellen"; a.risk = Risk::Mittel; a.needsImage = false; a.why = "Schreibt 1–12 Sektoren, Original wird gesichert"; }
        else if (id == "rebuild_mbr") { a.title = "Partitionstabelle neu schreiben"; a.risk = Risk::Mittel; a.why = "Schreibt Sektor 0, Original wird gesichert"; }
        else if (id == "fix_fake") { a.title = "Auf echte Kapazität einrichten"; a.risk = Risk::Loeschend; a.needsImage = true; a.why = "Neue Partition in echter Größe – löscht den Stick"; }
        else if (id == "format_fat32") { a.title = "Neu formatieren (FAT32)"; a.risk = Risk::Loeschend; a.needsImage = true; a.why = "Löscht den Stick"; }
        else if (id == "format_exfat") { a.title = "Neu formatieren (exFAT)"; a.risk = Risk::Loeschend; a.needsImage = true; a.why = "Löscht den Stick"; }
        else if (id == "reflash_guide") { a.title = "Controller-Neuflash (Anleitung)"; a.manual = true; a.risk = Risk::Loeschend;
            a.why = std::string("Mit ") + (f.controller && *f.controller->tool ? f.controller->tool : "dem passenden Herstellerwerkzeug") + " – löscht alle Daten, nur wenn Daten unwichtig/gesichert"; }
        return a;
    }

    void buildPlan(const DeviceFacts &f, Diagnosis &d, LogFn log) {
        std::vector<std::string> ids;
        auto push = [&](const std::string &id) { if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id); };
        const double topConf = d.hypotheses.front().confidence;
        for (auto &h : d.hypotheses) if (h.confidence >= std::max(0.25, topConf * 0.5)) for (auto &a : h.actions) push(a);
        // Daten sichern kommt immer vor schreibenden Schritten, wenn der Stick lesbar ist
        bool writes = false; for (auto &i : ids) { auto a = action(i, f); if (a.risk >= Risk::Mittel && !a.manual) writes = true; }
        if (writes && f.probe.ok > 0 && !f.imageAvailable) { ids.insert(ids.begin(), "image"); }
        for (auto &i : ids) {
            if (i == "image" && f.imageAvailable) continue;
            if (i == "image" && (f.probe.ok == 0 || f.dev.imageFile)) continue;
            if ((i == "file_rescue" || i == "carve") && f.probe.ok == 0 && !f.dev.imageFile) continue;
            PlannedAction a = action(i, f);
            if (kb_) {
                auto e = kb_->actionExperience(f.dev.idString(), d.symptoms, i);
                a.learnedTries = e.tries; a.learnedRate = e.tries ? e.rate() : -1;
            }
            d.plan.push_back(a);
        }
        // Gleiche Risikostufe: was bei ähnlichen Sticks geholfen hat, zuerst (Retten bleibt vorne)
        std::stable_sort(d.plan.begin(), d.plan.end(), [](const PlannedAction &a, const PlannedAction &b) {
            auto rank = [](const PlannedAction &x) { return x.id == "image" ? -2 : (x.id == "file_rescue" || x.id == "carve") ? -1 : int(x.risk); };
            if (rank(a) != rank(b)) return rank(a) < rank(b);
            return a.learnedRate > b.learnedRate;
        });
        if (log) for (auto &a : d.plan)
            log(LogKind::Entscheidung, "Plan: " + a.title + " (" + (a.manual ? std::string("Anleitung") : riskName(a.risk)) + ")" +
                (a.learnedTries ? fmt(" – Erfahrung: %d/%d erfolgreich", int(a.learnedRate * a.learnedTries + 0.5), a.learnedTries) : std::string()));
    }

    const KnowledgeStore *kb_;
};

} // namespace usbr
