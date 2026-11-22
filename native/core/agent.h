#pragma once
// Der KI-Agent: sammelt Messwerte, bewertet, plant, führt Reparaturversuche aus (mit Freigabe),
// prüft nach jedem Schritt selbst das Ergebnis und lernt daraus.
#include "diagnosis_engine.h"
#include "rescue_imager.h"
#include "fs_reader.h"
#include "file_carver.h"
#include "repair_actions.h"
#include "capacity_test.h"
#include <ctime>
#include <future>

namespace usbr {

struct AgentCallbacks {
    LogFn log;
    ProgressFn progress;
    // Freigabe für einen Schritt (Titel, Risiko, Erklärung). false = abgelehnt.
    std::function<bool(const PlannedAction &, const std::string &details)> approve;
    std::function<void(const Diagnosis &, const DeviceFacts &)> diagnosisChanged;
};

struct RunOptions {
    std::string targetDir;          // Zielordner für gerettete Dateien
    std::string formatFs = "exfat"; // exfat, fat32, ntfs
    std::string label = "USB";
    bool stickIsEmpty = false;      // Benutzer bestätigt: keine wichtigen Daten
    bool includeDeleted = true;
};

struct StepOutcome { std::string action; bool executed = false, ok = false, improved = false, manual = false; std::string message; };

class RecoveryAgent {
public:
    RecoveryAgent(KnowledgeStore &kb, std::string workDir) : kb_(kb), work_(std::move(workDir)) { model_.train(&kb_); }

    const SymptomModel &model() const { return model_; }
    std::string modelSummary() const {
        return fmt("Trainiertes Symptom-Modell: %zu Fehlerbilder, %zu Trainingsbeispiele (%zu Grundfälle mit Variationen, %zu von dir bestätigte Fälle), Treffergenauigkeit %d %% (Kreuzvalidierung)",
                   model_.classes(), model_.trainingSize(), model_.builtinCases(), model_.confirmedCases(), int(model_.crossValidate() * 100 + 0.5));
    }

    const DeviceFacts &facts() const { return facts_; }
    const Diagnosis &diagnosis() const { return diag_; }
    const std::string &caseDir() const { return caseDir_; }

    // --- Messen (nur lesend)
    DeviceFacts collect(const UsbDevice &dev, bool withLogs, const AgentCallbacks &cb, CancelToken *cancel) {
        DeviceFacts f; f.dev = dev;
        f.imageAvailable = facts_.imageAvailable && sameDevice(facts_.dev, dev);
        f.imagePath = f.imageAvailable ? facts_.imagePath : std::string();
        if (sameDevice(facts_.dev, dev)) { f.scan = facts_.scan; f.capacity = facts_.capacity; f.writeTest = facts_.writeTest; if (!withLogs) f.logs = facts_.logs; }
        auto L = [&](LogKind k, const std::string &s) { if (cb.log) cb.log(k, s); };
        const uint64_t t0 = nowMs();
        // Protokolle parallel auslesen – das ist der langsamste Teil und braucht den Stick nicht
        std::future<LogReport> logsFuture;
        if (withLogs) logsFuture = std::async(std::launch::async, [dev] { return collectLogs(dev); });
        f.host = readHostPolicy();
        if (dev.hasVidPid) f.controller = findController(dev.vid, dev.pid);
        L(LogKind::Messung, "Gerät: " + dev.displayName() + " · " + dev.idString() + (dev.isDisk ? " · " + (dev.mediaPresent ? humanSize(dev.size) : std::string("kein Medium")) : " · kein Datenträger"));
        if (f.controller) L(LogKind::Messung, std::string("Controller laut Kennung: ") + f.controller->vendor + " · " + f.controller->family);
        if (f.host.registryWriteProtect) L(LogKind::Messung, "Registry-Schreibschutz für USB-Datenträger ist aktiv");
        if (dev.problemCode) L(LogKind::Messung, problemCodeText(dev.problemCode));
        if (dev.isDisk && !dev.path.empty() && !dev.imageFile) {
            if (cb.progress) cb.progress(10, "SCSI-Abfragen an den Controller");
            f.scsi = probeScsi(dev.path);
            if (f.scsi.inquiryOk) L(LogKind::Messung, fmt("INQUIRY: „%s %s“ Rev. %s", f.scsi.vendor.c_str(), f.scsi.product.c_str(), f.scsi.revision.c_str()));
            if (f.scsi.capacityOk) L(LogKind::Messung, "READ CAPACITY: " + humanSize(f.scsi.capacityBytes()) + fmt(" (Blockgröße %u)", f.scsi.blockLen));
            else if (!f.scsi.capacitySense.empty()) L(LogKind::Messung, "READ CAPACITY: " + f.scsi.capacitySense);
            if (f.scsi.writeProtectBit >= 0) L(LogKind::Messung, std::string("MODE SENSE: Schreibschutzbit ") + (f.scsi.writeProtectBit ? "GESETZT" : "nicht gesetzt"));
            if (!f.scsi.unitReady) L(LogKind::Messung, "TEST UNIT READY: " + (f.scsi.readySense.empty() ? std::string("keine Antwort") : f.scsi.readySense));
        }
        {   // Sofort-Einschätzung des trainierten Modells aus Kennung, Status und Controller-Antworten
            const std::string early = symptomKey(f);
            auto pr = model_.predict(early);
            if (early.empty()) L(LogKind::Hypothese, "Sofort-Einschätzung: Kennung und Controller-Antworten unauffällig – Messungen entscheiden");
            else if (!pr.empty()) L(LogKind::Hypothese, fmt("Sofort-Einschätzung nach %llu ms: %s (%d %%) – wird jetzt mit Messungen geprüft",
                                                      (unsigned long long)(nowMs() - t0), hypothesisTitleShort(pr[0].label), int(pr[0].p * 100 + 0.5)));
        }
        if (dev.isDisk && dev.mediaPresent && !dev.path.empty()) {
            RawDisk d; std::string e;
            if (d.open(dev.path, false, e)) {
                if (cb.progress) cb.progress(30, "Lese-Probe über den ganzen Stick");
                f.probe = quickProbe(d, 24, cancel);
                if (f.probe.errors || f.probe.timeouts) {
                    L(LogKind::Entscheidung, "Lesefehler in der Schnellprobe – prüfe genauer (64 Stichproben)");
                    f.probe = quickProbe(d, 64, cancel);
                }
                L(LogKind::Messung, fmt("Lese-Probe: %d/%d lesbar, %d Fehler, %d Zeitüberschreitungen, Ø %u ms", f.probe.ok, f.probe.samples, f.probe.errors, f.probe.timeouts, f.probe.avgMs));
                if (f.probe.ok && f.probe.zeros + f.probe.ones == f.probe.ok) L(LogKind::Messung, "Alle lesbaren Stichproben sind leer (00/FF)");
                if (cb.progress) cb.progress(50, "Partition und Dateisystem prüfen");
                BlockReader r(d, 3000);
                f.layout = analyzeLayout(r, [&](double p, const std::string &t) { if (cb.progress) cb.progress(50 + p * 0.3, t); }, cancel, false);
                for (auto &x : f.layout.facts) L(LogKind::Messung, x);
                for (auto &x : f.layout.issues) L(LogKind::Messung, "Auffällig: " + x);
            } else { f.probe.openError = e; L(LogKind::Fehler, "Öffnen nicht möglich: " + e); }
        }
        if (withLogs) {
            if (cb.progress) cb.progress(85, "Protokolle auslesen");
            f.logs = logsFuture.get();
            int matched = 0; for (auto &e : f.logs.entries) matched += e.matched;
            L(LogKind::Messung, fmt("Protokolle: %zu relevante Einträge (%d eindeutig diesem Stick zugeordnet)", f.logs.entries.size(), matched));
            int shown = 0;
            for (auto &e : f.logs.entries) if (e.matched && shown++ < 8) L(LogKind::Messung, "  " + e.time + " " + e.source + (e.id ? fmt(" %d", e.id) : std::string()) + ": " + e.meaning);
            for (auto &n : f.logs.notes) L(LogKind::Info, n);
        }
        if (cb.progress) cb.progress(100, "Messung abgeschlossen");
        L(LogKind::Info, fmt("Messungen abgeschlossen in %.1f s", double(nowMs() - t0) / 1000.0));
        return f;
    }

    Diagnosis diagnose(const UsbDevice &dev, const AgentCallbacks &cb, CancelToken *cancel, bool withLogs = true) {
        if (!sameDevice(facts_.dev, dev)) { facts_ = DeviceFacts{}; caseDir_.clear(); }
        facts_ = collect(dev, withLogs, cb, cancel);
        DiagnosisEngine eng(&kb_, &model_);
        diag_ = eng.evaluate(facts_, cb.log);
        if (cb.log && diag_.top()) cb.log(LogKind::Entscheidung, fmt("Befund: %s (Sicherheit %d %%)", diag_.top()->title.c_str(), int(diag_.top()->confidence * 100 + 0.5)));
        if (cb.diagnosisChanged) cb.diagnosisChanged(diag_, facts_);
        return diag_;
    }

    void confirm(const std::string &hypId, bool correct, const AgentCallbacks &cb) {
        kb_.recordDiagnosis(facts_.dev.idString(), family(), diag_.symptoms, hypId, correct);
        model_.train(&kb_);   // sofort nachtrainieren
        if (cb.log) cb.log(LogKind::Lernen, std::string("Diagnose „") + hypId + (correct ? "“ bestätigt – wird bei ähnlichen Sticks stärker gewichtet" : "“ verworfen – wird bei ähnlichen Sticks schwächer gewichtet"));
    }

    // --- Einen Schritt ausführen (Freigabe muss vorher eingeholt sein)
    StepOutcome execute(const std::string &id, const RunOptions &opt, const AgentCallbacks &cb, CancelToken *cancel) {
        StepOutcome o; o.action = id;
        auto L = [&](LogKind k, const std::string &s) { if (cb.log) cb.log(k, s); };
        const UsbDevice &dev = facts_.dev;
        ensureCaseDir();
        const std::string symptomsBefore = diag_.symptoms;
        const std::string topBefore = diag_.top() ? diag_.top()->id : "";
        const double confBefore = diag_.top() ? diag_.top()->confidence : 0;
        curTitle_ = titleOf(id);
        L(LogKind::Aktion, "Starte: " + curTitle_);

        if (id == "image") {
            RawDisk d; std::string e;
            if (!d.open(dev.path, false, e)) { o.message = e; return finish(o, symptomsBefore, false, cb); }
            std::string dir = opt.targetDir.empty() ? caseDir_ : opt.targetDir;
            std::string img = dir + sep() + "abbild.img", map = dir + sep() + "abbild.map";
            RescueImager im;
            auto st = im.run(d, img, map, {}, cb.progress, cancel, cb.log);
            o.executed = true; o.ok = st.rescued > 0 && st.error.empty();
            o.message = st.error.empty() ? fmt("%s gesichert, %s defekt%s", humanSize(st.rescued).c_str(), humanSize(st.bad).c_str(), st.cancelled ? " (abgebrochen, fortsetzbar)" : "") : st.error;
            if (o.ok) { facts_.imageAvailable = st.finished; facts_.imagePath = img; }
            return finish(o, symptomsBefore, o.ok, cb);
        }
        if (id == "file_rescue" || id == "carve") {
            if (opt.targetDir.empty()) { o.message = "Bitte Zielordner wählen (nicht auf dem defekten Stick!)"; return o; }
            RawDisk d; std::string e;
            const std::string src = facts_.imageAvailable ? facts_.imagePath : dev.path;
            if (!d.open(src, false, e)) { o.message = e; return o; }
            if (facts_.imageAvailable) L(LogKind::Entscheidung, "Arbeite mit dem Abbild statt mit dem Stick (schont den Stick)");
            BlockReader r(d, 3000);
            o.executed = true;
            if (id == "file_rescue") {
                std::vector<VolumeInfo> vols;
                for (auto &v : facts_.layout.volumes) if (v.primaryValid) vols.push_back(v);
                for (auto &v : facts_.layout.found) if (v.primaryValid || v.backupValid) vols.push_back(v);
                if (vols.empty()) { o.message = "Kein lesbares FAT/exFAT-Dateisystem – Signatursuche verwenden"; return finish(o, symptomsBefore, false, cb); }
                size_t files = 0; uint64_t bytes = 0; int n = 0; std::vector<uint64_t> done;
                for (auto &v : vols) {
                    if (std::find(done.begin(), done.end(), v.offset) != done.end()) continue;
                    done.push_back(v.offset);
                    if (v.type == FsType::NTFS) { L(LogKind::Info, "NTFS-Volume: Dateien über Signatursuche retten"); continue; }
                    std::string dir = opt.targetDir + sep() + fmt("Volume_%d_%s", ++n, fsName(v.type));
                    if (!v.primaryValid) L(LogKind::Entscheidung, "Nutze Parameter aus dem Sicherungs-Bootsektor – Original ist zerstört");
                    auto res = rescueFiles(r, v, dir, opt.includeDeleted, cb.progress, cancel, cb.log);
                    files += res.files; bytes += res.bytes;
                    L(LogKind::Pruefung, fmt("%s: %zu Dateien (%zu gelöscht wiederhergestellt, %zu beschädigt), %s", fsName(v.type), res.files, res.deleted, res.damaged, humanSize(res.bytes).c_str()));
                    for (size_t i = 0; i < res.problems.size() && i < 5; ++i) L(LogKind::Warnung, res.problems[i]);
                }
                o.ok = files > 0; o.message = fmt("%zu Dateien gerettet (%s) nach %s", files, humanSize(bytes).c_str(), opt.targetDir.c_str());
            } else {
                FileCarver c(r);
                auto st = c.run(opt.targetDir + sep() + "Signatursuche", cb.progress, cancel, cb.log);
                std::string types; for (auto &[k, v] : st.perType) types += fmt("%s %d, ", k.c_str(), v);
                o.ok = st.files > 0; o.message = fmt("%d Dateien gefunden (%s) – %s", st.files, humanSize(st.bytes).c_str(), types.empty() ? "keine" : types.substr(0, types.size() - 2).c_str());
            }
            return finish(o, symptomsBefore, o.ok, cb);
        }
        if (id == "surface_scan") {
            RawDisk d; std::string e;
            if (!d.open(dev.path, false, e)) { o.message = e; return o; }
            auto s = fullScan(d, cb.progress, cancel, cb.log);
            facts_.scan = s; o.executed = true; o.ok = !s.cancelled;
            o.message = fmt("%s geprüft: %u defekte Blöcke, %u langsame Bereiche, Ø %.1f MB/s", humanSize(s.scanned).c_str(), s.badBlocks, s.slowBlocks, s.avgMBs);
            rediagnose(cb);
            return finish(o, symptomsBefore, o.ok, cb);
        }
        if (id == "port_hint" || id == "policy_hint" || id == "reflash_guide") {
            o.manual = true; o.message = guidance(id);
            L(LogKind::Info, o.message);
            return o;
        }

        // --- Ab hier wird geschrieben oder das Gerät verändert
        if (id == "clear_readonly") { auto a = clearReadOnly(dev.path); o.executed = true; o.ok = a.ok; o.message = a.message; }
        else if (id == "clear_host_policy") { std::string e; o.executed = true; o.ok = clearHostWriteProtect(e); o.message = o.ok ? "Registry-Schreibschutz zurückgesetzt – Stick einmal neu einstecken" : e; }
        else if (id == "restart_device") {
            std::string e; o.executed = true;
            o.ok = !dev.usbInstanceId.empty() && restartUsbDevice(dev.usbInstanceId, e);
            o.message = o.ok ? "USB-Gerät neu gestartet" : (dev.usbInstanceId.empty() ? "Keine USB-Geräte-ID bekannt" : e);
        } else {
            RawDisk d; std::string e;
            if (!d.open(dev.path, true, e)) { o.message = "Schreibend öffnen nicht möglich: " + e; return finish(o, symptomsBefore, false, cb); }
            if (!d.lockVolumes(e)) L(LogKind::Warnung, e);
            UndoJournal u;
            std::string undoPath = caseDir_ + sep() + "rueckgaengig_" + id + "_" + stamp() + ".bin";
            if (!u.open(undoPath, e)) { o.message = e; return o; }
            o.executed = true;
            if (id == "restore_boot") {
                const VolumeInfo *v = nullptr;
                for (auto &x : facts_.layout.found) if (!x.primaryValid && x.backupValid) { v = &x; break; }
                if (!v) for (auto &x : facts_.layout.volumes) if (!x.primaryValid) for (auto &y : facts_.layout.found) if (y.offset == x.offset && y.backupValid) v = &y;
                if (!v) o.message = "Kein Volume mit intakter Sicherung gefunden";
                else { auto a = restoreBootSector(d, u, *v); o.ok = a.ok; o.message = a.message; }
            } else if (id == "rebuild_mbr") {
                const VolumeInfo *v = nullptr;
                for (auto &x : facts_.layout.found) if (x.primaryValid && x.offset > 0) { v = &x; break; }
                if (!v) for (auto &x : facts_.layout.found) if (x.backupValid && x.offset > 0) { v = &x; break; }
                if (!v) o.message = "Kein Dateisystem gefunden, für das eine Tabelle geschrieben werden könnte";
                else { auto a = rebuildMbr(d, u, *v); o.ok = a.ok; o.message = a.message; }
            } else if (id == "capacity_test") {
                if (!facts_.imageAvailable && !opt.stickIsEmpty) { o.executed = false; o.message = "Gesperrt: erst Rettungsabbild erstellen oder bestätigen, dass der Stick leer ist"; }
                else { CapacityTester t; auto c = t.run(d, u, cb.progress, cancel, cb.log); facts_.capacity = c; o.ok = c.done; o.message = c.done ? (c.fake ? "Fake-Stick: echte Größe ca. " + humanSize(c.real) + " statt " + humanSize(c.claimed) : "Kapazität echt: " + humanSize(c.claimed)) : c.error; for (auto &n : c.notes) L(LogKind::Info, n); }
            } else if (id == "fix_fake" || id == "format_fat32" || id == "format_exfat") {
                if (!facts_.imageAvailable && !opt.stickIsEmpty) { o.executed = false; o.message = "Gesperrt: erst Rettungsabbild erstellen oder bestätigen, dass der Stick leer ist"; }
                else {
                    uint64_t limit = 0;
                    if (id == "fix_fake") {
                        if (!facts_.capacity || !facts_.capacity->fake) { o.executed = false; o.message = "Erst Kapazitätstest durchführen"; }
                        else limit = facts_.capacity->real / 100 * 98 / (1 << 20) * (1 << 20);
                    }
                    std::string fs = id == "format_fat32" ? "fat32" : id == "format_exfat" ? "exfat" : opt.formatFs;
                    if (o.executed) {
                        if (fs == "fat32") { auto a = formatFat32(d, u, limit ? limit : d.size(), opt.label, cb.progress, cancel); o.ok = a.ok; o.message = a.message; }
                        else { d.close(); auto a = formatWithWindows(dev.diskNumber, fs, limit, opt.label, caseDir_); o.ok = a.ok; o.message = a.message; for (auto &x : a.details) writeTextFile(caseDir_ + sep() + "diskpart_" + stamp() + ".txt", x); }
                    }
                }
            } else if (id == "write_test") {
                auto a = writeAcceptTest(d, u, 4096 * 64); facts_.writeTest = a; o.ok = true; o.message = a.message;
            } else { o.executed = false; o.message = "Unbekannte Aktion " + id; }
            u.close();
            if (o.executed) L(LogKind::Info, "Rückgängig-Datei: " + undoPath);
        }
        if (!o.executed) { L(LogKind::Warnung, o.message); return o; }
        L(o.ok ? LogKind::Aktion : LogKind::Fehler, o.message);

        // --- Selbst prüfen: neu erkennen und neu bewerten
        L(LogKind::Pruefung, "Prüfe Ergebnis: Gerät neu erkennen und neu bewerten");
        UsbDevice now = refind(dev);
        facts_.dev = now;
        DeviceFacts f = collect(now, false, cb, cancel);
        f.imageAvailable = facts_.imageAvailable; f.imagePath = facts_.imagePath;
        f.capacity = facts_.capacity; f.scan = facts_.scan; f.writeTest = facts_.writeTest;
        facts_ = f;
        DiagnosisEngine eng(&kb_, &model_);
        diag_ = eng.evaluate(facts_, cb.log);
        if (cb.diagnosisChanged) cb.diagnosisChanged(diag_, facts_);
        const Hypothesis *t = diag_.top();
        bool improved = o.ok && t && (t->id == "healthy" || t->id != topBefore || t->confidence < confBefore - 0.15);
        if (id == "capacity_test" || id == "write_test") improved = o.ok;
        o.improved = improved;
        L(LogKind::Pruefung, improved ? "Ergebnis: Zustand hat sich verbessert – neuer Befund: " + (t ? t->title : std::string("–"))
                                      : "Ergebnis: keine Verbesserung – Befund bleibt: " + (t ? t->title : std::string("–")));
        return finish(o, symptomsBefore, improved, cb);
    }

    // --- Automatischer Ablauf: von harmlos nach riskant, nach jedem Schritt prüfen, bei Erfolg aufhören
    std::vector<StepOutcome> autoRun(const RunOptions &opt, const AgentCallbacks &cb, CancelToken *cancel) {
        std::vector<StepOutcome> out;
        std::vector<std::string> tried;
        auto L = [&](LogKind k, const std::string &s) { if (cb.log) cb.log(k, s); };
        L(LogKind::Entscheidung, "Automatischer Ablauf gestartet: zuerst sichern, dann von harmlos nach riskant");
        for (int round = 0; round < 15; ++round) {
            if (cancel && cancel->cancelled()) break;
            if (diag_.top() && diag_.top()->id == "healthy" && round > 0) { L(LogKind::Entscheidung, "Stick ist wieder in Ordnung – Ablauf beendet"); break; }
            const PlannedAction *next = nullptr;
            for (auto &a : diag_.plan) {
                if (std::find(tried.begin(), tried.end(), a.id) != tried.end()) continue;
                next = &a; break;
            }
            if (!next) { L(LogKind::Entscheidung, "Keine weiteren sinnvollen Schritte"); break; }
            PlannedAction a = *next;
            tried.push_back(a.id);
            if (a.manual) { StepOutcome o; o.action = a.id; o.manual = true; o.message = guidance(a.id); L(LogKind::Info, "Hinweis: " + o.message); out.push_back(o); continue; }
            if (a.needsTarget && opt.targetDir.empty() && a.id != "image") { L(LogKind::Entscheidung, "Übersprungen (kein Zielordner gewählt): " + a.title); continue; }
            if (a.needsImage && !facts_.imageAvailable && !opt.stickIsEmpty) { L(LogKind::Entscheidung, "Übersprungen (erst Abbild nötig): " + a.title); continue; }
            if (a.risk >= Risk::Harmlos && cb.approve) {
                std::string details = a.why + "\nRisiko: " + riskName(a.risk);
                if (a.learnedTries) details += fmt("\nErfahrung bei ähnlichen Sticks: %d von %d erfolgreich", int(a.learnedRate * a.learnedTries + 0.5), a.learnedTries);
                if (!cb.approve(a, details)) { L(LogKind::Entscheidung, "Vom Benutzer abgelehnt: " + a.title); continue; }
                L(LogKind::Entscheidung, "Freigegeben: " + a.title);
            }
            out.push_back(execute(a.id, opt, cb, cancel));
        }
        return out;
    }

    void setImage(const std::string &path) { facts_.imageAvailable = true; facts_.imagePath = path; }

private:
    static const char *sep() {
#ifdef _WIN32
        return "\\";
#else
        return "/";
#endif
    }
    static std::string stamp() { std::time_t t = std::time(nullptr); char b[32]; std::strftime(b, sizeof b, "%Y%m%d_%H%M%S", std::localtime(&t)); return b; }
    static bool sameDevice(const UsbDevice &a, const UsbDevice &b) {
        if (a.path.empty() && a.usbInstanceId.empty()) return false;
        if (!a.usbInstanceId.empty() && a.usbInstanceId == b.usbInstanceId) return true;
        return !a.path.empty() && a.path == b.path && a.serial == b.serial;
    }
    std::string family() const { return facts_.controller ? facts_.controller->family : "unbekannt"; }
    void ensureCaseDir() {
        if (!caseDir_.empty()) return;
        std::string name = facts_.dev.hasVidPid ? hex4(facts_.dev.vid) + "-" + hex4(facts_.dev.pid) : std::string("geraet");
        caseDir_ = work_ + sep() + "Fallakten" + sep() + name + "_" + stamp();
        std::error_code ec; std::filesystem::create_directories(u8path(caseDir_), ec);
    }
    void rediagnose(const AgentCallbacks &cb) {
        DiagnosisEngine eng(&kb_, &model_); diag_ = eng.evaluate(facts_, cb.log);
        if (cb.diagnosisChanged) cb.diagnosisChanged(diag_, facts_);
    }
    UsbDevice refind(const UsbDevice &old) {
        if (old.imageFile) return old;
        for (int attempt = 0; attempt < 5; ++attempt) {
            for (auto &d : enumerateUsbDevices())
                if ((!old.usbInstanceId.empty() && d.usbInstanceId == old.usbInstanceId) || (!old.serial.empty() && d.serial == old.serial && d.vid == old.vid)) return d;
#ifdef _WIN32
            Sleep(1000);
#endif
        }
        return old;
    }
    StepOutcome finish(StepOutcome o, const std::string &symptoms, bool success, const AgentCallbacks &cb) {
        kb_.recordAction(facts_.dev.idString(), family(), symptoms, o.action, success);
        if (cb.log) cb.log(LogKind::Lernen, fmt("Gespeichert: „%s“ bei Symptomen [%s] → %s", (curTitle_.empty() ? o.action : curTitle_).c_str(), symptoms.c_str(), success ? "erfolgreich" : "ohne Erfolg"));
        return o;
    }
    std::string titleOf(const std::string &id) const { for (auto &a : diag_.plan) if (a.id == id) return a.title; return id; }
    std::string guidance(const std::string &id) const {
        if (id == "port_hint") return "Stick direkt hinten am PC anschließen (ohne Hub/Verlängerung), eine USB-2.0-Buchse probieren und bei Code 43 einen anderen PC testen. Danach „Neu erkennen“.";
        if (id == "policy_hint") return "Schreibzugriff auf Wechseldatenträger ist per Gruppenrichtlinie verboten. Auf Firmen-PCs den Administrator fragen; privat: gpedit.msc → Computerkonfiguration → Administrative Vorlagen → System → Wechselmedienzugriff.";
        if (id == "reflash_guide") {
            const auto *c = facts_.controller;
            std::string s = "Controller-Neuflash: ";
            s += c ? std::string(c->vendor) + " " + c->family : "Controller unbekannt";
            s += ". 1) Mit ChipGenius oder Flash Drive Information Extractor Controller-Modell, Firmware und NAND-Typ genau bestimmen. 2) Passendes Werkzeug: ";
            s += c && *c->tool ? c->tool : "zum Controller passendes MPTool";
            s += ". 3) Werkzeug nur aus vertrauenswürdiger Quelle, vorher Virenscan. 4) Alle anderen USB-Datenträger abziehen. 5) Neuflash löscht alle Daten – vorher retten, was lesbar ist.";
            return s;
        }
        return {};
    }

    KnowledgeStore &kb_;
    std::string work_, caseDir_, curTitle_;
    DeviceFacts facts_;
    Diagnosis diag_;
    SymptomModel model_;
};

} // namespace usbr
