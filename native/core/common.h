#pragma once
// Butchies USB Recovery Studio AI – gemeinsame Grundtypen des Kerns (ohne Qt).
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <malloc.h>
#endif

namespace usbr {

using ProgressFn = std::function<void(double percent, const std::string &text)>;
// Kategorien für den KI-Live-Monitor
enum class LogKind { Messung, Hypothese, Entscheidung, Aktion, Pruefung, Lernen, Warnung, Fehler, Info };
using LogFn = std::function<void(LogKind, const std::string &)>;

inline const char *logKindName(LogKind k) {
    switch (k) {
    case LogKind::Messung: return "Messung";
    case LogKind::Hypothese: return "Hypothese";
    case LogKind::Entscheidung: return "Entscheidung";
    case LogKind::Aktion: return "Aktion";
    case LogKind::Pruefung: return "Prüfung";
    case LogKind::Lernen: return "Lernen";
    case LogKind::Warnung: return "Warnung";
    case LogKind::Fehler: return "Fehler";
    default: return "Info";
    }
}

struct CancelToken {
    std::atomic<bool> flag{false};
    bool cancelled() const { return flag.load(); }
    void cancel() { flag.store(true); }
    void reset() { flag.store(false); }
};

// Sektorausgerichteter Puffer (nötig für ungepufferten Rohzugriff unter Windows).
class AlignedBuffer {
public:
    AlignedBuffer() = default;
    explicit AlignedBuffer(size_t n, size_t align = 4096) { resize(n, align); }
    ~AlignedBuffer() { release(); }
    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;
    AlignedBuffer(AlignedBuffer &&o) noexcept : p_(o.p_), n_(o.n_) { o.p_ = nullptr; o.n_ = 0; }
    void resize(size_t n, size_t align = 4096) {
        release();
        n = (n + align - 1) / align * align;
#ifdef _WIN32
        p_ = static_cast<uint8_t *>(_aligned_malloc(n, align));
#else
        void *q = nullptr;
        if (posix_memalign(&q, align, n) != 0) q = nullptr;
        p_ = static_cast<uint8_t *>(q);
#endif
        n_ = p_ ? n : 0;
        if (p_) std::memset(p_, 0, n_);
    }
    uint8_t *data() { return p_; }
    const uint8_t *data() const { return p_; }
    size_t size() const { return n_; }
private:
    void release() {
        if (!p_) return;
#ifdef _WIN32
        _aligned_free(p_);
#else
        free(p_);
#endif
        p_ = nullptr; n_ = 0;
    }
    uint8_t *p_ = nullptr;
    size_t n_ = 0;
};

inline uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline uint64_t rd64(const uint8_t *p) { return uint64_t(rd32(p)) | (uint64_t(rd32(p + 4)) << 32); }
inline uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint64_t be64(const uint8_t *p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
inline void wr16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
inline void wr64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }

inline std::string hex4(uint32_t v) { char b[8]; std::snprintf(b, sizeof b, "%04X", v & 0xFFFF); return b; }
inline std::string hex2(uint32_t v) { char b[4]; std::snprintf(b, sizeof b, "%02X", v & 0xFF); return b; }

inline std::string humanSize(uint64_t bytes) {
    const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = double(bytes); int i = 0;
    while (v >= 1000.0 && i < 4) { v /= 1000.0; ++i; }
    char b[32]; std::snprintf(b, sizeof b, i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
    return b;
}

inline std::string trim(std::string s) {
    auto ns = [](unsigned char c) { return !std::isspace(c) && c != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

#if defined(__GNUC__) || defined(__clang__)
inline std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
#endif
inline std::string fmt(const char *f, ...) {
    char b[1024];
    va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a);
    return b;
}

#ifdef _WIN32
inline std::wstring widen(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
inline std::string narrow(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::string winErrorText(DWORD e) {
    wchar_t *buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string s = buf ? trim(narrow(buf)) : std::string();
    if (buf) LocalFree(buf);
    return fmt("Windows-Fehler %lu: %s", (unsigned long)e, s.c_str());
}
#endif

inline uint64_t nowMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace usbr
