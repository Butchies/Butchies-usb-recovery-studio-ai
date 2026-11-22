#pragma once
// Protokolle selbst auslesen und übersetzen:
// Windows-Ereignisanzeige (System, Kernel-PnP), setupapi.dev.log; unter Linux dmesg.
#include "common.h"
#include "catalog.h"
#include "usb_inventory.h"
#include "disk_io.h"
#include <map>

#ifdef _WIN32
#  include <winevt.h>
#endif

namespace usbr {

struct LogEntry {
    std::string time;       // ISO-Zeit
    std::string source;     // Anbieter / Datei
    int id = 0;
    std::string category;   // bad, controller, io, connection, fs, partition, pnp, writeprotect, nomedia, driver ...
    int weight = 0;         // 0..3
    std::string meaning;    // Übersetzung
    std::string raw;        // Originaltext (gekürzt)
    bool matched = false;   // eindeutig diesem Gerät zugeordnet
};

struct LogReport {
    std::vector<LogEntry> entries;
    std::map<std::string, int> score;  // Kategorie -> gewichtete Summe
    std::vector<std::string> notes;
    int count(const std::string &cat) const { auto it = score.find(cat); return it == score.end() ? 0 : it->second; }
};

inline std::string lower(std::string s) { for (auto &c : s) c = char(std::tolower((unsigned char)c)); return s; }

inline void addEntry(LogReport &rep, LogEntry e) {
    rep.score[e.category] += e.matched ? e.weight : (e.weight + 1) / 2;
    if (e.raw.size() > 400) e.raw = e.raw.substr(0, 400) + " …";
    rep.entries.push_back(std::move(e));
}

#ifdef _WIN32
inline std::string xmlValue(const std::string &x, const std::string &tag, const std::string &attr = {}) {
    if (!attr.empty()) {
        auto p = x.find("<" + tag); if (p == std::string::npos) return {};
        auto a = x.find(attr + "='", p); char q = '\'';
        if (a == std::string::npos) { a = x.find(attr + "=\"", p); q = '"'; }
        if (a == std::string::npos) return {};
        a += attr.size() + 2; auto e = x.find(q, a);
        return x.substr(a, e - a);
    }
    auto p = x.find("<" + tag); if (p == std::string::npos) return {};
    p = x.find('>', p); if (p == std::string::npos) return {};
    auto e = x.find("</" + tag, p);
    return e == std::string::npos ? std::string() : x.substr(p + 1, e - p - 1);
}

inline std::string renderEventXml(EVT_HANDLE ev) {
    DWORD used = 0, props = 0;
    EvtRender(nullptr, ev, EvtRenderEventXml, 0, nullptr, &used, &props);
    std::vector<wchar_t> buf(used / sizeof(wchar_t) + 2);
    if (!EvtRender(nullptr, ev, EvtRenderEventXml, DWORD(buf.size() * sizeof(wchar_t)), buf.data(), &used, &props)) return {};
    return narrow(buf.data());
}

inline std::string formatEventMessage(EVT_HANDLE ev, const std::wstring &provider, std::map<std::wstring, EVT_HANDLE> &cache) {
    EVT_HANDLE meta = nullptr;
    auto it = cache.find(provider);
    if (it == cache.end()) { meta = EvtOpenPublisherMetadata(nullptr, provider.c_str(), nullptr, 0, 0); cache[provider] = meta; }
    else meta = it->second;
    if (!meta) return {};
    DWORD used = 0;
    EvtFormatMessage(meta, ev, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &used);
    if (!used) return {};
    std::vector<wchar_t> buf(used + 1);
    if (!EvtFormatMessage(meta, ev, 0, 0, nullptr, EvtFormatMessageEvent, used, buf.data(), &used)) return {};
    return trim(narrow(buf.data()));
}

inline void readChannel(const wchar_t *channel, const std::wstring &query, const UsbDevice &dev, LogReport &rep, int limit) {
    EVT_HANDLE q = EvtQuery(nullptr, channel, query.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!q) { rep.notes.push_back("Protokoll nicht lesbar: " + narrow(channel) + " (" + winErrorText(GetLastError()) + ")"); return; }
    std::map<std::wstring, EVT_HANDLE> metaCache;
    const std::string disk = dev.diskNumber >= 0 ? lower("harddisk" + std::to_string(dev.diskNumber) + "\\") : std::string("\x01");
    const std::string vp = dev.hasVidPid ? lower("vid_" + hex4(dev.vid) + "&pid_" + hex4(dev.pid)) : std::string("\x01");
    const std::string inst = dev.usbInstanceId.empty() ? std::string("\x01") : lower(dev.usbInstanceId);
    const std::string dinst = dev.diskInstanceId.empty() ? std::string("\x01") : lower(dev.diskInstanceId);
    EVT_HANDLE evs[32]; DWORD got = 0; int n = 0;
    while (n < limit && EvtNext(q, 32, evs, 2000, 0, &got)) {
        for (DWORD i = 0; i < got; ++i) {
            if (n < limit) {
                std::string x = renderEventXml(evs[i]);
                std::string prov = xmlValue(x, "Provider", "Name");
                int id = std::atoi(xmlValue(x, "EventID").c_str());
                std::string lx = lower(x);
                bool matched = lx.find(disk) != std::string::npos || lx.find(vp) != std::string::npos ||
                               lx.find(inst) != std::string::npos || lx.find(dinst) != std::string::npos;
                const EventInfo *info = findEvent(prov, id);
                bool usbGeneric = prov.find("USB") != std::string::npos;
                if (info && info->weight > 0 && (matched || usbGeneric)) {
                    LogEntry e;
                    e.time = xmlValue(x, "TimeCreated", "SystemTime").substr(0, 19);
                    e.source = prov; e.id = id; e.category = info->category; e.weight = info->weight;
                    e.meaning = info->text; e.matched = matched;
                    e.raw = formatEventMessage(evs[i], widen(prov), metaCache);
                    addEntry(rep, std::move(e));
                    ++n;
                }
            }
            EvtClose(evs[i]);
        }
    }
    for (auto &m : metaCache) if (m.second) EvtClose(m.second);
    EvtClose(q);
}
#endif

// setupapi.dev.log: Installationsabschnitte zu dieser VID/PID mit Fehlern (!!!) und Warnungen (!)
inline void readSetupApiLog(const UsbDevice &dev, LogReport &rep) {
#ifdef _WIN32
    if (!dev.hasVidPid) return;
    wchar_t win[MAX_PATH]; if (!GetWindowsDirectoryW(win, MAX_PATH)) return;
    std::string path = narrow(win) + "\\INF\\setupapi.dev.log", text;
    if (!readTextFile(path, text)) { rep.notes.push_back("setupapi.dev.log nicht lesbar"); return; }
    const std::string vp = lower("vid_" + hex4(dev.vid) + "&pid_" + hex4(dev.pid));
    size_t pos = 0; int sections = 0, errors = 0; std::string lastErr, lastTime;
    while ((pos = text.find(">>>  [", pos)) != std::string::npos) {
        size_t end = text.find("<<<  [", pos);
        if (end == std::string::npos) end = text.size();
        size_t hdrEnd = text.find('\n', pos);
        std::string hdr = lower(text.substr(pos, hdrEnd - pos));
        if (hdr.find(vp) != std::string::npos) {
            ++sections;
            std::string sec = text.substr(pos, end - pos);
            auto ts = sec.find("Section start ");
            std::string t = ts != std::string::npos ? sec.substr(ts + 14, 19) : std::string();
            size_t l = 0;
            while ((l = sec.find("!!!", l)) != std::string::npos) {
                size_t le = sec.find('\n', l);
                ++errors; lastErr = trim(sec.substr(l, le - l)); lastTime = t;
                l = le;
            }
        }
        pos = end == text.size() ? end : end + 1;
    }
    if (sections) rep.notes.push_back(fmt("setupapi.dev.log: %d Installationsvorgänge für %s gefunden, %d Fehlerzeilen", sections, dev.idString().c_str(), errors));
    if (errors) {
        LogEntry e; e.time = lastTime; e.source = "setupapi.dev.log"; e.category = "driver"; e.weight = 1;
        e.meaning = "Treiberinstallation für diesen Stick meldete Fehler"; e.raw = lastErr; e.matched = true;
        addEntry(rep, std::move(e));
    }
#else
    (void)dev; (void)rep;
#endif
}

inline LogReport collectLogs(const UsbDevice &dev, [[maybe_unused]] int days = 14) {
    LogReport rep;
    if (dev.imageFile) { rep.notes.push_back("Abbilddatei – keine Geräteprotokolle"); return rep; }
#ifdef _WIN32
    const uint64_t ms = uint64_t(days) * 86400000ull;
    std::wstring prov;
    const wchar_t *providers[] = {L"disk", L"Ntfs", L"Microsoft-Windows-Ntfs", L"volmgr", L"partmgr", L"Microsoft-Windows-Kernel-PnP",
                                  L"Microsoft-Windows-StorPort", L"UASPStor", L"USBSTOR", L"USBHUB3", L"Microsoft-Windows-USB-USBHUB3", L"Microsoft-Windows-USB-USBXHCI"};
    for (auto *p : providers) { if (!prov.empty()) prov += L" or "; prov += L"Provider[@Name='"; prov += p; prov += L"']"; }
    std::wstring q = L"*[System[(" + prov + L") and TimeCreated[timediff(@SystemTime) <= " + std::to_wstring(ms) + L"]]]";
    readChannel(L"System", q, dev, rep, 300);
    std::wstring q2 = L"*[System[TimeCreated[timediff(@SystemTime) <= " + std::to_wstring(ms) + L"]]]";
    readChannel(L"Microsoft-Windows-Kernel-PnP/Configuration", q2, dev, rep, 100);
    readSetupApiLog(dev, rep);
    if (dev.problemCode) {
        LogEntry e; e.source = "Geräte-Manager"; e.id = int(dev.problemCode); e.category = dev.problemCode == 43 ? "connection" : "pnp";
        e.weight = 3; e.meaning = problemCodeText(dev.problemCode); e.matched = true;
        addEntry(rep, std::move(e));
    }
#else
    std::FILE *p = popen("dmesg 2>/dev/null | tail -n 4000", "r");
    if (!p) { rep.notes.push_back("dmesg nicht lesbar"); return rep; }
    std::string devName = dev.path.substr(dev.path.find_last_of('/') + 1);
    char line[1024];
    while (std::fgets(line, sizeof line, p)) {
        std::string l = line;
        bool matched = !devName.empty() && l.find(devName) != std::string::npos;
        for (const auto &k : kernelPatterns()) {
            if (l.find(k.needle) == std::string::npos) continue;
            if (!matched && l.find("usb") == std::string::npos) continue;
            LogEntry e; e.source = "dmesg"; e.category = k.category; e.weight = k.weight; e.meaning = k.text;
            e.raw = trim(l); e.matched = matched;
            addEntry(rep, std::move(e));
            break;
        }
    }
    pclose(p);
    if (rep.entries.empty()) rep.notes.push_back("dmesg: keine passenden Meldungen (oder keine Berechtigung)");
#endif
    return rep;
}

} // namespace usbr
