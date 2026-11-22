#pragma once
// Lese-Probe (schnell, Stichproben über den ganzen Stick) und vollständiger Oberflächentest (nur lesend).
#include "reader.h"

namespace usbr {

struct ProbeReport {
    bool opened = false;
    std::string openError;
    uint64_t size = 0;
    bool firstOk = false, lastOk = false;
    int samples = 0, ok = 0, errors = 0, timeouts = 0;
    uint32_t avgMs = 0, maxMs = 0;
    int zeros = 0, ones = 0, repeating = 0, data = 0;
    std::vector<std::string> errorTexts;
    double readMBs = 0;
};

inline ProbeReport quickProbe(RawDisk &d, int samples = 64, CancelToken *cancel = nullptr) {
    ProbeReport p; p.opened = d.isOpen(); p.size = d.size();
    if (!p.opened || !p.size) return p;
    const uint32_t ss = d.sectorSize();
    const size_t blk = 64 << 10;
    AlignedBuffer b(blk);
    uint64_t sumMs = 0, bytes = 0;
    auto one = [&](uint64_t off, size_t len) {
        off = off / ss * ss;
        if (off + len > p.size) len = size_t((p.size - off) / ss * ss);
        if (!len) return false;
        IoResult r = d.read(off, b.data(), len, 3000);
        ++p.samples; sumMs += r.ms; p.maxMs = std::max(p.maxMs, r.ms);
        if (r.ok) {
            ++p.ok; bytes += len;
            switch (classify(b.data(), len)) { case Pattern::Zeros: ++p.zeros; break; case Pattern::Ones: ++p.ones; break; case Pattern::Repeating: ++p.repeating; break; default: ++p.data; }
        } else {
            if (r.timeout) ++p.timeouts; else ++p.errors;
            if (p.errorTexts.size() < 5) p.errorTexts.push_back(humanSize(off) + ": " + r.message);
        }
        return r.ok;
    };
    p.firstOk = one(0, ss);
    p.lastOk = one(p.size - ss, ss);
    for (int i = 0; i < samples; ++i) {
        if (cancel && cancel->cancelled()) break;
        one(uint64_t(double(p.size - blk) * (double(i) + 0.5) / double(samples)), blk);
    }
    p.avgMs = p.samples ? uint32_t(sumMs / uint64_t(p.samples)) : 0;
    p.readMBs = sumMs ? double(bytes) / 1e3 / double(sumMs) : 0;
    return p;
}

struct ScanReport {
    uint64_t scanned = 0, badBytes = 0;
    uint32_t badBlocks = 0, slowBlocks = 0, timeouts = 0;
    std::vector<std::pair<uint64_t, uint64_t>> badRegions;  // Versatz, Länge
    std::vector<double> speedCurve;                        // MB/s in 100 Abschnitten
    double avgMBs = 0, minMBs = 0;
    bool cancelled = false;
};

inline ScanReport fullScan(RawDisk &d, ProgressFn progress, CancelToken *cancel, LogFn log) {
    ScanReport s;
    const uint64_t size = d.size();
    const uint32_t ss = d.sectorSize();
    if (!size) return s;
    const size_t blk = 1 << 20;
    AlignedBuffer b(blk);
    std::vector<uint64_t> bucketBytes(100, 0), bucketMs(100, 0);
    uint64_t t0 = nowMs();
    for (uint64_t off = 0; off < size;) {
        if (cancel && cancel->cancelled()) { s.cancelled = true; break; }
        size_t len = size_t(std::min<uint64_t>(blk, size - off));
        IoResult r = d.read(off, b.data(), len, 3000);
        int bucket = int(std::min<uint64_t>(99, off * 100 / size));
        bucketMs[size_t(bucket)] += r.ms; bucketBytes[size_t(bucket)] += len;
        if (r.ms > 1000) ++s.slowBlocks;
        if (!r.ok) {
            if (r.timeout) ++s.timeouts;
            // Fehler eingrenzen: in 64-KiB-Schritten nachlesen
            for (uint64_t o = off; o < off + len; o += 64 << 10) {
                size_t l = size_t(std::min<uint64_t>(64 << 10, off + len - o));
                l = std::max<size_t>(l / ss * ss, ss);
                IoResult rr = d.read(o, b.data(), l, 1500);
                if (!rr.ok) {
                    ++s.badBlocks; s.badBytes += l;
                    if (!s.badRegions.empty() && s.badRegions.back().first + s.badRegions.back().second == o) s.badRegions.back().second += l;
                    else s.badRegions.push_back({o, l});
                }
                if (cancel && cancel->cancelled()) break;
            }
            if (log && s.badRegions.size() <= 10) log(LogKind::Messung, "Defekter Bereich bei " + humanSize(off));
        }
        off += len; s.scanned = off;
        if (progress) progress(double(off) * 100.0 / double(size),
                               fmt("%s von %s · %u defekte Blöcke", humanSize(off).c_str(), humanSize(size).c_str(), s.badBlocks));
    }
    uint64_t ms = nowMs() - t0;
    s.avgMBs = ms ? double(s.scanned) / 1e3 / double(ms) : 0;
    s.minMBs = 1e9;
    for (int i = 0; i < 100; ++i) {
        double v = bucketMs[size_t(i)] ? double(bucketBytes[size_t(i)]) / 1e3 / double(bucketMs[size_t(i)]) : 0;
        s.speedCurve.push_back(v);
        if (bucketBytes[size_t(i)]) s.minMBs = std::min(s.minMBs, v);
    }
    if (s.minMBs > 1e8) s.minMBs = 0;
    return s;
}

} // namespace usbr
