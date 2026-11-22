#pragma once
// Signatursuche (Carving): findet Dateien anhand ihres Inhalts – unabhängig vom Dateisystem.
// Funktioniert auch bei RAW, NTFS, zerstörtem Dateisystem oder neu formatierten Sticks.
// Annahme: Dateien liegen zusammenhängend (bei Sticks meistens der Fall).
#include "fs_reader.h"
#include <map>

namespace usbr {

struct CarveStats {
    std::map<std::string, int> perType;
    int files = 0;
    uint64_t bytes = 0;
    bool cancelled = false;
    std::vector<std::string> notes;
};

class FileCarver {
public:
    explicit FileCarver(BlockReader &r) : r_(r) {}

    CarveStats run(const std::string &targetDir, ProgressFn progress, CancelToken *cancel, LogFn log, uint64_t from = 0, uint64_t to = 0) {
        CarveStats st;
        const uint64_t size = to ? to : r_.size();
        const size_t chunk = 4 << 20;
        std::vector<uint8_t> buf;
        std::filesystem::path root = u8path(targetDir);
        for (uint64_t off = from; off < size;) {
            if (cancel && cancel->cancelled()) { st.cancelled = true; break; }
            size_t len = size_t(std::min<uint64_t>(chunk, size - off));
            r_.read(off, len, buf);       // unlesbare Bereiche sind Nullen
            uint64_t next = off + len;
            for (size_t i = 0; i + 64 <= len; i += 512) {
                std::string ext; uint64_t flen = 0;
                if (!identify(buf.data() + i, off + i, ext, flen) || flen < 64) continue;
                std::error_code ec;
                auto dir = root / u8path(ext);
                std::filesystem::create_directories(dir, ec);
                std::string name = fmt("datei_%010llX.", (unsigned long long)(off + i)) + ext;
                if (copyOut(off + i, flen, pathString(dir / u8path(name)))) {
                    ++st.files; st.bytes += flen; ++st.perType[ext];
                    if (log && st.files <= 30) log(LogKind::Messung, fmt("Gefunden: %s (%s) bei %s", ext.c_str(), humanSize(flen).c_str(), humanSize(off + i).c_str()));
                }
                uint64_t endAt = (off + i + flen + 511) / 512 * 512;
                if (endAt >= off + len) { next = endAt; break; }   // Datei reicht über den Block hinaus
                i = size_t(endAt - off) - 512;
            }
            off = std::max(next, off + 512);
            if (progress) progress(double(off - from) * 100.0 / double(size - from), fmt("%d Dateien gefunden · %s", st.files, humanSize(st.bytes).c_str()));
        }
        return st;
    }

private:
    bool rd(uint64_t off, size_t n, std::vector<uint8_t> &b) { return r_.read(off, n, b); }

    bool copyOut(uint64_t off, uint64_t len, const std::string &path) {
        OutFile f; std::string e;
        if (!f.open(path, false, e)) return false;
        std::vector<uint8_t> b;
        for (uint64_t p = 0; p < len; p += 4 << 20) {
            size_t n = size_t(std::min<uint64_t>(4 << 20, len - p));
            r_.read(off + p, n, b);
            if (!f.append(b.data(), n)) return false;
        }
        return true;
    }

    // Suche nach einer Bytefolge ab off, höchstens max Bytes weit. Liefert Versatz relativ zu off oder -1.
    int64_t find(uint64_t off, uint64_t max, const char *pat, size_t pn, bool last = false) {
        std::vector<uint8_t> b; int64_t found = -1;
        const size_t step = 1 << 20;
        for (uint64_t p = 0; p < max; p += step) {
            size_t n = size_t(std::min<uint64_t>(step + pn, max - p));
            if (off + p >= r_.size()) break;
            rd(off + p, n, b);
            for (size_t i = 0; i + pn <= n; ++i)
                if (b[i] == uint8_t(pat[0]) && std::memcmp(&b[i], pat, pn) == 0) { found = int64_t(p + i); if (!last) return found; }
        }
        return found;
    }

    uint64_t jpegLength(uint64_t off) {
        std::vector<uint8_t> b; uint64_t p = 2; const uint64_t max = 64ull << 20;
        while (p < 1 << 20) {                          // Segmente bis Start-of-Scan
            rd(off + p, 4, b);
            if (b[0] != 0xFF) return 0;
            uint8_t m = b[1];
            if (m == 0xD9) return p + 2;
            if (m == 0xDA) { p += 2 + ((b[2] << 8) | b[3]); break; }
            if (m >= 0xD0 && m <= 0xD7) { p += 2; continue; }
            p += 2 + ((b[2] << 8) | b[3]);
        }
        for (uint64_t q = p; q < max; q += 1 << 20) {  // Bilddaten bis FFD9
            rd(off + q, (1 << 20) + 1, b);
            for (size_t i = 0; i < (1u << 20); ++i)
                if (b[i] == 0xFF && b[i + 1] == 0xD9) return q + i + 2;
        }
        return 0;
    }
    uint64_t pngLength(uint64_t off) {
        std::vector<uint8_t> b; uint64_t p = 8;
        for (int n = 0; n < 100000; ++n) {
            rd(off + p, 8, b);
            uint32_t len = be32(&b[0]);
            if (len > (64u << 20)) return 0;
            for (int k = 4; k < 8; ++k) if (!std::isalpha(b[size_t(k)])) return 0;
            p += 12ull + len;
            if (std::memcmp(&b[4], "IEND", 4) == 0) return p;
        }
        return 0;
    }
    uint64_t mp4Length(uint64_t off) {
        static const char *types[] = {"ftyp", "moov", "mdat", "free", "skip", "wide", "uuid", "meta", "pdin", "moof", "mfra", "styp", "sidx", "udta", "pnot", "PICT", "junk"};
        std::vector<uint8_t> b; uint64_t p = 0; bool mdat = false, moov = false;
        for (int n = 0; n < 10000; ++n) {
            if (off + p + 16 > r_.size()) break;
            rd(off + p, 16, b);
            uint64_t sz = be32(&b[0]);
            bool known = false;
            for (auto *t : types) if (std::memcmp(&b[4], t, 4) == 0) known = true;
            if (!known) break;
            if (std::memcmp(&b[4], "mdat", 4) == 0) mdat = true;
            if (std::memcmp(&b[4], "moov", 4) == 0) moov = true;
            if (sz == 1) sz = be64(&b[8]);
            if (sz == 0) return 0;
            if (sz < 8 || sz > (64ull << 30)) break;
            p += sz;
        }
        return (mdat || moov) ? p : 0;
    }
    static int ebmlVint(const uint8_t *p, uint64_t &v) {
        int len = 1; uint8_t m = 0x80;
        while (len <= 8 && !(p[0] & m)) { ++len; m >>= 1; }
        if (len > 8) return 0;
        v = p[0] & (m - 1);
        for (int i = 1; i < len; ++i) v = (v << 8) | p[i];
        return len;
    }
    uint64_t mkvLength(uint64_t off) {
        std::vector<uint8_t> b; rd(off, 64, b);
        uint64_t hs = 0; int l = ebmlVint(&b[4], hs); if (!l) return 0;
        uint64_t p = 4 + l + hs;                         // hinter dem EBML-Kopf
        rd(off + p, 16, b);
        if (!(b[0] == 0x18 && b[1] == 0x53 && b[2] == 0x80 && b[3] == 0x67)) return 0;
        uint64_t ss = 0; int l2 = ebmlVint(&b[4], ss); if (!l2) return 0;
        if (ss >= (1ull << (7 * l2)) - 1) return 0;      // unbekannte Größe (Live-Aufnahme)
        return p + 4 + l2 + ss;
    }
    uint64_t mp3Length(uint64_t off) {
        std::vector<uint8_t> b; rd(off, 10, b);
        uint64_t p = 10 + ((uint64_t(b[6] & 0x7F) << 21) | (uint64_t(b[7] & 0x7F) << 14) | (uint64_t(b[8] & 0x7F) << 7) | (b[9] & 0x7F));
        static const int br1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
        static const int br2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
        static const int sr[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
        int frames = 0;
        for (int n = 0; n < 2000000; ++n) {
            rd(off + p, 4, b);
            if (b[0] != 0xFF || (b[1] & 0xE0) != 0xE0) break;
            int ver = (b[1] >> 3) & 3, layer = (b[1] >> 1) & 3, bri = b[2] >> 4, sri = (b[2] >> 2) & 3, pad = (b[2] >> 1) & 1;
            if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) break;
            int vi = ver == 3 ? 0 : ver == 2 ? 1 : 2;
            int rate = (vi == 0 ? br1[bri] : br2[bri]) * 1000, s = sr[vi][sri];
            uint64_t fl = vi == 0 ? uint64_t(144 * rate / s + pad) : uint64_t(72 * rate / s + pad);
            if (fl < 24) break;
            p += fl; ++frames;
        }
        return frames > 10 ? p : 0;
    }
    uint64_t zipLength(uint64_t off, std::string &ext) {
        std::vector<uint8_t> b; rd(off, 4096, b);
        std::string head(b.begin(), b.end());
        if (head.find("word/") != std::string::npos) ext = "docx";
        else if (head.find("xl/") != std::string::npos) ext = "xlsx";
        else if (head.find("ppt/") != std::string::npos) ext = "pptx";
        else if (head.find("mimetypeapplication/vnd.oasis") != std::string::npos) ext = "odt";
        else if (head.find("AndroidManifest") != std::string::npos) ext = "apk";
        int64_t e = find(off, 2ull << 30, "PK\x05\x06", 4);
        if (e < 0) return 0;
        rd(off + uint64_t(e), 22, b);
        return uint64_t(e) + 22 + rd16(&b[20]);
    }

    bool identify(const uint8_t *s, uint64_t off, std::string &ext, uint64_t &len) {
        len = 0;
        if (s[0] == 0xFF && s[1] == 0xD8 && s[2] == 0xFF && (s[3] >= 0xE0 || s[3] == 0xDB)) { ext = "jpg"; len = jpegLength(off); }
        else if (std::memcmp(s, "\x89PNG\r\n\x1a\n", 8) == 0) { ext = "png"; len = pngLength(off); }
        else if (std::memcmp(s, "GIF87a", 6) == 0 || std::memcmp(s, "GIF89a", 6) == 0) { ext = "gif"; int64_t e = find(off, 32ull << 20, "\x00\x3B", 2); len = e > 0 ? uint64_t(e) + 2 : 0; }
        else if (std::memcmp(s, "%PDF-", 5) == 0) {
            ext = "pdf";
            int64_t e = find(off, 256ull << 20, "%%EOF", 5);
            // Mehrfache Speicherstände: weitere %%EOF kurz dahinter einbeziehen
            while (e > 0) { int64_t e2 = find(off + uint64_t(e) + 5, 1 << 20, "%%EOF", 5); if (e2 < 0) break; e += 5 + e2; }
            len = e > 0 ? uint64_t(e) + 6 : 0;
        }
        else if (std::memcmp(s, "PK\x03\x04", 4) == 0) { ext = "zip"; len = zipLength(off, ext); }
        else if (std::memcmp(s + 4, "ftyp", 4) == 0 && be32(s) >= 8 && be32(s) <= 256) {
            std::string brand(reinterpret_cast<const char *>(s + 8), 4);
            ext = brand == "qt  " ? "mov" : (brand.rfind("hei", 0) == 0 || brand == "mif1") ? "heic" : brand.rfind("3g", 0) == 0 ? "3gp" : brand.rfind("M4A", 0) == 0 ? "m4a" : "mp4";
            len = mp4Length(off);
        }
        else if (s[0] == 0x1A && s[1] == 0x45 && s[2] == 0xDF && s[3] == 0xA3) { ext = "mkv"; len = mkvLength(off); }
        else if (std::memcmp(s, "RIFF", 4) == 0) {
            std::string t(reinterpret_cast<const char *>(s + 8), 4);
            ext = t == "AVI " ? "avi" : t == "WAVE" ? "wav" : t == "WEBP" ? "webp" : "riff";
            len = 8ull + rd32(s + 4);
        }
        else if (s[0] == 'B' && s[1] == 'M' && rd32(s + 6) == 0 && rd32(s + 14) >= 12 && rd32(s + 14) <= 124) { ext = "bmp"; len = rd32(s + 2); }
        else if (std::memcmp(s, "ID3", 3) == 0 && s[3] >= 2 && s[3] <= 4) { ext = "mp3"; len = mp3Length(off); }
        else if (std::memcmp(s, "7z\xBC\xAF\x27\x1C", 6) == 0) { ext = "7z"; len = 32 + rd64(s + 12) + rd64(s + 20); }
        else if (std::memcmp(s, "Rar!\x1A\x07", 6) == 0) { ext = "rar"; len = 0; }
        else if (std::memcmp(s, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0) { ext = "doc"; len = 0; }  // altes Office
        else if (std::memcmp(s, "II*\0", 4) == 0 || std::memcmp(s, "MM\0*", 4) == 0) { ext = "tif"; len = 0; }
        else return false;
        if (len == 0) len = endByNextHeader(off);
        if (len > (8ull << 30)) return false;
        return len > 0;
    }

    // Für Formate ohne Längenangabe: bis zum nächsten erkannten Dateianfang, höchstens 64 MB
    uint64_t endByNextHeader(uint64_t off) {
        std::vector<uint8_t> b;
        const uint64_t max = std::min<uint64_t>(64ull << 20, r_.size() - off);
        for (uint64_t p = 512; p < max; p += 1 << 20) {
            size_t n = size_t(std::min<uint64_t>(1 << 20, max - p));
            rd(off + p, n, b);
            for (size_t i = 0; i + 16 <= n; i += 512) {
                const uint8_t *s = b.data() + i;
                if ((s[0] == 0xFF && s[1] == 0xD8 && s[2] == 0xFF) || std::memcmp(s, "\x89PNG", 4) == 0 || std::memcmp(s, "%PDF", 4) == 0 ||
                    std::memcmp(s, "PK\x03\x04", 4) == 0 || std::memcmp(s + 4, "ftyp", 4) == 0 || std::memcmp(s, "RIFF", 4) == 0 ||
                    classify(s, 512) != Pattern::Data)
                    return p + i;
            }
        }
        return max;
    }

    BlockReader &r_;
};

} // namespace usbr
