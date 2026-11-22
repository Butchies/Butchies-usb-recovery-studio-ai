#pragma once
// Rettungsabbild in mehreren Durchgängen (Prinzip wie GNU ddrescue):
//  1. schnell vorwärts in großen Blöcken, bei Fehlern weiterspringen (schont sterbende Sticks)
//  2. übersprungene Bereiche in kleineren Blöcken
//  3. fehlerhafte Blöcke sektorweise auskratzen
//  4. defekte Sektoren erneut versuchen
// Mapdatei im ddrescue-Format – der Vorgang lässt sich jederzeit fortsetzen.
#include "disk_io.h"
#include <sstream>

namespace usbr {

struct Region { uint64_t pos, size; char st; };   // '?' offen, '*' Fehler grob, '/' unvollständig, '-' defekt, '+' gelesen

class RegionMap {
public:
    void init(uint64_t total) { total_ = total; r_.clear(); if (total) r_.push_back({0, total, '?'}); }
    uint64_t total() const { return total_; }
    const std::vector<Region> &regions() const { return r_; }
    void set(uint64_t pos, uint64_t size, char st) {
        if (!size || pos >= total_) return;
        size = std::min(size, total_ - pos);
        std::vector<Region> out; out.reserve(r_.size() + 2);
        const uint64_t end = pos + size;
        bool placed = false;
        for (const auto &x : r_) {
            uint64_t xe = x.pos + x.size;
            if (xe <= pos || x.pos >= end) { if (x.pos >= end && !placed) { out.push_back({pos, size, st}); placed = true; } out.push_back(x); continue; }
            if (x.pos < pos) out.push_back({x.pos, pos - x.pos, x.st});
            if (!placed) { out.push_back({pos, size, st}); placed = true; }
            if (xe > end) out.push_back({end, xe - end, x.st});
        }
        if (!placed) out.push_back({pos, size, st});
        r_.clear();
        for (auto &x : out) { if (!r_.empty() && r_.back().st == x.st && r_.back().pos + r_.back().size == x.pos) r_.back().size += x.size; else r_.push_back(x); }
    }
    uint64_t count(char st) const { uint64_t n = 0; for (auto &x : r_) if (x.st == st) n += x.size; return n; }
    std::vector<Region> with(char st) const { std::vector<Region> v; for (auto &x : r_) if (x.st == st) v.push_back(x); return v; }
    std::string serialize(uint64_t curPos, int pass) const {
        std::ostringstream o;
        o << "# Rescue Logfile. Created by Butchies USB Recovery Studio AI\n# current_pos  current_status  current_pass\n";
        o << fmt("0x%08llX     ?               %d\n", (unsigned long long)curPos, pass);
        o << "#      pos        size  status\n";
        for (auto &x : r_) o << fmt("0x%08llX  0x%08llX  %c\n", (unsigned long long)x.pos, (unsigned long long)x.size, x.st);
        return o.str();
    }
    bool parse(const std::string &text, uint64_t total) {
        init(total);
        std::istringstream in(text); std::string line; bool header = false;
        std::vector<Region> v;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (!header) { header = true; continue; } // Statuszeile
            unsigned long long p = 0, s = 0; char st = 0;
            if (std::sscanf(line.c_str(), "%llx %llx %c", &p, &s, &st) == 3) v.push_back({p, s, st});
        }
        if (v.empty()) return false;
        for (auto &x : v) set(x.pos, x.size, x.st);
        return true;
    }
private:
    uint64_t total_ = 0;
    std::vector<Region> r_;
};

struct ImagingStats {
    uint64_t total = 0, rescued = 0, bad = 0, pending = 0;
    uint32_t readErrors = 0, timeouts = 0;
    double mbPerSec = 0;
    bool finished = false, cancelled = false;
    std::string error;
    int pass = 0;
};

class RescueImager {
public:
    struct Options {
        size_t bigBlock = 1 << 20;     // 1 MiB
        size_t smallBlock = 64 << 10;  // 64 KiB
        uint32_t timeoutMs = 3000;
        int retries = 2;
        uint64_t maxSkip = 64ull << 20;
    };

    ImagingStats run(RawDisk &src, const std::string &imagePath, const std::string &mapPath, const Options &opt,
                     ProgressFn progress, CancelToken *cancel, LogFn log) {
        ImagingStats st; st.total = src.size();
        if (!st.total) { st.error = "Quelle meldet 0 Byte – kein Abbild möglich"; return st; }
        const uint32_t ss = src.sectorSize();
        std::string old;
        if (readTextFile(mapPath, old) && map_.parse(old, st.total)) {
            if (log) log(LogKind::Info, fmt("Mapdatei gefunden – setze fort (%s bereits gelesen)", humanSize(map_.count('+')).c_str()));
        } else map_.init(st.total);
        OutFile img; std::string err;
        if (!img.open(imagePath, true, err)) { st.error = err; return st; }
        AlignedBuffer buf(opt.bigBlock);
        uint64_t lastSave = nowMs(), t0 = nowMs(), bytes0 = map_.count('+');

        auto report = [&](const char *what, uint64_t pos) {
            st.rescued = map_.count('+'); st.bad = map_.count('-');
            st.pending = st.total - st.rescued - st.bad;
            double secs = double(nowMs() - t0) / 1000.0;
            st.mbPerSec = secs > 0.5 ? double(st.rescued - bytes0) / 1e6 / secs : 0;
            if (progress) progress(double(st.rescued + st.bad) * 100.0 / double(st.total),
                                   fmt("%s · %s gerettet · %s defekt · %.1f MB/s", what, humanSize(st.rescued).c_str(), humanSize(st.bad).c_str(), st.mbPerSec));
            if (nowMs() - lastSave > 10000) { writeTextFile(mapPath, map_.serialize(pos, st.pass)); lastSave = nowMs(); }
        };
        auto readInto = [&](uint64_t pos, size_t len) -> bool {
            IoResult r = src.read(pos, buf.data(), len, opt.timeoutMs);
            if (r.ok) { img.writeAt(pos, buf.data(), len); map_.set(pos, len, '+'); return true; }
            if (r.timeout) ++st.timeouts; else ++st.readErrors;
            return false;
        };
        auto align = [&](uint64_t v) { return v / ss * ss; };

        // Durchgang 1: vorwärts, große Blöcke, bei Fehlern springen
        st.pass = 1;
        uint64_t skip = opt.bigBlock;
        for (const auto &reg : map_.with('?')) {
            uint64_t pos = reg.pos, end = reg.pos + reg.size;
            while (pos < end) {
                if (cancel && cancel->cancelled()) { st.cancelled = true; goto done; }
                size_t len = size_t(std::min<uint64_t>(opt.bigBlock, end - pos));
                len = size_t(std::max<uint64_t>(align(len), ss));
                if (pos + len > st.total) len = size_t(st.total - pos);
                if (readInto(pos, len)) { skip = opt.bigBlock; pos += len; }
                else {
                    map_.set(pos, len, '*');
                    if (log && st.readErrors + st.timeouts <= 20) log(LogKind::Messung, fmt("Lesefehler bei %s – springe %s weiter", humanSize(pos).c_str(), humanSize(skip).c_str()));
                    pos += len + skip;          // übersprungener Bereich bleibt '?'
                    skip = std::min<uint64_t>(skip * 2, opt.maxSkip);
                }
                report("Durchgang 1/4 (schnell)", pos);
            }
        }
        // Durchgang 2: übersprungene Bereiche in kleineren Blöcken
        st.pass = 2;
        for (const auto &reg : map_.with('?')) {
            for (uint64_t pos = reg.pos; pos < reg.pos + reg.size;) {
                if (cancel && cancel->cancelled()) { st.cancelled = true; goto done; }
                size_t len = size_t(std::min<uint64_t>(opt.smallBlock, reg.pos + reg.size - pos));
                if (!readInto(pos, len)) map_.set(pos, len, '*');
                pos += len;
                report("Durchgang 2/4 (Lücken)", pos);
            }
        }
        // Durchgang 3: fehlerhafte Blöcke sektorweise
        st.pass = 3;
        for (const auto &reg : map_.with('*')) {
            for (uint64_t pos = reg.pos; pos < reg.pos + reg.size; pos += ss) {
                if (cancel && cancel->cancelled()) { st.cancelled = true; goto done; }
                if (!readInto(pos, ss)) {
                    map_.set(pos, ss, '-');
                    std::vector<uint8_t> zero(ss, 0); img.writeAt(pos, zero.data(), ss);
                }
                report("Durchgang 3/4 (sektorweise)", pos);
            }
        }
        // Durchgang 4: defekte Sektoren erneut versuchen
        st.pass = 4;
        for (int t = 0; t < opt.retries; ++t) {
            for (const auto &reg : map_.with('-')) {
                for (uint64_t pos = reg.pos; pos < reg.pos + reg.size; pos += ss) {
                    if (cancel && cancel->cancelled()) { st.cancelled = true; goto done; }
                    readInto(pos, ss);
                    report(fmt("Durchgang 4/4 (Wiederholung %d)", t + 1).c_str(), pos);
                }
            }
        }
        st.finished = true;
    done:
        // Abbild auf volle Größe bringen
        { uint8_t z = 0; std::vector<uint8_t> last(1, 0); if (map_.count('+') < st.total) img.writeAt(st.total - 1, &z, 1); }
        img.close();
        writeTextFile(mapPath, map_.serialize(0, st.pass));
        st.rescued = map_.count('+'); st.bad = map_.count('-'); st.pending = st.total - st.rescued - st.bad;
        return st;
    }
    const RegionMap &map() const { return map_; }

private:
    RegionMap map_;
};

} // namespace usbr
