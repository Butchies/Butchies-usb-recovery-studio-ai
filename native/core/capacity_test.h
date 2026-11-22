#pragma once
// Kapazitätstest (Fake-Erkennung): schreibt eindeutige Muster an verteilte Positionen, liest zurück
// und bestimmt die echte Größe. Originaldaten werden gesichert und zurückgeschrieben.
// ACHTUNG: Bei Fake-Sticks landen Schreibzugriffe oberhalb der echten Größe auf echten Daten.
// Deshalb ist der Test nur mit vorhandenem Rettungsabbild oder bestätigt leerem Stick erlaubt (prüft der Agent).
#include "repair_actions.h"

namespace usbr {

struct CapacityResult {
    bool done = false;
    bool fake = false;
    uint64_t claimed = 0;
    uint64_t real = 0;          // höchste bestätigte Grenze
    bool wrapDetected = false;  // spätere Schreibvorgänge haben frühere überschrieben
    double writeMBs = 0;
    std::vector<std::string> notes;
    std::string error;
};

class CapacityTester {
public:
    CapacityResult run(RawDisk &d, UndoJournal &u, ProgressFn progress, CancelToken *cancel, LogFn log) {
        CapacityResult res; res.claimed = d.size();
        if (!d.writable()) { res.error = "Datenträger ist nicht schreibend geöffnet"; return res; }
        const uint32_t ss = d.sectorSize();
        const uint32_t blk = 64 * 1024;
        seed_ = uint64_t(nowMs()) * 0x9E3779B97F4A7C15ull;
        // Positionen: Zweierpotenzen (typische Fake-Grenzen) + 48 gleichmäßig verteilte
        std::vector<uint64_t> pos;
        for (uint64_t p = 16ull << 20; p < res.claimed; p *= 2) { pos.push_back(p - blk); pos.push_back(p); }
        for (int i = 1; i <= 48; ++i) pos.push_back(uint64_t(double(res.claimed - blk) * i / 49.0));
        pos.push_back(res.claimed - blk);
        for (auto &p : pos) p = p / ss * ss;
        std::sort(pos.begin(), pos.end()); pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
        pos.erase(std::remove_if(pos.begin(), pos.end(), [&](uint64_t p) { return p < (1ull << 20) || p + blk > res.claimed; }), pos.end());

        std::string e;
        AlignedBuffer pat(blk), back(blk);
        uint64_t t0 = nowMs(), written = 0;
        for (size_t i = 0; i < pos.size(); ++i) {
            if (cancel && cancel->cancelled()) { res.error = "Abgebrochen"; restore(d, u, res); return res; }
            if (!u.save(d, pos[i], blk, e)) { res.notes.push_back("Position " + humanSize(pos[i]) + ": " + e); continue; }
            saved_.push_back(pos[i]);
            fill(pat.data(), blk, pos[i]);
            IoResult w = d.write(pos[i], pat.data(), blk, 10000);
            if (!w.ok) res.notes.push_back("Schreibfehler bei " + humanSize(pos[i]) + ": " + w.message);
            else written += blk;
            if (progress) progress(double(i + 1) * 50.0 / double(pos.size()), "Schreibe Prüfmuster " + humanSize(pos[i]));
        }
        d.flush();
        res.writeMBs = nowMs() > t0 ? double(written) / 1e3 / double(nowMs() - t0) : 0;
        // Zurücklesen
        uint64_t firstBad = 0, lastGood = 0;
        for (size_t i = 0; i < saved_.size(); ++i) {
            uint64_t p = saved_[i];
            fill(pat.data(), blk, p);
            IoResult r = d.read(p, back.data(), blk, 5000);
            bool ok = r.ok && std::memcmp(pat.data(), back.data(), blk) == 0;
            if (ok) { if (!firstBad) lastGood = p + blk; else res.wrapDetected = true; }
            else {
                if (!firstBad) firstBad = p;
                // Zeigt der Block das Muster einer anderen Position? Dann wurde umgeleitet (Wrap-around).
                if (r.ok && rd64(back.data()) != rd64(pat.data())) {
                    uint64_t other = rd64(back.data() + 8);
                    if (other != p && other < res.claimed) res.wrapDetected = true;
                }
            }
            if (progress) progress(50.0 + double(i + 1) * 45.0 / double(saved_.size()), "Prüfe " + humanSize(p));
        }
        restore(d, u, res);
        res.done = true;
        res.fake = firstBad != 0;
        res.real = res.fake ? lastGood : res.claimed;
        if (res.fake && log) log(LogKind::Messung, fmt("Prüfmuster ab %s nicht mehr lesbar – echte Größe höchstens %s statt %s",
                                                       humanSize(firstBad).c_str(), humanSize(res.real).c_str(), humanSize(res.claimed).c_str()));
        if (progress) progress(100, res.fake ? "Fake-Kapazität erkannt" : "Kapazität bestätigt");
        return res;
    }

private:
    void fill(uint8_t *p, uint32_t n, uint64_t pos) {
        uint64_t x = seed_ ^ pos;
        wr64(p, seed_); wr64(p + 8, pos);
        for (uint32_t i = 16; i + 8 <= n; i += 8) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; wr64(p + i, x); }
    }
    void restore(RawDisk &d, UndoJournal &u, CapacityResult &res) {
        u.close();
        std::string e;
        if (!UndoJournal::restore(d, u.path(), e)) res.notes.push_back("Zurückschreiben der Originaldaten fehlgeschlagen: " + e);
        else res.notes.push_back("Originaldaten an den Prüfpositionen zurückgeschrieben");
    }
    uint64_t seed_ = 0;
    std::vector<uint64_t> saved_;
};

} // namespace usbr
