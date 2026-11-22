#pragma once
// Erkennung aller USB-Datenträger – auch ohne Medium ("Bitte Datenträger einlegen"), mit 0 Byte
// und nicht erkannte USB-Geräte mit Problemcode (z. B. Code 43). Interne Laufwerke werden ausgefiltert.
#include "common.h"
#include "catalog.h"

#ifdef _WIN32
#  include <winioctl.h>
#  include <setupapi.h>
#  include <cfgmgr32.h>
#  include <cwchar>
#else
#  include <dirent.h>
#  include <climits>
#  include <unistd.h>
#endif

namespace usbr {

struct UsbDevice {
    bool isDisk = true;            // false = nur USB-Gerät ohne Datenträger (z. B. Code 43)
    int diskNumber = -1;
    std::string path;              // \\.\PhysicalDriveN bzw. /dev/sdX bzw. Abbilddatei
    std::string vendor, product, revision, serial;
    std::string description;
    uint64_t size = 0;
    uint32_t sectorSize = 512;
    bool mediaPresent = true;
    bool removable = false;
    bool readOnlyAttr = false;
    bool imageFile = false;
    uint16_t vid = 0, pid = 0;
    bool hasVidPid = false;
    std::string usbInstanceId;     // USB\VID_xxxx&PID_yyyy\...
    std::string diskInstanceId;    // USBSTOR\DISK&... bzw. SCSI\...
    uint32_t problemCode = 0;
    uint32_t openError = 0;
    bool uas = false;
    std::vector<std::string> driveLetters;
    std::string busType = "USB";

    std::string displayName() const {
        std::string n = trim(vendor + " " + product);
        if (n.empty()) n = description.empty() ? "Unbekanntes USB-Gerät" : description;
        return n;
    }
    std::string idString() const { return hasVidPid ? hex4(vid) + ":" + hex4(pid) : "????:????"; }
};

struct HostPolicy {
    bool registryWriteProtect = false;     // HKLM\...\StorageDevicePolicies\WriteProtect = 1
    bool policyDenyWrite = false;          // Gruppenrichtlinie: Schreibzugriff auf Wechseldatenträger verweigert
    bool policyDenyRead = false;
};

inline bool parseVidPid(const std::string &id, uint16_t &vid, uint16_t &pid) {
    std::string up = id; for (auto &c : up) c = char(std::toupper((unsigned char)c));
    auto v = up.find("VID_"), p = up.find("PID_");
    if (v == std::string::npos || p == std::string::npos) return false;
    vid = uint16_t(std::strtoul(up.substr(v + 4, 4).c_str(), nullptr, 16));
    pid = uint16_t(std::strtoul(up.substr(p + 4, 4).c_str(), nullptr, 16));
    return true;
}

#ifdef _WIN32
// GUID_DEVINTERFACE_DISK (eigene Kopie, damit kein initguid-Reihenfolgeproblem entsteht)
static const GUID kGuidDevInterfaceDisk = {0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
inline std::string cmDeviceId(DEVINST d) {
    wchar_t buf[MAX_DEVICE_ID_LEN + 1] = {};
    if (CM_Get_Device_IDW(d, buf, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
    return narrow(buf);
}
inline uint32_t cmProblem(DEVINST d) {
    ULONG st = 0, pr = 0;
    if (CM_Get_DevNode_Status(&st, &pr, d, 0) != CR_SUCCESS) return 0;
    return (st & DN_HAS_PROBLEM) ? uint32_t(pr) : 0;
}
inline std::string setupProp(HDEVINFO s, SP_DEVINFO_DATA &d, DWORD prop) {
    wchar_t buf[2048] = {}; DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(s, &d, prop, &type, reinterpret_cast<PBYTE>(buf), sizeof buf - 2, nullptr)) return {};
    if (type == REG_MULTI_SZ) { std::string out; for (wchar_t *p = buf; *p; p += wcslen(p) + 1) { out += narrow(p); out += ';'; } return out; }
    return narrow(buf);
}
inline int systemDiskNumber() {
    wchar_t sys[MAX_PATH]; if (!GetEnvironmentVariableW(L"SystemDrive", sys, MAX_PATH)) return -1;
    std::wstring p = L"\\\\.\\" + std::wstring(sys);
    HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    STORAGE_DEVICE_NUMBER n{}; DWORD r = 0; int res = -1;
    if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &n, sizeof n, &r, nullptr)) res = int(n.DeviceNumber);
    CloseHandle(h);
    return res;
}
#endif

inline HostPolicy readHostPolicy() {
    HostPolicy p;
#ifdef _WIN32
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\StorageDevicePolicies", L"WriteProtect", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        p.registryWriteProtect = v != 0;
    const wchar_t *k = L"SOFTWARE\\Policies\\Microsoft\\Windows\\RemovableStorageDevices\\{53f5630d-b6bf-11d0-94f2-00a0c91efb8b}";
    v = 0; sz = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, k, L"Deny_Write", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) p.policyDenyWrite = v != 0;
    v = 0; sz = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, k, L"Deny_Read", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) p.policyDenyRead = v != 0;
    v = 0; sz = sizeof v;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\RemovableStorageDevices", L"Deny_All", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v)
        p.policyDenyWrite = p.policyDenyRead = true;
#endif
    return p;
}

inline std::vector<UsbDevice> enumerateUsbDevices() {
    std::vector<UsbDevice> out;
#ifdef _WIN32
    const int sysDisk = systemDiskNumber();
    HDEVINFO set = SetupDiGetClassDevsW(&kGuidDevInterfaceDisk, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA ifd{}; ifd.cbSize = sizeof ifd;
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kGuidDevInterfaceDisk, i, &ifd); ++i) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
            std::vector<uint8_t> buf(need + 8);
            auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            SP_DEVINFO_DATA dev{}; dev.cbSize = sizeof dev;
            if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, &dev)) continue;

            HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            UsbDevice d;
            DWORD r = 0;
            STORAGE_DEVICE_NUMBER num{};
            if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &num, sizeof num, &r, nullptr)) d.diskNumber = int(num.DeviceNumber);
            STORAGE_PROPERTY_QUERY q{}; q.PropertyId = StorageDeviceProperty; q.QueryType = PropertyStandardQuery;
            uint8_t pb[1024] = {};
            STORAGE_BUS_TYPE bus = BusTypeUnknown;
            if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, pb, sizeof pb, &r, nullptr)) {
                auto *sd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR *>(pb);
                bus = sd->BusType;
                auto str = [&](DWORD off) { return off && off < r ? trim(std::string(reinterpret_cast<char *>(pb) + off)) : std::string(); };
                d.vendor = str(sd->VendorIdOffset); d.product = str(sd->ProductIdOffset);
                d.revision = str(sd->ProductRevisionOffset); d.serial = str(sd->SerialNumberOffset);
                d.removable = sd->RemovableMedia != 0;
            }
            CloseHandle(h);
            if (bus != BusTypeUsb) continue;                       // nur USB
            if (d.diskNumber >= 0 && d.diskNumber == sysDisk) continue; // Systemdatenträger nie anbieten

            d.diskInstanceId = cmDeviceId(dev.DevInst);
            d.uas = d.diskInstanceId.rfind("SCSI\\", 0) == 0;
            d.problemCode = cmProblem(dev.DevInst);
            d.description = setupProp(set, dev, SPDRP_FRIENDLYNAME);
            DEVINST cur = dev.DevInst;
            for (int lvl = 0; lvl < 4; ++lvl) {
                DEVINST parent = 0;
                if (CM_Get_Parent(&parent, cur, 0) != CR_SUCCESS) break;
                std::string pid = cmDeviceId(parent);
                if (pid.rfind("USB\\VID_", 0) == 0) {
                    d.usbInstanceId = pid; d.hasVidPid = parseVidPid(pid, d.vid, d.pid);
                    if (!d.problemCode) d.problemCode = cmProblem(parent);
                    break;
                }
                cur = parent;
            }
            d.path = d.diskNumber >= 0 ? "\\\\.\\PhysicalDrive" + std::to_string(d.diskNumber) : std::string();

            if (!d.path.empty()) {
                HANDLE hd = CreateFileW(widen(d.path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (hd == INVALID_HANDLE_VALUE) d.openError = GetLastError();
                else {
                    GET_LENGTH_INFORMATION li{};
                    if (DeviceIoControl(hd, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &li, sizeof li, &r, nullptr)) d.size = uint64_t(li.Length.QuadPart);
                    else { d.openError = GetLastError(); d.mediaPresent = false; }
                    DISK_GEOMETRY_EX g{};
                    if (DeviceIoControl(hd, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &g, sizeof g, &r, nullptr) && g.Geometry.BytesPerSector)
                        d.sectorSize = g.Geometry.BytesPerSector;
                    if (DeviceIoControl(hd, IOCTL_STORAGE_CHECK_VERIFY2, nullptr, 0, nullptr, 0, &r, nullptr) == FALSE) {
                        DWORD e = GetLastError();
                        if (e == ERROR_NOT_READY || e == ERROR_NO_MEDIA_IN_DRIVE) d.mediaPresent = false;
                    }
                    GET_DISK_ATTRIBUTES a{};
                    if (DeviceIoControl(hd, IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &a, sizeof a, &r, nullptr))
                        d.readOnlyAttr = (a.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;
                    CloseHandle(hd);
                }
                if (d.size == 0) d.mediaPresent = false;
                DWORD mask = GetLogicalDrives();
                for (int l = 0; l < 26; ++l) {
                    if (!(mask & (1u << l))) continue;
                    wchar_t vp[] = {L'\\', L'\\', L'.', L'\\', wchar_t(L'A' + l), L':', 0};
                    HANDLE vh = CreateFileW(vp, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (vh == INVALID_HANDLE_VALUE) continue;
                    STORAGE_DEVICE_NUMBER vn{};
                    if (DeviceIoControl(vh, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &vn, sizeof vn, &r, nullptr) && int(vn.DeviceNumber) == d.diskNumber && vn.DeviceType == FILE_DEVICE_DISK)
                        d.driveLetters.push_back(std::string(1, char('A' + l)) + ":");
                    CloseHandle(vh);
                }
            }
            out.push_back(std::move(d));
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    // Nicht erkannte bzw. fehlerhafte USB-Geräte (Code 43, VID_0000&PID_0002, Massenspeicher ohne Laufwerk)
    HDEVINFO usb = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (usb != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA dev{}; dev.cbSize = sizeof dev;
        for (DWORD i = 0; SetupDiEnumDeviceInfo(usb, i, &dev); ++i) {
            uint32_t prob = cmProblem(dev.DevInst);
            if (!prob) continue;
            std::string id = cmDeviceId(dev.DevInst);
            std::string compat = setupProp(usb, dev, SPDRP_COMPATIBLEIDS);
            bool storage = compat.find("Class_08") != std::string::npos || id.find("VID_0000") != std::string::npos || prob == 43;
            if (!storage) continue;
            bool known = false;
            for (auto &d : out) if (d.usbInstanceId == id) known = true;
            if (known) continue;
            UsbDevice d; d.isDisk = false; d.mediaPresent = false; d.usbInstanceId = id;
            d.hasVidPid = parseVidPid(id, d.vid, d.pid);
            d.problemCode = prob;
            d.description = setupProp(usb, dev, SPDRP_DEVICEDESC);
            out.push_back(std::move(d));
        }
        SetupDiDestroyDeviceInfoList(usb);
    }
#else
    DIR *dir = opendir("/sys/block");
    if (dir) {
        while (dirent *e = readdir(dir)) {
            std::string n = e->d_name;
            if (n.rfind("sd", 0) != 0) continue;
            std::string base = "/sys/block/" + n;
            char real[PATH_MAX] = {};
            if (!realpath(base.c_str(), real)) continue;
            std::string rp = real;
            if (rp.find("/usb") == std::string::npos) continue;
            auto rd = [](const std::string &p) { std::string s; std::FILE *f = std::fopen(p.c_str(), "r"); if (f) { char b[256]; if (std::fgets(b, sizeof b, f)) s = b; std::fclose(f); } return trim(s); };
            UsbDevice d; d.path = "/dev/" + n;
            d.size = std::strtoull(rd(base + "/size").c_str(), nullptr, 10) * 512ull;
            d.mediaPresent = d.size > 0;
            d.removable = rd(base + "/removable") == "1";
            d.readOnlyAttr = rd(base + "/ro") == "1";
            d.vendor = rd(base + "/device/vendor"); d.product = rd(base + "/device/model"); d.revision = rd(base + "/device/rev");
            std::string up = rp;
            for (int i = 0; i < 8 && !up.empty(); ++i) {
                up = up.substr(0, up.find_last_of('/'));
                std::string v = rd(up + "/idVendor");
                if (!v.empty()) {
                    d.vid = uint16_t(std::strtoul(v.c_str(), nullptr, 16));
                    d.pid = uint16_t(std::strtoul(rd(up + "/idProduct").c_str(), nullptr, 16));
                    d.serial = rd(up + "/serial"); d.hasVidPid = true;
                    d.usbInstanceId = up; break;
                }
            }
            out.push_back(std::move(d));
        }
        closedir(dir);
    }
#endif
    return out;
}

inline UsbDevice imageAsDevice(const std::string &path, uint64_t size) {
    UsbDevice d; d.path = path; d.imageFile = true; d.size = size; d.mediaPresent = size > 0;
    d.description = "Abbilddatei"; d.product = path.substr(path.find_last_of("/\\") + 1);
    d.busType = "Datei";
    return d;
}

// USB-Gerät deaktivieren und wieder aktivieren (wie Abziehen/Einstecken). Keine Daten werden verändert.
inline bool restartUsbDevice(const std::string &instanceId, std::string &err) {
#ifdef _WIN32
    DEVINST dn = 0;
    std::wstring id = widen(instanceId);
    if (CM_Locate_DevNodeW(&dn, const_cast<wchar_t *>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) { err = "Gerät nicht gefunden"; return false; }
    CONFIGRET c = CM_Disable_DevNode(dn, CM_DISABLE_UI_NOT_OK);
    if (c != CR_SUCCESS) {
        // Fallback: Elternknoten (Hub) neu aufzählen lassen
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, dn, 0) == CR_SUCCESS && CM_Reenumerate_DevNode(parent, CM_REENUMERATE_SYNCHRONOUS) == CR_SUCCESS) return true;
        err = fmt("Gerät ließ sich nicht neu starten (CONFIGRET %lu, Administratorrechte?)", (unsigned long)c); return false;
    }
    Sleep(1500);
    c = CM_Enable_DevNode(dn, 0);
    if (c != CR_SUCCESS) { err = fmt("Wiedereinschalten fehlgeschlagen (CONFIGRET %lu)", (unsigned long)c); return false; }
    Sleep(2500);
    return true;
#else
    err = "Nur unter Windows verfügbar"; (void)instanceId; return false;
#endif
}

// Registry-Schreibschutzrichtlinie zurücksetzen (betrifft alle USB-Datenträger dieses PCs).
inline bool clearHostWriteProtect(std::string &err) {
#ifdef _WIN32
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\StorageDevicePolicies", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        DWORD z = 0;
        LONG r = RegSetValueExW(k, L"WriteProtect", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&z), sizeof z);
        RegCloseKey(k);
        if (r != ERROR_SUCCESS) { err = winErrorText(DWORD(r)); return false; }
    }
    return true;
#else
    err = "Nur unter Windows verfügbar"; return false;
#endif
}

} // namespace usbr
