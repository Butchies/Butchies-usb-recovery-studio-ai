#pragma once
// Reparaturaktionen. Jede schreibende Aktion sichert vorher die betroffenen Sektoren in eine Rückgängig-Datei.
// Freigabe durch den Benutzer erfolgt im Agenten bzw. in der Oberfläche – hier wird nur ausgeführt.
#include "partition_fs.h"
#include "usb_inventory.h"
#include <random>

namespace usbr {

// Rückgängig-Datei: Folge von {Magie "BURU", Versatz u64, Länge u32, Daten}
class UndoJournal {
public:
    bool open(const std::string &path, std::string &err) { path_ = path; return f_.open(path, true, err) && seekEnd(); }
    const std::string &path() const { return path_; }
    bool save(RawDisk &d, uint64_t off, uint32_t len, std::string &err) {
        AlignedBuffer b(len);
        IoResult r = d.read(off, b.data(), len, 5000);
        if (!r.ok) { err = "Originalsektoren nicht lesbar: " + r.message; return false; }
        uint8_t h[16] = {'B', 'U', 'R', 'U'}; wr64(h + 4, off); wr32(h + 12, len);
        if (!f_.writeAt(end_, h, 16) || !f_.writeAt(end_ + 16, b.data(), len)) { err = "Rückgängig-Datei nicht schreibbar"; return false; }
        end_ += 16 + len;
        return true;
    }
    void close() { f_.close(); }

    // Alle Einträge in umgekehrter Reihenfolge zurückschreiben
    static bool restore(RawDisk &d, const std::string &path, std::string &err) {
        std::string data;
        if (!readTextFile(path, data)) { err = "Rückgängig-Datei nicht lesbar"; return false; }
        struct E { uint64_t off; uint32_t len; size_t pos; };
        std::vector<E> es;
        for (size_t p = 0; p + 16 <= data.size();) {
            auto *h = reinterpret_cast<const uint8_t *>(data.data() + p);
            if (std::memcmp(h, "BURU", 4) != 0) break;
            E e{rd64(h + 4), rd32(h + 12), p + 16};
            if (e.pos + e.len > data.size()) break;
            es.push_back(e); p = e.pos + e.len;
        }
        for (auto it = es.rbegin(); it != es.rend(); ++it) {
            AlignedBuffer b(it->len); std::memcpy(b.data(), data.data() + it->pos, it->len);
            IoResult r = d.write(it->off, b.data(), it->len);
            if (!r.ok) { err = r.message; return false; }
        }
        d.flush(); d.refreshPartitions();
        return true;
    }
private:
    bool seekEnd() { std::string s; if (readTextFile(path_, s)) end_ = s.size(); return true; }
    OutFile f_;
    std::string path_;
    uint64_t end_ = 0;
};

struct ActionResult { bool ok = false; std::string message; std::vector<std::string> details; };

inline bool writeSectors(RawDisk &d, UndoJournal &u, uint64_t off, const std::vector<uint8_t> &data, std::string &err) {
    const uint32_t ss = d.sectorSize();
    uint32_t len = uint32_t((data.size() + ss - 1) / ss * ss);
    if (!u.save(d, off, len, err)) return false;
    AlignedBuffer b(len); std::memcpy(b.data(), data.data(), data.size());
    IoResult r = d.write(off, b.data(), len);
    if (!r.ok) { err = "Schreiben fehlgeschlagen: " + r.message; return false; }
    // sofort zurücklesen und vergleichen
    AlignedBuffer c(len);
    IoResult rr = d.read(off, c.data(), len);
    if (!rr.ok || std::memcmp(b.data(), c.data(), len) != 0) { err = "Geschriebene Daten kommen nicht zurück (Controller verwirft Schreibzugriffe?)"; return false; }
    return true;
}

// 1) Windows-Schreibschutzattribut entfernen (wie diskpart "attributes disk clear readonly")
inline ActionResult clearReadOnly(const std::string &devPath) {
    ActionResult a; RawDisk d; std::string e;
    if (!d.open(devPath, true, e)) { a.message = e; return a; }
    if (!d.setReadOnlyAttribute(false, e)) { a.message = e; return a; }
    bool ro = true; d.readOnlyAttribute(ro, e);
    a.ok = !ro; a.message = a.ok ? "Schreibschutzattribut entfernt" : "Attribut ist weiterhin gesetzt";
    return a;
}

// 2) Bootsektor aus der Sicherung wiederherstellen
inline ActionResult restoreBootSector(RawDisk &d, UndoJournal &u, const VolumeInfo &v) {
    ActionResult a; std::string e;
    if (!v.backupValid || !v.backupOffset) { a.message = "Keine gültige Sicherung vorhanden"; return a; }
    uint32_t sectors = v.type == FsType::FAT32 ? 3 : v.type == FsType::ExFAT ? 12 : 1;
    std::vector<uint8_t> b;
    BlockReader r(d);
    if (!r.read(v.backupOffset, size_t(sectors) * v.bytesPerSector, b, &e)) { a.message = "Sicherung nicht lesbar: " + e; return a; }
    if (!writeSectors(d, u, v.offset, b, e)) { a.message = e; return a; }
    d.flush(); d.refreshPartitions();
    a.ok = true; a.message = fmt("%s-Bootsektor aus Sicherung wiederhergestellt (%u Sektoren)", fsName(v.type), sectors);
    return a;
}

inline uint8_t mbrTypeFor(FsType t, uint64_t startLba, uint64_t sectors) {
    switch (t) {
    case FsType::FAT32: return 0x0C;
    case FsType::FAT16: return (startLba + sectors) > 16450560 ? 0x0E : 0x06;
    case FsType::FAT12: return 0x01;
    default: return 0x07;
    }
}

// 3) Partitionstabelle (MBR) für ein gefundenes Volume neu schreiben
inline ActionResult rebuildMbr(RawDisk &d, UndoJournal &u, const VolumeInfo &v) {
    ActionResult a; std::string e;
    const uint32_t ss = d.sectorSize();
    if (v.offset == 0) { a.message = "Volume beginnt bei Sektor 0 (Superfloppy) – keine Tabelle nötig"; return a; }
    uint64_t start = v.offset / ss;
    uint64_t count = (v.sizeBytes + ss - 1) / ss + (v.type == FsType::NTFS ? 1 : 0);
    if (start > 0xFFFFFFFFull || count > 0xFFFFFFFFull) { a.message = "Volume zu groß für MBR"; return a; }
    std::vector<uint8_t> mbr(ss, 0);
    BlockReader r(d); std::vector<uint8_t> old;
    if (r.read(0, ss, old)) std::memcpy(mbr.data(), old.data(), 440);   // vorhandenen Bootcode behalten
    std::mt19937 rng{uint32_t(nowMs())};
    wr32(&mbr[440], rng());
    uint8_t *p = &mbr[446];
    p[0] = 0x00; p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF;                 // CHS ungenutzt
    p[4] = mbrTypeFor(v.type, start, count);
    p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;
    wr32(p + 8, uint32_t(start)); wr32(p + 12, uint32_t(count));
    mbr[510] = 0x55; mbr[511] = 0xAA;
    if (!writeSectors(d, u, 0, mbr, e)) { a.message = e; return a; }
    d.flush(); d.refreshPartitions();
    a.ok = true; a.message = fmt("Neue Partitionstabelle: %s ab Sektor %llu, %s", fsName(v.type), (unsigned long long)start, humanSize(count * ss).c_str());
    return a;
}

// 4) FAT32 selbst formatieren (beliebige Größe bis 2 TB; Windows erlaubt FAT32 sonst nur bis 32 GB).
//    usableBytes < Datenträgergröße richtet Fake-Sticks auf die echte Kapazität ein.
inline ActionResult formatFat32(RawDisk &d, UndoJournal &u, uint64_t usableBytes, const std::string &label, ProgressFn progress, CancelToken *cancel) {
    ActionResult a; std::string e;
    const uint32_t ss = d.sectorSize();
    if (ss != 512) { a.message = "FAT32-Formatierung unterstützt nur 512-Byte-Sektoren"; return a; }
    usableBytes = std::min(usableBytes, d.size());
    const uint64_t start = 2048;                                   // 1 MiB Ausrichtung
    uint64_t total = usableBytes / ss;
    if (total <= start + 65536) { a.message = "Zu klein für FAT32 (mindestens ca. 64 MB)"; return a; }
    total -= start;
    if (total > 0xFFFFFFFFull) total = 0xFFFFFFFFull;
    uint32_t spc = total < 532480 ? 1 : total < 16777216 ? 8 : total < 33554432 ? 16 : total < 67108864 ? 32 : 64;
    const uint32_t reserved = 32, nf = 2;
    // FAT-Größe iterativ bestimmen
    uint32_t fatSz = 1;
    for (int it = 0; it < 10; ++it) {
        uint64_t clusters = (total - reserved - uint64_t(nf) * fatSz) / spc;
        uint32_t need = uint32_t(((clusters + 2) * 4 + ss - 1) / ss);
        if (need == fatSz) break;
        fatSz = need;
    }
    uint64_t clusters = (total - reserved - uint64_t(nf) * fatSz) / spc;
    if (clusters < 65525) { a.message = "Zu wenige Cluster für FAT32"; return a; }

    // MBR
    std::vector<uint8_t> mbr(512, 0);
    std::mt19937 rng{uint32_t(nowMs())};
    wr32(&mbr[440], rng());
    uint8_t *p = &mbr[446];
    p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF; p[4] = 0x0C; p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;
    wr32(p + 8, uint32_t(start)); wr32(p + 12, uint32_t(total));
    mbr[510] = 0x55; mbr[511] = 0xAA;

    // Bootsektor
    std::vector<uint8_t> bs(512, 0);
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90; std::memcpy(&bs[3], "MSWIN4.1", 8);
    wr16(&bs[11], 512); bs[13] = uint8_t(spc); wr16(&bs[14], uint16_t(reserved)); bs[16] = uint8_t(nf);
    bs[21] = 0xF8; wr16(&bs[24], 63); wr16(&bs[26], 255); wr32(&bs[28], uint32_t(start)); wr32(&bs[32], uint32_t(total));
    wr32(&bs[36], fatSz); wr32(&bs[44], 2); wr16(&bs[48], 1); wr16(&bs[50], 6);
    bs[64] = 0x80; bs[66] = 0x29; wr32(&bs[67], rng());
    std::string lab = label.substr(0, 11); for (auto &c : lab) c = char(std::toupper((unsigned char)c));
    lab.resize(11, ' '); std::memcpy(&bs[71], lab.data(), 11); std::memcpy(&bs[82], "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    // FSInfo
    std::vector<uint8_t> fsi(512, 0);
    wr32(&fsi[0], 0x41615252); wr32(&fsi[484], 0x61417272); wr32(&fsi[488], uint32_t(clusters - 1)); wr32(&fsi[492], 3); wr32(&fsi[508], 0xAA550000);
    std::vector<uint8_t> boot3(512 * 3, 0);
    std::memcpy(&boot3[0], bs.data(), 512); std::memcpy(&boot3[512], fsi.data(), 512);
    boot3[1024 + 510] = 0x55; boot3[1024 + 511] = 0xAA;

    const uint64_t base = start * ss;
    if (!writeSectors(d, u, 0, mbr, e)) { a.message = e; return a; }
    // Reservierter Bereich: leeren, dann Boot + Sicherung
    std::vector<uint8_t> resv(size_t(reserved) * 512, 0);
    std::memcpy(&resv[0], boot3.data(), boot3.size());
    std::memcpy(&resv[6 * 512], boot3.data(), boot3.size());
    if (!writeSectors(d, u, base, resv, e)) { a.message = e; return a; }
    // FATs (nur Anfang sichern – der Rest ist für die Rückgängig-Datei zu groß; Abbild ist Pflicht)
    const uint64_t fatBytes = uint64_t(fatSz) * ss;
    AlignedBuffer zero(1 << 20);
    for (uint32_t f = 0; f < nf; ++f) {
        uint64_t fo = base + uint64_t(reserved) * ss + f * fatBytes;
        for (uint64_t o = 0; o < fatBytes; o += zero.size()) {
            if (cancel && cancel->cancelled()) { a.message = "Abgebrochen – Stick ist jetzt unvollständig formatiert"; return a; }
            size_t n = size_t(std::min<uint64_t>(zero.size(), fatBytes - o));
            std::memset(zero.data(), 0, n);
            if (o == 0) { wr32(zero.data(), 0x0FFFFFF8); wr32(zero.data() + 4, 0x0FFFFFFF); wr32(zero.data() + 8, 0x0FFFFFFF); }
            IoResult r = d.write(fo + o, zero.data(), n);
            if (!r.ok) { a.message = "FAT schreiben fehlgeschlagen: " + r.message; return a; }
            if (progress) progress(double(f * fatBytes + o) * 100.0 / double(nf * fatBytes), "Schreibe FAT");
        }
    }
    // Stammverzeichnis (Cluster 2) leeren, Volumename eintragen
    const uint64_t data0 = base + (uint64_t(reserved) + uint64_t(nf) * fatSz) * ss;
    std::memset(zero.data(), 0, zero.size());
    std::memcpy(zero.data(), lab.data(), 11); zero.data()[11] = 0x08;
    IoResult r = d.write(data0, zero.data(), size_t(spc) * ss);
    if (!r.ok) { a.message = "Stammverzeichnis schreiben fehlgeschlagen: " + r.message; return a; }
    d.flush(); d.refreshPartitions();
    a.ok = true;
    a.message = fmt("FAT32 erstellt: %s nutzbar, Clustergröße %u Bytes", humanSize(clusters * spc * ss).c_str(), spc * ss);
    return a;
}

// 5) Neu formatieren über Windows (diskpart): exFAT oder NTFS, optional begrenzte Größe (Fake-Sticks).
inline ActionResult formatWithWindows(int diskNumber, const std::string &fs, uint64_t sizeLimitBytes, const std::string &label, const std::string &workDir) {
    ActionResult a;
#ifdef _WIN32
    if (diskNumber < 0) { a.message = "Keine Datenträgernummer"; return a; }
    std::string script = fmt("select disk %d\nattributes disk clear readonly\nclean\n", diskNumber);
    script += sizeLimitBytes ? fmt("create partition primary size=%llu\n", (unsigned long long)(sizeLimitBytes / (1024 * 1024))) : std::string("create partition primary\n");
    script += "format fs=" + fs + " quick label=\"" + label.substr(0, 11) + "\"\nassign\nexit\n";
    std::string sp = workDir + "\\diskpart_script.txt", lp = workDir + "\\diskpart_ausgabe.txt";
    if (!writeTextFile(sp, script)) { a.message = "Skript nicht schreibbar"; return a; }
    std::wstring cmd = L"cmd.exe /c diskpart /s \"" + widen(sp) + L"\" > \"" + widen(lp) + L"\" 2>&1";
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> c(cmd.begin(), cmd.end()); c.push_back(0);
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { a.message = winErrorText(GetLastError()); return a; }
    WaitForSingleObject(pi.hProcess, 15 * 60 * 1000);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    std::string out; readTextFile(lp, out);
    a.details.push_back(out);
    a.ok = code == 0 && out.find("100") != std::string::npos && out.find("DiskPart-Fehler") == std::string::npos && out.find("DiskPart has encountered an error") == std::string::npos;
    a.message = a.ok ? "Neu formatiert (" + fs + ")" : "diskpart meldet einen Fehler – Ausgabe in der Fallakte";
#else
    (void)diskNumber; (void)fs; (void)sizeLimitBytes; (void)label; (void)workDir;
    a.message = "Nur unter Windows verfügbar";
#endif
    return a;
}

// 6) Schreibtest: prüft, ob der Controller Schreibzugriffe annimmt (nur nach Freigabe, mit Rückgängig-Datei)
inline ActionResult writeAcceptTest(RawDisk &d, UndoJournal &u, uint64_t off) {
    ActionResult a; std::string e;
    const uint32_t ss = d.sectorSize();
    off = off / ss * ss;
    if (!u.save(d, off, ss, e)) { a.message = e; return a; }
    AlignedBuffer orig(ss), pat(ss), back(ss);
    d.read(off, orig.data(), ss);
    for (uint32_t i = 0; i < ss; ++i) pat.data()[i] = uint8_t(0xA5 ^ i);
    IoResult w = d.write(off, pat.data(), ss);
    IoResult r = d.read(off, back.data(), ss);
    bool same = r.ok && std::memcmp(pat.data(), back.data(), ss) == 0;
    d.write(off, orig.data(), ss);
    if (!w.ok) a.message = "Controller lehnt Schreibzugriffe ab: " + w.message;
    else if (!same) a.message = "Schreiben wird angenommen, aber nicht gespeichert (Controller verwirft Daten)";
    else { a.ok = true; a.message = "Schreiben funktioniert"; }
    return a;
}

} // namespace usbr
