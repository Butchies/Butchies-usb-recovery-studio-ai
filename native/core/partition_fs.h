#pragma once
// Lesende Analyse von Partitionstabelle (MBR/GPT/Superfloppy) und Bootsektoren (FAT12/16/32, exFAT, NTFS)
// inklusive Sicherungs-Bootsektoren und Suche nach verlorenen Partitionen.
#include "reader.h"

namespace usbr {

enum class FsType { None, FAT12, FAT16, FAT32, ExFAT, NTFS };
inline const char *fsName(FsType t) {
    switch (t) { case FsType::FAT12: return "FAT12"; case FsType::FAT16: return "FAT16"; case FsType::FAT32: return "FAT32";
    case FsType::ExFAT: return "exFAT"; case FsType::NTFS: return "NTFS"; default: return "unbekannt"; }
}

struct VolumeInfo {
    FsType type = FsType::None;
    uint64_t offset = 0;          // Byte-Versatz des Bootsektors auf dem Datenträger
    uint64_t sizeBytes = 0;       // laut Bootsektor
    uint32_t bytesPerSector = 512;
    uint32_t clusterSize = 0;
    std::string label;
    bool primaryValid = false;
    bool backupValid = false;
    uint64_t backupOffset = 0;
    bool fromScan = false;        // nur durch Suche gefunden (nicht in Partitionstabelle)
    // FAT
    uint32_t reserved = 0, fatCount = 0, fatSectors = 0, rootEntries = 0, rootCluster = 0;
    // exFAT
    uint32_t fatOffsetSec = 0, fatLengthSec = 0, heapOffsetSec = 0, clusterCount = 0;
    std::vector<std::string> issues;
};

struct PartitionEntry { int index = 0; uint8_t type = 0; std::string typeName; uint64_t startLba = 0, sectors = 0; bool active = false; };

struct LayoutReport {
    bool sector0Readable = false;
    Pattern sector0Pattern = Pattern::Data;
    std::string scheme = "keins";   // MBR, GPT, Superfloppy, keins
    std::vector<PartitionEntry> partitions;
    std::vector<VolumeInfo> volumes;   // aus der Tabelle
    std::vector<VolumeInfo> found;     // durch Suche gefunden
    std::vector<std::string> issues;
    std::vector<std::string> facts;
    bool tableDamaged = false;
    bool fsDamaged = false;
    bool fsRepairableFromBackup = false;
    bool beyondEnd = false;           // Partition größer als der Datenträger meldet (Fake-Hinweis)
    bool healthy() const { return !tableDamaged && !fsDamaged && !volumes.empty() && issues.empty(); }
};

inline bool isPow2(uint32_t v) { return v && !(v & (v - 1)); }

// Prüft einen 512-Byte-Bootsektor. Gibt true zurück, wenn ein gültiges Dateisystem erkannt wurde.
inline bool parseBootSector(const uint8_t *b, uint64_t offset, VolumeInfo &v) {
    v = VolumeInfo{}; v.offset = offset;
    const bool sig = b[510] == 0x55 && b[511] == 0xAA;
    if (std::memcmp(b + 3, "EXFAT   ", 8) == 0) {
        for (int i = 11; i < 64; ++i) if (b[i]) return false;
        uint8_t bpsShift = b[108], spcShift = b[109];
        if (bpsShift < 9 || bpsShift > 12 || spcShift > 25 - bpsShift || !sig) return false;
        v.type = FsType::ExFAT; v.bytesPerSector = 1u << bpsShift; v.clusterSize = v.bytesPerSector << spcShift;
        v.sizeBytes = rd64(b + 72) * v.bytesPerSector;
        v.fatOffsetSec = rd32(b + 80); v.fatLengthSec = rd32(b + 84); v.heapOffsetSec = rd32(b + 88);
        v.clusterCount = rd32(b + 92); v.rootCluster = rd32(b + 96);
        v.backupOffset = offset + 12ull * v.bytesPerSector;
        if (v.rootCluster < 2 || v.rootCluster > v.clusterCount + 1) v.issues.push_back("exFAT: Stammverzeichnis-Cluster ungültig");
        return true;
    }
    if (std::memcmp(b + 3, "NTFS    ", 8) == 0) {
        uint32_t bps = rd16(b + 11); uint8_t spc = b[13];
        if (!isPow2(bps) || bps < 512 || bps > 4096 || !sig) return false;
        uint32_t cs = spc <= 0x80 ? spc * bps : (1u << (256 - spc));
        v.type = FsType::NTFS; v.bytesPerSector = bps; v.clusterSize = cs;
        uint64_t total = rd64(b + 40);
        v.sizeBytes = total * bps;
        v.backupOffset = offset + total * bps;  // letzter Sektor hinter dem Volume
        return total > 0;
    }
    // FAT
    if (!(b[0] == 0xEB || b[0] == 0xE9)) return false;
    uint32_t bps = rd16(b + 11), spc = b[13], res = rd16(b + 14), nf = b[16], root = rd16(b + 17);
    uint32_t tot = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
    uint32_t fsz = rd16(b + 22) ? rd16(b + 22) : rd32(b + 36);
    if (!isPow2(bps) || bps < 512 || bps > 4096 || !isPow2(spc) || spc > 128 || res == 0 || nf == 0 || nf > 4 || tot == 0 || fsz == 0) return false;
    if (b[21] != 0xF0 && b[21] < 0xF8) return false;
    uint32_t rootSec = (root * 32 + bps - 1) / bps;
    uint64_t meta = uint64_t(res) + uint64_t(nf) * fsz + rootSec;
    if (meta >= tot) return false;
    uint32_t clusters = uint32_t((tot - meta) / spc);
    v.type = clusters < 4085 ? FsType::FAT12 : clusters < 65525 ? FsType::FAT16 : FsType::FAT32;
    v.bytesPerSector = bps; v.clusterSize = bps * spc; v.sizeBytes = uint64_t(tot) * bps;
    v.reserved = res; v.fatCount = nf; v.fatSectors = fsz; v.rootEntries = root;
    if (v.type == FsType::FAT32) {
        v.rootCluster = rd32(b + 44);
        uint32_t bk = rd16(b + 50);
        if (bk && bk < res) v.backupOffset = offset + uint64_t(bk) * bps;
        v.label = trim(std::string(reinterpret_cast<const char *>(b + 71), 11));
    } else v.label = trim(std::string(reinterpret_cast<const char *>(b + 43), 11));
    if (!sig) v.issues.push_back("Bootsektor-Signatur 55AA fehlt");
    return true;
}

inline std::string mbrTypeName(uint8_t t) {
    switch (t) {
    case 0x01: return "FAT12"; case 0x04: case 0x06: case 0x0E: return "FAT16"; case 0x0B: case 0x0C: return "FAT32";
    case 0x07: return "NTFS/exFAT"; case 0x05: case 0x0F: return "Erweitert"; case 0x83: return "Linux"; case 0xEE: return "GPT-Schutz";
    case 0xEF: return "EFI"; case 0x00: return "leer"; default: return "Typ " + hex2(t);
    }
}

// Volume an gegebenem Versatz prüfen, inklusive Sicherung
inline VolumeInfo checkVolume(BlockReader &r, uint64_t offset, bool fromScan) {
    VolumeInfo v; std::vector<uint8_t> b;
    if (!r.read(offset, 512, b)) { v.offset = offset; v.issues.push_back("Bootsektor nicht lesbar"); return v; }
    v.primaryValid = parseBootSector(b.data(), offset, v);
    v.fromScan = fromScan;
    if (!v.primaryValid) { v.offset = offset; return v; }
    if (v.backupOffset) {
        std::vector<uint8_t> bb; VolumeInfo bv;
        if (r.read(v.backupOffset, 512, bb) && parseBootSector(bb.data(), offset, bv) && bv.type == v.type) {
            v.backupValid = true;
            if (std::memcmp(b.data(), bb.data(), 512) != 0 && v.type != FsType::ExFAT) v.issues.push_back("Bootsektor und Sicherung unterscheiden sich");
        }
    }
    return v;
}

// Sucht im Bereich nach Sicherungs-Bootsektoren, deren Original zerstört ist.
inline void findFromBackups(BlockReader &r, uint64_t from, LayoutReport &rep, ProgressFn progress, CancelToken *cancel) {
    const uint64_t limit = std::min<uint64_t>(r.size(), 64ull << 20);
    std::vector<uint8_t> chunk;
    for (uint64_t off = from; off < limit; off += 1 << 20) {
        if (cancel && cancel->cancelled()) return;
        if (progress) progress(double(off) * 100.0 / double(limit), "Suche verlorene Partitionen");
        size_t len = size_t(std::min<uint64_t>(1 << 20, limit - off));
        if (!r.read(off, len, chunk)) continue;
        for (size_t i = 0; i + 512 <= len; i += 512) {
            const uint8_t *s = chunk.data() + i;
            if (s[510] != 0x55 || s[511] != 0xAA) continue;
            VolumeInfo v;
            if (!parseBootSector(s, off + i, v)) continue;
            uint64_t start = off + i;
            // Ist das eine Sicherung (FAT32: Sektor 6, exFAT: Sektor 12), deren Original davor zerstört ist?
            // Ein Original erkennt man daran, dass hinter ihm die passende Sicherung liegt.
            uint64_t bkDist = 0;
            if (v.type == FsType::FAT32) bkDist = uint64_t(rd16(s + 50)) * v.bytesPerSector;
            if (v.type == FsType::ExFAT) bkDist = 12ull * v.bytesPerSector;
            if (bkDist) {
                std::vector<uint8_t> after, before; VolumeInfo av, bv;
                bool hasBackupAfter = r.read(start + bkDist, 512, after) && parseBootSector(after.data(), start, av) && av.type == v.type;
                if (!hasBackupAfter && start >= bkDist) {
                    uint64_t cand = start - bkDist;
                    bool primaryBefore = r.read(cand, 512, before) && parseBootSector(before.data(), cand, bv);
                    if (primaryBefore) continue;              // Sicherung eines intakten Originals
                    // Gegenprobe über die FAT: Sie muss relativ zum vermuteten Original stehen.
                    uint64_t fatRel = v.type == FsType::FAT32 ? uint64_t(v.reserved) * v.bytesPerSector : uint64_t(v.fatOffsetSec) * v.bytesPerSector;
                    auto fatAt = [&](uint64_t base) {
                        std::vector<uint8_t> f;
                        if (!r.read(base + fatRel, 512, f)) return false;
                        return f[0] >= 0xF0 && f[1] == 0xFF && f[2] == 0xFF && (f[3] == 0x0F || f[3] == 0xFF);
                    };
                    if (!fatAt(cand) || fatAt(start)) { /* eher ein Original ohne Sicherung */ goto asPrimary; }
                    v.offset = cand; v.backupOffset = start; v.backupValid = true; v.primaryValid = false; v.fromScan = true;
                    v.issues.push_back("Original-Bootsektor zerstört, Sicherung gefunden");
                    bool dup = false;
                    for (auto &x : rep.found) if (x.offset == cand) dup = true;
                    if (!dup) rep.found.push_back(v);
                    continue;
                }
            }
        asPrimary:
            bool dup = false;
            for (auto &x : rep.volumes) if (x.offset == start || x.backupOffset == start) dup = true;
            for (auto &x : rep.found) if (x.offset == start || x.backupOffset == start) dup = true;
            if (dup) continue;
            v.primaryValid = true; v.fromScan = true;
            rep.found.push_back(v);
        }
    }
    // NTFS-Sicherung am Ende des Datenträgers
    if (r.size() > 1024) {
        std::vector<uint8_t> e;
        for (uint64_t back : {512ull, 1024ull}) {
            uint64_t off = r.size() - back;
            VolumeInfo v;
            if (r.read(off, 512, e) && parseBootSector(e.data(), off, v) && v.type == FsType::NTFS && v.sizeBytes <= off) {
                uint64_t cand = off - v.sizeBytes;
                bool known = false;
                for (auto &x : rep.volumes) if (x.offset == cand && x.primaryValid) known = true;
                if (!known) {
                    v.offset = cand; v.backupOffset = off; v.backupValid = true; v.fromScan = true; v.primaryValid = false;
                    std::vector<uint8_t> p; VolumeInfo pv;
                    if (r.read(cand, 512, p) && parseBootSector(p.data(), cand, pv) && pv.type == FsType::NTFS) v.primaryValid = true;
                    else v.issues.push_back("NTFS-Bootsektor zerstört, Sicherung am Ende gefunden");
                    rep.found.push_back(v);
                }
            }
        }
    }
}

inline LayoutReport analyzeLayout(BlockReader &r, ProgressFn progress = {}, CancelToken *cancel = nullptr) {
    LayoutReport rep;
    std::vector<uint8_t> s0;
    std::string err;
    if (!r.read(0, 4096, s0, &err)) {
        rep.issues.push_back("Sektor 0 nicht lesbar: " + err);
        rep.tableDamaged = true;
        return rep;
    }
    rep.sector0Readable = true;
    rep.sector0Pattern = classify(s0.data(), 4096);
    if (rep.sector0Pattern == Pattern::Zeros) rep.issues.push_back("Sektor 0 enthält nur Nullen – Partitionstabelle gelöscht oder Controller liefert leere Daten");
    if (rep.sector0Pattern == Pattern::Ones) rep.issues.push_back("Sektor 0 enthält nur FF – typisch für gelöschten NAND oder verlorene Controller-Zuordnung");
    const uint64_t ss = r.sector();
    const uint64_t diskBytes = r.size();

    VolumeInfo sf;
    if (parseBootSector(s0.data(), 0, sf)) {
        rep.scheme = "Superfloppy";
        rep.facts.push_back(std::string("Kein Partitionsschema – Dateisystem beginnt direkt bei Sektor 0 (") + fsName(sf.type) + ")");
        rep.volumes.push_back(checkVolume(r, 0, false));
    } else if (s0[510] == 0x55 && s0[511] == 0xAA) {
        bool gpt = false;
        for (int i = 0; i < 4; ++i) {
            const uint8_t *e = s0.data() + 446 + i * 16;
            PartitionEntry p; p.index = i + 1; p.type = e[4]; p.active = e[0] == 0x80;
            p.startLba = rd32(e + 8); p.sectors = rd32(e + 12); p.typeName = mbrTypeName(p.type);
            if (p.type == 0 || p.sectors == 0) continue;
            if (p.type == 0xEE) gpt = true;
            if (e[0] != 0 && e[0] != 0x80) rep.issues.push_back(fmt("Partition %d: ungültiges Statusbyte %02X", i + 1, e[0]));
            rep.partitions.push_back(p);
        }
        if (gpt) {
            rep.scheme = "GPT";
            std::vector<uint8_t> h;
            if (r.read(ss, 512, h) && std::memcmp(h.data(), "EFI PART", 8) == 0) {
                rep.partitions.clear();
                uint64_t elba = rd64(h.data() + 72); uint32_t cnt = rd32(h.data() + 80), esz = rd32(h.data() + 84);
                if (esz >= 128 && cnt <= 256) {
                    std::vector<uint8_t> ents;
                    if (r.read(elba * ss, size_t(cnt) * esz, ents)) {
                        for (uint32_t i = 0; i < cnt; ++i) {
                            const uint8_t *e = ents.data() + size_t(i) * esz;
                            bool used = false; for (int k = 0; k < 16; ++k) if (e[k]) used = true;
                            if (!used) continue;
                            PartitionEntry p; p.index = int(i + 1); p.startLba = rd64(e + 32); p.sectors = rd64(e + 40) - p.startLba + 1; p.typeName = "GPT";
                            rep.partitions.push_back(p);
                        }
                    } else rep.issues.push_back("GPT-Partitionseinträge nicht lesbar");
                }
            } else { rep.issues.push_back("Schutz-MBR vorhanden, aber GPT-Kopf fehlt oder beschädigt"); rep.tableDamaged = true; }
        } else rep.scheme = "MBR";
        for (auto &p : rep.partitions) {
            if (p.type == 0x05 || p.type == 0x0F) continue;
            uint64_t off = p.startLba * ss, end = off + p.sectors * ss;
            if (diskBytes && end > diskBytes) {
                rep.beyondEnd = true;
                rep.issues.push_back(fmt("Partition %d reicht über das Ende des Datenträgers hinaus (%s statt %s)", p.index, humanSize(end).c_str(), humanSize(diskBytes).c_str()));
            }
            if (diskBytes && off >= diskBytes) continue;
            VolumeInfo v = checkVolume(r, off, false);
            if (!v.primaryValid) {
                rep.fsDamaged = true;
                rep.issues.push_back(fmt("Partition %d (%s): kein gültiges Dateisystem – Windows zeigt sie als RAW", p.index, p.typeName.c_str()));
            }
            rep.volumes.push_back(v);
        }
        if (rep.partitions.empty()) { rep.issues.push_back("Partitionstabelle ist leer"); rep.tableDamaged = true; }
    } else {
        rep.tableDamaged = true;
        if (rep.sector0Pattern == Pattern::Data) rep.issues.push_back("Keine gültige Partitionstabelle (Signatur 55AA fehlt)");
    }
    for (auto &v : rep.volumes) {
        if (v.primaryValid) {
            rep.facts.push_back(fmt("%s-Volume bei %s, %s, Cluster %u Bytes%s", fsName(v.type), humanSize(v.offset).c_str(), humanSize(v.sizeBytes).c_str(), v.clusterSize,
                                    v.label.empty() ? "" : (", Name „" + v.label + "“").c_str()));
            if (v.backupOffset && !v.backupValid) rep.facts.push_back(std::string(fsName(v.type)) + ": Sicherungs-Bootsektor fehlt oder ungültig");
            for (auto &i : v.issues) rep.issues.push_back(i);
            if (diskBytes && v.offset + v.sizeBytes > diskBytes + 512) { rep.beyondEnd = true; rep.issues.push_back("Dateisystem ist größer als der Datenträger meldet – Hinweis auf Fake-Kapazität oder falsch gemeldete Größe"); }
        }
    }
    findFromBackups(r, 0, rep, progress, cancel);
    for (auto &f : rep.found) {
        if (!f.primaryValid && f.backupValid) rep.fsRepairableFromBackup = true;
        rep.facts.push_back(fmt("Gefunden: %s bei %s (%s)%s", fsName(f.type), humanSize(f.offset).c_str(), humanSize(f.sizeBytes).c_str(), f.primaryValid ? "" : " – nur Sicherung intakt"));
    }
    for (auto &v : rep.volumes) if (!v.primaryValid) {
        for (auto &f : rep.found) if (f.offset == v.offset && f.backupValid) rep.fsRepairableFromBackup = true;
    }
    if (rep.tableDamaged && !rep.found.empty()) rep.facts.push_back("Partitionstabelle kann aus gefundenem Dateisystem neu erstellt werden");
    return rep;
}

} // namespace usbr
