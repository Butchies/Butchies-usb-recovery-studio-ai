#pragma once
// Lokale, lernende Wissensbasis: speichert jeden Reparaturversuch und jede bestätigte Diagnose
// (je VID/PID, Controllerfamilie und Symptommuster) und liefert Erfahrungswerte für ähnliche Sticks.
#include "disk_io.h"
#include <map>
#include <set>
#include <sstream>
#include <ctime>

namespace usbr {

struct Experience { int tries = 0, successes = 0; double rate() const { return tries ? double(successes) / tries : 0.0; } };

class KnowledgeStore {
public:
    struct Rec { char kind; std::string ts, id, family, symptoms, what; int value; };

    bool load(const std::string &path) {
        path_ = path; recs_.clear();
        std::string text;
        if (!readTextFile(path, text)) return false;
        std::istringstream in(text); std::string line;
        while (std::getline(in, line)) {
            auto f = split(line);
            if (f.size() < 7 || (f[0] != "A" && f[0] != "D")) continue;
            recs_.push_back({f[0][0], f[1], f[2], f[3], f[4], f[5], std::atoi(f[6].c_str())});
        }
        return true;
    }
    void recordAction(const std::string &id, const std::string &family, const std::string &symptoms, const std::string &action, bool ok) { add({'A', now(), id, family, symptoms, action, ok ? 1 : 0}); }
    void recordDiagnosis(const std::string &id, const std::string &family, const std::string &symptoms, const std::string &hyp, bool confirmed) { add({'D', now(), id, family, symptoms, hyp, confirmed ? 1 : 0}); }

    // Erfahrung mit einer Aktion bei ähnlichen Sticks (gleiche Kennung oder ähnliche Symptome)
    Experience actionExperience(const std::string &id, const std::string &symptoms, const std::string &action) const {
        Experience e;
        for (auto &r : recs_) if (r.kind == 'A' && r.what == action && (r.id == id || similarity(r.symptoms, symptoms) >= 0.6)) { ++e.tries; e.successes += r.value; }
        return e;
    }
    // Bestätigte Diagnosen ähnlicher Fälle: Hypothese -> Anteil
    std::map<std::string, double> diagnosisPrior(const std::string &id, const std::string &symptoms) const {
        std::map<std::string, Experience> m;
        for (auto &r : recs_) if (r.kind == 'D' && (r.id == id || similarity(r.symptoms, symptoms) >= 0.6)) { ++m[r.what].tries; m[r.what].successes += r.value; }
        std::map<std::string, double> out;
        for (auto &[k, v] : m) if (v.tries) out[k] = v.rate();
        return out;
    }
    struct Summary { int actions = 0, successes = 0, diagnoses = 0, devices = 0; };
    Summary summary() const {
        Summary s; std::set<std::string> dev;
        for (auto &r : recs_) { if (r.kind == 'A') { ++s.actions; s.successes += r.value; } else ++s.diagnoses; dev.insert(r.id); }
        s.devices = int(dev.size());
        return s;
    }
    const std::vector<Rec> &records() const { return recs_; }

    static double similarity(const std::string &a, const std::string &b) {
        auto sa = tokens(a), sb = tokens(b);
        if (sa.empty() && sb.empty()) return 1.0;
        size_t inter = 0; for (auto &x : sa) inter += sb.count(x);
        size_t uni = sa.size() + sb.size() - inter;
        return uni ? double(inter) / double(uni) : 0.0;
    }

private:
    static std::set<std::string> tokens(const std::string &s) { std::set<std::string> t; std::istringstream in(s); std::string x; while (std::getline(in, x, ',')) if (!x.empty()) t.insert(x); return t; }
    static std::vector<std::string> split(const std::string &l) { std::vector<std::string> f; std::istringstream in(l); std::string x; while (std::getline(in, x, '|')) f.push_back(x); return f; }
    static std::string clean(std::string s) { for (auto &c : s) if (c == '|' || c == '\n' || c == '\r') c = ' '; return s; }
    static std::string now() { std::time_t t = std::time(nullptr); char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", std::localtime(&t)); return b; }
    void add(Rec r) {
        recs_.push_back(r);
        if (path_.empty()) return;
        OutFile f; std::string e;
        if (!f.open(path_, true, e)) return;
        std::string existing; readTextFile(path_, existing);
        std::string line = fmt("%c|%s|%s|%s|%s|%s|%d\n", r.kind, r.ts.c_str(), clean(r.id).c_str(), clean(r.family).c_str(), clean(r.symptoms).c_str(), clean(r.what).c_str(), r.value);
        f.writeAt(existing.size(), line.data(), line.size());
    }
    std::string path_;
    std::vector<Rec> recs_;
};

} // namespace usbr
