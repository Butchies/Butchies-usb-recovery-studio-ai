#pragma once
// Lesende SCSI-Abfragen direkt an den USB-Controller (wie ChipGenius, aber nur Standardbefehle):
// TEST UNIT READY, INQUIRY, Seriennummer, READ CAPACITY, MODE SENSE (Schreibschutzbit), READ(10).
// Es werden keine herstellerspezifischen oder schreibenden Befehle gesendet.
#include "common.h"
#include "catalog.h"

#ifdef _WIN32
#  include <winioctl.h>
#  include <ntddscsi.h>
#  include <cstddef>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <scsi/sg.h>
#endif

namespace usbr {

struct ScsiResult {
    bool sent = false;        // Befehl konnte übertragen werden
    uint8_t status = 0xFF;    // SCSI-Status (0 = GOOD, 2 = CHECK CONDITION)
    uint8_t key = 0, asc = 0, ascq = 0;
    bool hasSense = false;
    std::vector<uint8_t> data;
    uint32_t sysError = 0;
    bool good() const { return sent && status == 0; }
    std::string senseString() const { return hasSense ? senseText(key, asc, ascq) : std::string(); }
};

class ScsiChannel {
public:
    ~ScsiChannel() { close(); }
    bool open(const std::string &path, std::string &err) {
#ifdef _WIN32
        h_ = CreateFileW(widen(path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) { err = winErrorText(GetLastError()); return false; }
        return true;
#else
        fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd_ < 0) { err = std::strerror(errno); return false; }
        return true;
#endif
    }
    void close() {
#ifdef _WIN32
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }

    ScsiResult command(const std::vector<uint8_t> &cdb, uint32_t dataIn, uint32_t timeoutSec = 5) {
        ScsiResult res;
        AlignedBuffer buf(dataIn ? dataIn : 512);
#ifdef _WIN32
        struct Sptd { SCSI_PASS_THROUGH_DIRECT s; ULONG pad; UCHAR sense[32]; } p{};
        p.s.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
        p.s.CdbLength = UCHAR(cdb.size());
        p.s.SenseInfoLength = sizeof p.sense;
        p.s.DataIn = dataIn ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_UNSPECIFIED;
        p.s.DataTransferLength = dataIn;
        p.s.TimeOutValue = timeoutSec;
        p.s.DataBuffer = dataIn ? buf.data() : nullptr;
        p.s.SenseInfoOffset = ULONG(offsetof(Sptd, sense));
        std::memcpy(p.s.Cdb, cdb.data(), std::min<size_t>(cdb.size(), 16));
        DWORD r = 0;
        if (!DeviceIoControl(h_, IOCTL_SCSI_PASS_THROUGH_DIRECT, &p, sizeof p, &p, sizeof p, &r, nullptr)) { res.sysError = GetLastError(); return res; }
        res.sent = true; res.status = p.s.ScsiStatus;
        decodeSense(p.sense, sizeof p.sense, res);
        if (dataIn && res.status == 0) res.data.assign(buf.data(), buf.data() + std::min<uint32_t>(dataIn, p.s.DataTransferLength ? p.s.DataTransferLength : dataIn));
#else
        uint8_t sense[32] = {};
        sg_io_hdr_t h{};
        h.interface_id = 'S';
        h.cmd_len = uint8_t(cdb.size());
        h.cmdp = const_cast<uint8_t *>(cdb.data());
        h.dxfer_direction = dataIn ? SG_DXFER_FROM_DEV : SG_DXFER_NONE;
        h.dxfer_len = dataIn; h.dxferp = dataIn ? buf.data() : nullptr;
        h.sbp = sense; h.mx_sb_len = sizeof sense;
        h.timeout = timeoutSec * 1000;
        if (ioctl(fd_, SG_IO, &h) != 0) { res.sysError = uint32_t(errno); return res; }
        res.sent = true; res.status = h.status;
        if (h.sb_len_wr) decodeSense(sense, h.sb_len_wr, res);
        if (dataIn && h.status == 0 && h.host_status == 0) res.data.assign(buf.data(), buf.data() + (dataIn - uint32_t(h.resid > 0 ? h.resid : 0)));
        if (h.host_status || h.driver_status) { if (!res.status) res.status = 0xFE; }
#endif
        return res;
    }

private:
    static void decodeSense(const uint8_t *s, size_t n, ScsiResult &r) {
        if (n < 8) return;
        uint8_t code = s[0] & 0x7F;
        if (code == 0x70 || code == 0x71) { r.key = s[2] & 0x0F; r.asc = n > 12 ? s[12] : 0; r.ascq = n > 13 ? s[13] : 0; r.hasSense = true; }
        else if (code == 0x72 || code == 0x73) { r.key = s[1] & 0x0F; r.asc = s[2]; r.ascq = s[3]; r.hasSense = true; }
        if (r.hasSense && r.key == 0 && r.asc == 0) r.hasSense = false;
    }
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

struct ScsiReport {
    bool channelOpen = false;
    std::string channelError;
    bool unitReady = false;
    std::string readySense;
    bool inquiryOk = false;
    std::string vendor, product, revision, serial;
    int peripheralType = -1;
    bool removableBit = false;
    bool capacityOk = false;
    uint64_t lastLba = 0;
    uint32_t blockLen = 0;
    uint64_t capacityBytes() const { return capacityOk ? (lastLba + 1) * uint64_t(blockLen) : 0; }
    std::string capacitySense;
    int writeProtectBit = -1;   // -1 unbekannt, 0 nein, 1 Controller meldet Schreibschutz
    bool read0Ok = false;
    std::string read0Sense;
    std::vector<std::string> notes;
    struct Sense { uint8_t key, asc, ascq; };
    std::vector<Sense> senses;    // alle gemeldeten Sense-Codes
    bool hasSenseFor(uint8_t asc) const { for (auto &x : senses) if (x.asc == asc) return true; return false; }
    std::string firstSenseText() const { return senses.empty() ? std::string() : senseText(senses[0].key, senses[0].asc, senses[0].ascq); }
    void keep(const ScsiResult &r) { if (r.hasSense && r.key != 0x6) senses.push_back({r.key, r.asc, r.ascq}); }
};

inline ScsiReport probeScsi(const std::string &path) {
    ScsiReport rep;
    ScsiChannel ch;
    if (!ch.open(path, rep.channelError)) return rep;
    rep.channelOpen = true;

    auto tur = ch.command({0x00, 0, 0, 0, 0, 0}, 0);
    if (!tur.good() && tur.hasSense && tur.key == 0x6) tur = ch.command({0x00, 0, 0, 0, 0, 0}, 0); // Unit Attention quittieren
    rep.keep(tur);
    rep.unitReady = tur.good();
    rep.readySense = tur.senseString();
    if (!tur.sent) rep.notes.push_back(fmt("TEST UNIT READY nicht übertragbar (Systemfehler %u)", tur.sysError));

    auto inq = ch.command({0x12, 0, 0, 0, 36, 0}, 36);
    if (inq.good() && inq.data.size() >= 36) {
        rep.inquiryOk = true;
        rep.peripheralType = inq.data[0] & 0x1F;
        rep.removableBit = (inq.data[1] & 0x80) != 0;
        rep.vendor = trim(std::string(reinterpret_cast<char *>(&inq.data[8]), 8));
        rep.product = trim(std::string(reinterpret_cast<char *>(&inq.data[16]), 16));
        rep.revision = trim(std::string(reinterpret_cast<char *>(&inq.data[32]), 4));
    }
    auto sn = ch.command({0x12, 1, 0x80, 0, 0xFC, 0}, 0xFC);
    if (sn.good() && sn.data.size() > 4) {
        size_t len = std::min<size_t>(sn.data[3], sn.data.size() - 4);
        rep.serial = trim(std::string(reinterpret_cast<char *>(&sn.data[4]), len));
    }

    auto rc = ch.command({0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8);
    if (rc.good() && rc.data.size() >= 8) {
        rep.capacityOk = true; rep.lastLba = be32(&rc.data[0]); rep.blockLen = be32(&rc.data[4]);
        if (rep.lastLba == 0xFFFFFFFFu) {
            auto rc16 = ch.command({0x9E, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0}, 32);
            if (rc16.good() && rc16.data.size() >= 12) { rep.lastLba = be64(&rc16.data[0]); rep.blockLen = be32(&rc16.data[8]); }
        }
    } else { rep.capacitySense = rc.senseString(); rep.keep(rc); }

    auto ms = ch.command({0x1A, 0, 0x3F, 0, 0xC0, 0}, 0xC0);
    if (!ms.good()) ms = ch.command({0x1A, 0x08, 0x00, 0, 0x04, 0}, 4);
    if (ms.good() && ms.data.size() >= 3) rep.writeProtectBit = (ms.data[2] & 0x80) ? 1 : 0;

    if (rep.capacityOk && rep.blockLen >= 512 && rep.blockLen <= 4096) {
        auto r0 = ch.command({0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0}, rep.blockLen, 8);
        rep.read0Ok = r0.good();
        rep.read0Sense = r0.senseString();
        rep.keep(r0);
    }
    return rep;
}

} // namespace usbr
