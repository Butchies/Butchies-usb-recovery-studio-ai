#pragma once
// Rohzugriff auf Datenträger und Abbilddateien mit kurzen Zeitlimits.
// Windows: \\.\PhysicalDriveN über überlappende, ungepufferte E/A (Abbruch per CancelIoEx).
// Linux (Entwicklung/Abbilder): pread/pwrite.
#include "common.h"

#ifdef _WIN32
#  include <winioctl.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <sys/stat.h>
#  include <linux/fs.h>
#  include <cerrno>
#endif

namespace usbr {

struct IoResult {
    bool ok = false;
    bool timeout = false;
    uint32_t error = 0;     // Systemfehlercode
    uint32_t ms = 0;        // Dauer
    size_t bytes = 0;
    std::string message;
};

class RawDisk {
public:
    RawDisk() = default;
    ~RawDisk() { close(); }
    RawDisk(const RawDisk &) = delete;
    RawDisk &operator=(const RawDisk &) = delete;

    bool isOpen() const {
#ifdef _WIN32
        return h_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }
    const std::string &path() const { return path_; }
    uint64_t size() const { return size_; }
    uint32_t sectorSize() const { return sector_; }
    bool writable() const { return writable_; }
    bool isDevice() const { return device_; }
    int diskNumber() const { return diskNumber_; }

    bool open(const std::string &path, bool write, std::string &err) {
        close();
        path_ = path; writable_ = write;
#ifdef _WIN32
        device_ = path.rfind("\\\\.\\", 0) == 0;
        diskNumber_ = -1;
        auto pos = path.find("PhysicalDrive");
        if (pos != std::string::npos) diskNumber_ = std::atoi(path.c_str() + pos + 13);
        DWORD access = GENERIC_READ | (write ? GENERIC_WRITE : 0);
        DWORD flags = FILE_FLAG_OVERLAPPED | (device_ ? FILE_FLAG_NO_BUFFERING : 0) | (write ? FILE_FLAG_WRITE_THROUGH : 0);
        h_ = CreateFileW(widen(path).c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, flags, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) { err = winErrorText(GetLastError()); return false; }
        ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        sector_ = 512; size_ = 0;
        DWORD ret = 0;
        if (device_) {
            DISK_GEOMETRY_EX g{};
            if (DeviceIoControl(h_, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &ret, nullptr))
                sector_ = g.Geometry.BytesPerSector ? g.Geometry.BytesPerSector : 512;
            GET_LENGTH_INFORMATION li{};
            if (DeviceIoControl(h_, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &li, sizeof li, &ret, nullptr))
                size_ = uint64_t(li.Length.QuadPart);
            else lastOpenError_ = GetLastError();
        } else {
            LARGE_INTEGER s{}; if (GetFileSizeEx(h_, &s)) size_ = uint64_t(s.QuadPart);
        }
        return true;
#else
        device_ = path.rfind("/dev/", 0) == 0;
        fd_ = ::open(path.c_str(), (write ? O_RDWR : O_RDONLY) | O_CLOEXEC);
        if (fd_ < 0) { err = fmt("Öffnen fehlgeschlagen: %s", std::strerror(errno)); return false; }
        sector_ = 512; size_ = 0;
        struct stat st{};
        if (fstat(fd_, &st) == 0 && S_ISBLK(st.st_mode)) {
            uint64_t s = 0; if (ioctl(fd_, BLKGETSIZE64, &s) == 0) size_ = s;
            int ss = 0; if (ioctl(fd_, BLKSSZGET, &ss) == 0 && ss > 0) sector_ = uint32_t(ss);
        } else size_ = uint64_t(st.st_size);
        return true;
#endif
    }

    void close() {
#ifdef _WIN32
        for (HANDLE v : volumes_) { DWORD r; DeviceIoControl(v, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &r, nullptr); CloseHandle(v); }
        volumes_.clear();
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        if (ev_) CloseHandle(ev_);
        h_ = INVALID_HANDLE_VALUE; ev_ = nullptr;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }

    // Länge und Versatz müssen sektorausgerichtet sein, Puffer 4096-ausgerichtet.
    IoResult read(uint64_t offset, void *buf, size_t len, uint32_t timeoutMs = 4000) { return io(false, offset, buf, len, timeoutMs); }
    IoResult write(uint64_t offset, const void *buf, size_t len, uint32_t timeoutMs = 8000) {
        if (!writable_) { IoResult r; r.message = "Schreibsperre: Datenträger nur lesend geöffnet"; return r; }
        return io(true, offset, const_cast<void *>(buf), len, timeoutMs);
    }

    bool flush() {
#ifdef _WIN32
        return FlushFileBuffers(h_) != 0;
#else
        return fsync(fd_) == 0;
#endif
    }

    // Windows-Datenträgerattribute (Schreibschutz-/Offline-Flag, wie diskpart "attributes disk").
    bool readOnlyAttribute(bool &ro, std::string &err) {
#ifdef _WIN32
        GET_DISK_ATTRIBUTES a{}; DWORD r = 0;
        if (!DeviceIoControl(h_, IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &a, sizeof a, &r, nullptr)) { err = winErrorText(GetLastError()); return false; }
        ro = (a.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;
        return true;
#else
        int v = 0; if (ioctl(fd_, BLKROGET, &v) != 0) { err = std::strerror(errno); return false; }
        ro = v != 0; return true;
#endif
    }
    bool setReadOnlyAttribute(bool ro, std::string &err) {
#ifdef _WIN32
        SET_DISK_ATTRIBUTES a{}; a.Version = sizeof a; a.Persist = TRUE;
        a.Attributes = ro ? DISK_ATTRIBUTE_READ_ONLY : 0; a.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;
        DWORD r = 0;
        if (!DeviceIoControl(h_, IOCTL_DISK_SET_DISK_ATTRIBUTES, &a, sizeof a, nullptr, 0, &r, nullptr)) { err = winErrorText(GetLastError()); return false; }
        DeviceIoControl(h_, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &r, nullptr);
        return true;
#else
        int v = ro ? 1 : 0; if (ioctl(fd_, BLKROSET, &v) != 0) { err = std::strerror(errno); return false; }
        return true;
#endif
    }

    // Alle Laufwerksbuchstaben/Volumes dieses Datenträgers sperren und abmelden,
    // damit Windows nicht dazwischenschreibt ("Formatieren?", System Volume Information).
    bool lockVolumes(std::string &err) {
#ifdef _WIN32
        if (diskNumber_ < 0) return true;
        wchar_t name[MAX_PATH];
        HANDLE f = FindFirstVolumeW(name, MAX_PATH);
        if (f == INVALID_HANDLE_VALUE) return true;
        bool ok = true;
        do {
            std::wstring v(name);
            if (!v.empty() && v.back() == L'\\') v.pop_back();
            HANDLE vh = CreateFileW(v.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (vh == INVALID_HANDLE_VALUE) continue;
            uint8_t ext[512]; DWORD r = 0; bool mine = false;
            if (DeviceIoControl(vh, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, ext, sizeof ext, &r, nullptr)) {
                auto *de = reinterpret_cast<VOLUME_DISK_EXTENTS *>(ext);
                for (DWORD i = 0; i < de->NumberOfDiskExtents; ++i) if (int(de->Extents[i].DiskNumber) == diskNumber_) mine = true;
            }
            if (!mine) { CloseHandle(vh); continue; }
            if (!DeviceIoControl(vh, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &r, nullptr)) { ok = false; err = "Volume ist in Benutzung: " + winErrorText(GetLastError()); }
            DeviceIoControl(vh, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &r, nullptr);
            volumes_.push_back(vh);
        } while (FindNextVolumeW(f, name, MAX_PATH));
        FindVolumeClose(f);
        return ok;
#else
        (void)err; return true;
#endif
    }

    // Nach dem Schreiben von Partitionstabellen: Windows soll neu einlesen.
    void refreshPartitions() {
#ifdef _WIN32
        DWORD r = 0; DeviceIoControl(h_, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &r, nullptr);
#else
        ioctl(fd_, BLKRRPART, 0);
#endif
    }

    uint32_t lastOpenError() const { return lastOpenError_; }
#ifdef _WIN32
    HANDLE handle() const { return h_; }
#else
    int handle() const { return fd_; }
#endif

private:
    IoResult io(bool wr, uint64_t offset, void *buf, size_t len, uint32_t timeoutMs) {
        IoResult res;
        const uint64_t t0 = nowMs();
#ifdef _WIN32
        OVERLAPPED ov{}; ov.Offset = DWORD(offset); ov.OffsetHigh = DWORD(offset >> 32); ov.hEvent = ev_;
        ResetEvent(ev_);
        DWORD done = 0;
        BOOL ok = wr ? WriteFile(h_, buf, DWORD(len), nullptr, &ov) : ReadFile(h_, buf, DWORD(len), nullptr, &ov);
        DWORD e = ok ? 0 : GetLastError();
        if (!ok && e == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ev_, timeoutMs) == WAIT_TIMEOUT) {
                CancelIoEx(h_, &ov);
                GetOverlappedResult(h_, &ov, &done, TRUE);
                res.timeout = true; res.error = ERROR_TIMEOUT;
                res.message = fmt("Zeitlimit %u ms überschritten", timeoutMs);
                res.ms = uint32_t(nowMs() - t0);
                return res;
            }
            ok = GetOverlappedResult(h_, &ov, &done, FALSE);
            e = ok ? 0 : GetLastError();
        } else if (ok) GetOverlappedResult(h_, &ov, &done, TRUE);
        res.ok = ok && done == len; res.bytes = done; res.error = e;
        if (!res.ok) res.message = e ? winErrorText(e) : fmt("Nur %lu von %zu Bytes", (unsigned long)done, len);
#else
        (void)timeoutMs;
        ssize_t n = wr ? pwrite(fd_, buf, len, off_t(offset)) : pread(fd_, buf, len, off_t(offset));
        res.ok = n == ssize_t(len); res.bytes = n > 0 ? size_t(n) : 0;
        if (n < 0) { res.error = uint32_t(errno); res.message = std::strerror(errno); }
        else if (!res.ok) res.message = fmt("Nur %zd von %zu Bytes", n, len);
#endif
        res.ms = uint32_t(nowMs() - t0);
        return res;
    }

    std::string path_;
    uint64_t size_ = 0;
    uint32_t sector_ = 512;
    bool writable_ = false, device_ = false;
    int diskNumber_ = -1;
    uint32_t lastOpenError_ = 0;
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
    HANDLE ev_ = nullptr;
    std::vector<HANDLE> volumes_;
#else
    int fd_ = -1;
#endif
};

// Einfache Ausgabedatei (Abbild, Rückgängig-Datei, gerettete Dateien) mit 64-Bit-Versatz.
class OutFile {
public:
    ~OutFile() { close(); }
    bool open(const std::string &path, bool keepExisting, std::string &err) {
#ifdef _WIN32
        f_ = _wfopen(widen(path).c_str(), keepExisting ? L"r+b" : L"wb");
        if (!f_ && keepExisting) f_ = _wfopen(widen(path).c_str(), L"w+b");
#else
        f_ = std::fopen(path.c_str(), keepExisting ? "r+b" : "wb");
        if (!f_ && keepExisting) f_ = std::fopen(path.c_str(), "w+b");
#endif
        if (!f_) { err = "Datei kann nicht angelegt werden: " + path; return false; }
        return true;
    }
    bool writeAt(uint64_t off, const void *p, size_t n) {
#ifdef _WIN32
        if (_fseeki64(f_, int64_t(off), SEEK_SET) != 0) return false;
#else
        if (fseeko(f_, off_t(off), SEEK_SET) != 0) return false;
#endif
        return std::fwrite(p, 1, n, f_) == n;
    }
    bool append(const void *p, size_t n) { return std::fwrite(p, 1, n, f_) == n; }
    void close() { if (f_) std::fclose(f_); f_ = nullptr; }
    bool isOpen() const { return f_ != nullptr; }
private:
    std::FILE *f_ = nullptr;
};

inline bool writeTextFile(const std::string &path, const std::string &text) {
    OutFile f; std::string e;
    if (!f.open(path, false, e)) return false;
    return f.append(text.data(), text.size());
}

inline bool readTextFile(const std::string &path, std::string &out) {
#ifdef _WIN32
    std::FILE *f = _wfopen(widen(path).c_str(), L"rb");
#else
    std::FILE *f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    char b[65536]; size_t n; out.clear();
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    std::fclose(f);
    return true;
}

} // namespace usbr
