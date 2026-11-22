#pragma once
// Bequemer Lesezugriff mit beliebigen Versätzen über ausgerichtete Sektorblöcke.
#include "disk_io.h"

namespace usbr {

class BlockReader {
public:
    explicit BlockReader(RawDisk &d, uint32_t timeoutMs = 4000) : d_(d), timeout_(timeoutMs) {}
    RawDisk &disk() { return d_; }
    uint64_t size() const { return d_.size(); }
    uint32_t sector() const { return d_.sectorSize(); }

    // Liest beliebige Bytes; nicht lesbare Bereiche werden mit Nullen gefüllt und als Fehler gemeldet.
    bool read(uint64_t off, size_t len, std::vector<uint8_t> &out, std::string *err = nullptr) {
        out.assign(len, 0);
        if (len == 0) return true;
        const uint64_t ss = d_.sectorSize();
        uint64_t start = off / ss * ss;
        uint64_t end = (off + len + ss - 1) / ss * ss;
        if (d_.size() && end > d_.size()) end = (d_.size() + ss - 1) / ss * ss;
        if (end <= start) return false;
        size_t total = size_t(end - start);
        if (buf_.size() < total) buf_.resize(std::max<size_t>(total, 1 << 20));
        IoResult r = d_.read(start, buf_.data(), total, timeout_);
        if (!r.ok) {
            if (err) *err = r.message;
            ++errors_;
            return false;
        }
        size_t avail = std::min<uint64_t>(len, end - off);
        std::memcpy(out.data(), buf_.data() + (off - start), avail);
        return true;
    }
    bool readSectors(uint64_t lba, uint32_t count, std::vector<uint8_t> &out, std::string *err = nullptr) {
        return read(lba * d_.sectorSize(), size_t(count) * d_.sectorSize(), out, err);
    }
    int errors() const { return errors_; }

private:
    RawDisk &d_;
    uint32_t timeout_;
    AlignedBuffer buf_;
    int errors_ = 0;
};

// Inhaltsmuster eines Blocks: hilft zu erkennen, ob der Controller nur noch 00/FF oder Wiederholungen liefert.
enum class Pattern { Data, Zeros, Ones, Repeating };
inline Pattern classify(const uint8_t *p, size_t n) {
    if (n == 0) return Pattern::Zeros;
    bool z = true, o = true;
    for (size_t i = 0; i < n && (z || o); ++i) { if (p[i] != 0) z = false; if (p[i] != 0xFF) o = false; }
    if (z) return Pattern::Zeros;
    if (o) return Pattern::Ones;
    if (n >= 1024) {
        bool rep = true;
        for (size_t i = 512; i < n && rep; ++i) if (p[i] != p[i % 512]) rep = false;
        if (rep) return Pattern::Repeating;
    }
    return Pattern::Data;
}
inline const char *patternName(Pattern p) {
    switch (p) { case Pattern::Zeros: return "nur 00"; case Pattern::Ones: return "nur FF"; case Pattern::Repeating: return "Wiederholungsmuster"; default: return "Daten"; }
}

} // namespace usbr
