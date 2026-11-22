#pragma once
// Dateien aus FAT12/16/32 und exFAT auslesen – auch gelöschte Einträge – direkt vom Stick oder aus dem Abbild.
#include "partition_fs.h"
#include <filesystem>
#include <set>

namespace usbr {

inline std::filesystem::path u8path(const std::string &s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
inline std::string pathString(const std::filesystem::path &p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char *>(u.data()), u.size());
}

inline void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}
inline std::string utf16ToUtf8(const std::vector<uint16_t> &w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = w[i];
        if (c == 0 || c == 0xFFFF) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) { c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00); ++i; }
        appendUtf8(s, c);
    }
    return s;
}
inline std::string safeName(std::string n) {
    for (auto &c : n) if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 32) c = '_';
    while (!n.empty() && (n.back() == '.' || n.back() == ' ')) n.pop_back();
    if (n.empty() || n == "." || n == "..") n = "_";
    return n;
}

// "Bild.png" -> "Bild (gelöscht).png", damit die Endung erhalten bleibt
inline std::string deletedName(const std::string &n, bool del) {
    if (!del) return n;
    auto dot = n.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return n + " (gelöscht)";
    return n.substr(0, dot) + " (gelöscht)" + n.substr(dot);
}

struct FsEntry {
    std::string path;          // relativer Pfad mit '/'
    bool dir = false;
    bool deleted = false;
    uint64_t size = 0;
    uint32_t firstCluster = 0;
    bool noFatChain = false;   // exFAT: zusammenhängend gespeichert
    bool damaged = false;      // Kette fehlerhaft / teilweise überschrieben
};

class FatVolumeReader {
public:
    bool open(BlockReader &r, const VolumeInfo &v, std::string &err) {
        r_ = &r; v_ = v;
        if (v.type == FsType::None) { err = "Kein Dateisystem"; return false; }
        if (v.type == FsType::NTFS) { err = "NTFS wird per Signatursuche gerettet (Dateiliste folgt in einer späteren Version)"; return false; }
        const uint64_t bps = v.bytesPerSector;
        if (v.type == FsType::ExFAT) {
            fatStart_ = v.offset + uint64_t(v.fatOffsetSec) * bps;
            dataStart_ = v.offset + uint64_t(v.heapOffsetSec) * bps;
            fatBytes_ = uint64_t(v.fatLengthSec) * bps;
            clusters_ = v.clusterCount;
        } else {
            fatStart_ = v.offset + uint64_t(v.reserved) * bps;
            fatBytes_ = uint64_t(v.fatSectors) * bps;
            rootDirStart_ = fatStart_ + uint64_t(v.fatCount) * fatBytes_;
            uint64_t rootSec = (uint64_t(v.rootEntries) * 32 + bps - 1) / bps;
            dataStart_ = rootDirStart_ + rootSec * bps;
            clusters_ = uint32_t((v.sizeBytes - (dataStart_ - v.offset)) / v.clusterSize);
        }
        if (fatBytes_ > (512ull << 20)) { err = "FAT unplausibel groß"; return false; }
        // FAT laden: erst Kopie 1, bei Lesefehler Kopie 2 (nur FAT)
        if (!r.read(fatStart_, size_t(fatBytes_), fat_)) {
            bool ok = false;
            if (v.type != FsType::ExFAT && v.fatCount > 1) ok = r.read(fatStart_ + fatBytes_, size_t(fatBytes_), fat_);
            if (!ok) notes_.push_back("FAT teilweise unlesbar – Ketten werden als zusammenhängend angenommen");
            fatDamaged_ = !ok;
        }
        return true;
    }

    const std::vector<std::string> &notes() const { return notes_; }
    uint32_t clusterSize() const { return v_.clusterSize; }

    std::vector<FsEntry> list(size_t maxEntries, CancelToken *cancel, ProgressFn progress = {}) {
        std::vector<FsEntry> out;
        visited_.clear();
        if (v_.type == FsType::ExFAT) listExfat(v_.rootCluster, false, "", out, maxEntries, cancel, 0, progress);
        else if (v_.type == FsType::FAT32) listFat(v_.rootCluster, "", out, maxEntries, cancel, 0, progress);
        else listFatRoot16("", out, maxEntries, cancel, progress);
        return out;
    }

    // Datei in Zieldatei schreiben. badBytes = nicht lesbare Bytes (mit Nullen gefüllt).
    bool extract(const FsEntry &e, OutFile &f, uint64_t &badBytes, CancelToken *cancel) {
        badBytes = 0;
        if (e.size == 0) return true;
        std::vector<uint32_t> chain = clusterChain(e.firstCluster, e.size, e.noFatChain || e.deleted);
        uint64_t left = e.size, pos = 0;
        std::vector<uint8_t> buf;
        for (size_t i = 0; i < chain.size() && left; ) {
            if (cancel && cancel->cancelled()) return false;
            // aufeinanderfolgende Cluster zusammen lesen (schneller)
            size_t j = i + 1;
            while (j < chain.size() && chain[j] == chain[j - 1] + 1 && (j - i) * v_.clusterSize < (4u << 20)) ++j;
            uint64_t n = std::min<uint64_t>(left, uint64_t(j - i) * v_.clusterSize);
            if (!r_->read(clusterOffset(chain[i]), size_t(n), buf)) {
                // Einzeln nachlesen, nur defekte Cluster bleiben leer
                for (size_t k = i; k < j; ++k) {
                    uint64_t m = std::min<uint64_t>(left - uint64_t(k - i) * v_.clusterSize, v_.clusterSize);
                    if ((k - i) * uint64_t(v_.clusterSize) >= n) break;
                    std::vector<uint8_t> one;
                    if (!r_->read(clusterOffset(chain[k]), size_t(m), one)) badBytes += m;
                    std::memcpy(buf.data() + (k - i) * v_.clusterSize, one.data(), size_t(m));
                }
            }
            if (!f.writeAt(pos, buf.data(), size_t(n))) return false;
            pos += n; left -= n; i = j;
        }
        if (left) badBytes += left;
        return true;
    }

private:
    uint64_t clusterOffset(uint32_t c) const { return dataStart_ + uint64_t(c - 2) * v_.clusterSize; }
    bool validCluster(uint32_t c) const { return c >= 2 && c < clusters_ + 2; }

    uint32_t fatNext(uint32_t c) const {
        if (fatDamaged_) return c + 1;
        switch (v_.type) {
        case FsType::FAT12: { size_t o = c + c / 2; if (o + 1 >= fat_.size()) return 0xFFFFFFFF; uint16_t w = rd16(&fat_[o]); uint32_t x = (c & 1) ? (w >> 4) : (w & 0xFFF); return x >= 0xFF8 ? 0xFFFFFFFF : x; }
        case FsType::FAT16: { size_t o = size_t(c) * 2; if (o + 1 >= fat_.size()) return 0xFFFFFFFF; uint32_t x = rd16(&fat_[o]); return x >= 0xFFF8 ? 0xFFFFFFFF : x; }
        case FsType::FAT32: { size_t o = size_t(c) * 4; if (o + 3 >= fat_.size()) return 0xFFFFFFFF; uint32_t x = rd32(&fat_[o]) & 0x0FFFFFFF; return x >= 0x0FFFFFF8 ? 0xFFFFFFFF : x; }
        case FsType::ExFAT: { size_t o = size_t(c) * 4; if (o + 3 >= fat_.size()) return 0xFFFFFFFF; uint32_t x = rd32(&fat_[o]); return x >= 0xFFFFFFF7 ? 0xFFFFFFFF : x; }
        default: return 0xFFFFFFFF;
        }
    }

    std::vector<uint32_t> clusterChain(uint32_t first, uint64_t size, bool contiguous) const {
        std::vector<uint32_t> ch;
        if (!validCluster(first)) return ch;
        uint64_t need = size ? (size + v_.clusterSize - 1) / v_.clusterSize : UINT64_MAX;
        if (contiguous) {
            for (uint64_t i = 0; i < need && validCluster(uint32_t(first + i)); ++i) ch.push_back(uint32_t(first + i));
            return ch;
        }
        std::set<uint32_t> seen;
        uint32_t c = first;
        while (validCluster(c) && ch.size() < need && seen.insert(c).second) { ch.push_back(c); c = fatNext(c); if (c == 0) break; }
        return ch;
    }

    bool readChain(uint32_t first, bool contiguous, uint64_t maxBytes, std::vector<uint8_t> &out) {
        out.clear();
        auto ch = clusterChain(first, contiguous ? maxBytes : 0, contiguous);
        std::vector<uint8_t> b;
        for (uint32_t c : ch) {
            if (out.size() >= maxBytes) break;
            r_->read(clusterOffset(c), v_.clusterSize, b);
            out.insert(out.end(), b.begin(), b.end());
        }
        return !out.empty();
    }

    void parseFatDir(const std::vector<uint8_t> &d, const std::string &base, std::vector<FsEntry> &out, size_t maxE, CancelToken *cancel, int depth, ProgressFn &progress) {
        std::vector<uint16_t> lfn; uint8_t lfnSum = 0; bool lfnOk = false;
        for (size_t i = 0; i + 32 <= d.size() && out.size() < maxE; i += 32) {
            if (cancel && cancel->cancelled()) return;
            const uint8_t *e = d.data() + i;
            if (e[0] == 0x00) break;
            const bool del = e[0] == 0xE5;
            if (e[11] == 0x0F) {
                int ord = e[0] & 0x1F;
                if (del) ord = 0; // gelöschte LFN: Reihenfolge unbekannt, Teile werden rückwärts gesammelt
                std::vector<uint16_t> part;
                for (int k : {1, 3, 5, 7, 9}) part.push_back(rd16(e + k));
                for (int k : {14, 16, 18, 20, 22, 24}) part.push_back(rd16(e + k));
                for (int k : {28, 30}) part.push_back(rd16(e + k));
                if (del ? (!lfnOk || e[13] != lfnSum) : (e[0] & 0x40) != 0) { lfn.clear(); lfnSum = e[13]; lfnOk = true; }
                if (e[13] != lfnSum) lfnOk = false;
                lfn.insert(lfn.begin(), part.begin(), part.end());
                (void)ord;
                continue;
            }
            if (e[11] & 0x08) { lfn.clear(); lfnOk = false; continue; } // Volumename
            std::string shortName;
            for (int k = 0; k < 8; ++k) if (e[k] != ' ') shortName += char(k == 0 && del ? '_' : e[k]);
            std::string ext; for (int k = 8; k < 11; ++k) if (e[k] != ' ') ext += char(e[k]);
            if (!ext.empty()) shortName += "." + ext;
            uint8_t sum = 0; for (int k = 0; k < 11; ++k) sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + (k == 0 && del ? 0xE5 : e[k]));
            std::string name = shortName;
            if (lfnOk && !lfn.empty()) {
                std::string ln = utf16ToUtf8(lfn);
                if (!ln.empty() && (sum == lfnSum || del)) name = ln;
            }
            lfn.clear(); lfnOk = false;
            if (shortName == "." || shortName == ".." || shortName.rfind("..", 0) == 0) continue;
            FsEntry fe;
            fe.dir = (e[11] & 0x10) != 0; fe.deleted = del;
            fe.firstCluster = (uint32_t(rd16(e + 20)) << 16) | rd16(e + 26);
            if (v_.type != FsType::FAT32) fe.firstCluster &= 0xFFFF;
            fe.size = rd32(e + 28);
            fe.path = base + deletedName(safeName(name), del);
            if (del && fe.firstCluster && !fatDamaged_) {
                uint32_t c = fe.firstCluster; uint64_t need = (fe.size + v_.clusterSize - 1) / v_.clusterSize;
                for (uint64_t k = 0; k < need && validCluster(uint32_t(c + k)); ++k) if (fatNext(uint32_t(c + k)) != 0) { fe.damaged = true; break; }
            }
            if (!fe.dir && fe.firstCluster && !validCluster(fe.firstCluster)) fe.damaged = true;
            out.push_back(fe);
            if (fe.dir && validCluster(fe.firstCluster) && depth < 32 && visited_.insert(fe.firstCluster).second)
                listFat(fe.firstCluster, fe.path + "/", out, maxE, cancel, depth + 1, progress, del);
        }
    }

    void listFat(uint32_t cl, const std::string &base, std::vector<FsEntry> &out, size_t maxE, CancelToken *cancel, int depth, ProgressFn &progress, bool contiguous = false) {
        std::vector<uint8_t> d;
        if (!readChain(cl, contiguous, contiguous ? uint64_t(v_.clusterSize) * 4 : (64ull << 20), d)) return;
        if (progress && depth <= 1) progress(-1, "Lese Verzeichnis " + (base.empty() ? std::string("/") : base));
        parseFatDir(d, base, out, maxE, cancel, depth, progress);
    }
    void listFatRoot16(const std::string &base, std::vector<FsEntry> &out, size_t maxE, CancelToken *cancel, ProgressFn &progress) {
        std::vector<uint8_t> d;
        r_->read(rootDirStart_, size_t(v_.rootEntries) * 32, d);
        parseFatDir(d, base, out, maxE, cancel, 0, progress);
    }

    void listExfat(uint32_t cl, bool noChain, const std::string &base, std::vector<FsEntry> &out, size_t maxE, CancelToken *cancel, int depth, ProgressFn &progress, uint64_t dirSize = 0) {
        std::vector<uint8_t> d;
        if (!readChain(cl, noChain, noChain && dirSize ? dirSize : (64ull << 20), d)) return;
        if (progress && depth <= 1) progress(-1, "Lese Verzeichnis " + (base.empty() ? std::string("/") : base));
        for (size_t i = 0; i + 32 <= d.size() && out.size() < maxE; i += 32) {
            if (cancel && cancel->cancelled()) return;
            const uint8_t *e = d.data() + i;
            if (e[0] == 0x00) break;
            const uint8_t t = e[0];
            if ((t & 0x7F) != 0x05) continue;          // 0x85 Datei, 0x05 gelöschte Datei
            const bool del = (t & 0x80) == 0;
            const int sec = e[1];
            if (sec < 2 || i + size_t(sec + 1) * 32 > d.size()) continue;
            const uint8_t *st = e + 32;
            if ((st[0] & 0x7F) != 0x40) continue;      // Stream Extension
            FsEntry fe;
            fe.dir = (rd16(e + 4) & 0x10) != 0; fe.deleted = del;
            fe.noFatChain = (st[1] & 0x02) != 0;
            int nameLen = st[3];
            fe.firstCluster = rd32(st + 20); fe.size = rd64(st + 24);
            std::vector<uint16_t> name;
            for (int k = 2; k <= sec && int(name.size()) < nameLen; ++k) {
                const uint8_t *ne = e + size_t(k) * 32;
                if ((ne[0] & 0x7F) != 0x41) break;
                for (int c = 0; c < 15 && int(name.size()) < nameLen; ++c) name.push_back(rd16(ne + 2 + c * 2));
            }
            i += size_t(sec) * 32;
            fe.path = base + deletedName(safeName(utf16ToUtf8(name)), del);
            if (!validCluster(fe.firstCluster) && fe.size) fe.damaged = true;
            out.push_back(fe);
            if (fe.dir && validCluster(fe.firstCluster) && depth < 32 && visited_.insert(fe.firstCluster).second)
                listExfat(fe.firstCluster, fe.noFatChain || del, fe.path + "/", out, maxE, cancel, depth + 1, progress, fe.size);
        }
    }

    BlockReader *r_ = nullptr;
    VolumeInfo v_;
    uint64_t fatStart_ = 0, fatBytes_ = 0, rootDirStart_ = 0, dataStart_ = 0;
    uint32_t clusters_ = 0;
    std::vector<uint8_t> fat_;
    bool fatDamaged_ = false;
    std::set<uint32_t> visited_;
    std::vector<std::string> notes_;
};

struct FileRescueResult {
    size_t files = 0, dirs = 0, deleted = 0, damaged = 0, failed = 0;
    uint64_t bytes = 0, badBytes = 0;
    std::vector<std::string> problems;
};

// Alle Dateien eines Volumes in einen Zielordner kopieren.
inline FileRescueResult rescueFiles(BlockReader &r, const VolumeInfo &v, const std::string &targetDir, bool includeDeleted,
                                    ProgressFn progress, CancelToken *cancel, LogFn log) {
    FileRescueResult res;
    FatVolumeReader fr; std::string err;
    if (!fr.open(r, v, err)) { res.problems.push_back(err); return res; }
    for (auto &n : fr.notes()) if (log) log(LogKind::Warnung, n);
    auto list = fr.list(500000, cancel, progress);
    uint64_t total = 0;
    for (auto &e : list) if (!e.dir && (includeDeleted || !e.deleted)) total += e.size;
    if (log) log(LogKind::Messung, fmt("%zu Einträge gefunden, %s zu retten", list.size(), humanSize(total).c_str()));
    const std::filesystem::path root = u8path(targetDir);
    uint64_t done = 0;
    for (auto &e : list) {
        if (cancel && cancel->cancelled()) break;
        if (e.deleted && !includeDeleted) continue;
        std::error_code ec;
        auto p = root / u8path(e.path);
        if (e.dir) { std::filesystem::create_directories(p, ec); ++res.dirs; continue; }
        std::filesystem::create_directories(p.parent_path(), ec);
        OutFile f; std::string fe;
        if (!f.open(pathString(p), false, fe)) { ++res.failed; res.problems.push_back(fe); continue; }
        uint64_t bad = 0;
        if (!fr.extract(e, f, bad, cancel)) { ++res.failed; res.problems.push_back("Schreiben fehlgeschlagen: " + e.path); }
        f.close();
        ++res.files; if (e.deleted) ++res.deleted;
        if (bad || e.damaged) { ++res.damaged; res.badBytes += bad; }
        res.bytes += e.size; done += e.size;
        if (progress && total) progress(double(done) * 100.0 / double(total), e.path);
    }
    return res;
}

} // namespace usbr
