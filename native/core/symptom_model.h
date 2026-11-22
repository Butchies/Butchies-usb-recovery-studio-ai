#pragma once
// Trainiertes Symptom-Modell (Naive Bayes über Symptommerkmale).
// Trainiert beim Start aus typischen Fehlerbildern defekter USB-Sticks (eingebaute Trainingsfälle, mit Variationen)
// und aus den vom Benutzer bestätigten Diagnosen der Wissensbasis. Liefert in Millisekunden eine Einschätzung,
// noch bevor die langsameren Messungen fertig sind, und verstärkt die regelbasierten Belege.
#include "knowledge_store.h"
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace usbr {

// Merkmale (siehe symptomKey in diagnosis_engine.h):
// nodisk code43 code10 codeX vid0000 nomedia tiny zerosize sense3a sense04 sense11 sense27 wpbit roattr hostpolicy
// table fs backup zero0 ff0 blank readerr timeout bad slow fake beyond fakevid bigcap conn cardreader
// logbad logfs logpart fsok readok
struct TrainingCase { const char *features; const char *label; };

inline const std::vector<TrainingCase> &builtinTrainingCases() {
    static const std::vector<TrainingCase> v = {
        // Stick wird nicht erkannt
        {"nodisk,code43,vid0000", "not_detected"}, {"nodisk,code43,conn", "not_detected"}, {"nodisk,code43", "not_detected"},
        {"nodisk,codeX", "not_detected"}, {"nodisk,code10", "not_detected"}, {"nodisk,vid0000,conn", "not_detected"},
        // Controller im Notmodus
        {"nomedia,sense3a", "controller_safe_mode"}, {"nomedia,sense3a,zerosize", "controller_safe_mode"}, {"nomedia", "controller_safe_mode"},
        {"tiny,readok,blank", "controller_safe_mode"}, {"tiny,table,blank", "controller_safe_mode"}, {"tiny,readok", "controller_safe_mode"},
        {"zerosize,nomedia", "controller_safe_mode"}, {"readok,blank,table,ff0", "controller_safe_mode"}, {"nomedia,sense04", "controller_safe_mode"},
        {"tiny,sense3a", "controller_safe_mode"}, {"readok,blank,zero0,table", "controller_safe_mode"},
        // Speicher verschlissen, nur lesen
        {"wpbit,readok,fsok", "nand_readonly"}, {"wpbit,readok,sense27,fsok", "nand_readonly"}, {"wpbit,readok,slow,fsok", "nand_readonly"},
        {"wpbit,readok,readerr", "nand_readonly"}, {"wpbit,sense27,readok", "nand_readonly"}, {"wpbit,readok,fs", "nand_readonly"},
        // Schreibschutz durch Windows
        {"roattr,readok,fsok", "windows_readonly"}, {"hostpolicy,readok,fsok", "windows_readonly"}, {"roattr,hostpolicy,readok,fsok", "windows_readonly"},
        {"roattr,readok", "windows_readonly"},
        // Partitionstabelle defekt
        {"table,zero0,readok", "partition_damaged"}, {"table,readok,logpart", "partition_damaged"}, {"table,zero0,readok,backup", "partition_damaged"},
        {"table,readok", "partition_damaged"}, {"table,zero0,readok,logpart", "partition_damaged"},
        // Dateisystem defekt
        {"fs,backup,readok", "fs_damaged"}, {"fs,readok,logfs", "fs_damaged"}, {"fs,readok", "fs_damaged"}, {"fs,backup,readok,logfs", "fs_damaged"},
        {"fs,readok,zero0", "fs_damaged"}, {"fs,readok,beyond", "fs_damaged"},
        // Defekte Speicherbereiche
        {"readerr,readok,logbad", "bad_sectors"}, {"readerr,readok,sense11", "bad_sectors"}, {"bad,readok,slow", "bad_sectors"},
        {"readerr,timeout,readok,logbad", "bad_sectors"}, {"bad,readok,logbad,fsok", "bad_sectors"}, {"readerr,readok,fs", "bad_sectors"},
        {"readerr,readok,slow,sense11", "bad_sectors"},
        // Fake-Kapazität
        {"fake,readok", "fake_capacity"}, {"fake,readok,fakevid,bigcap", "fake_capacity"}, {"fakevid,bigcap,readok,fsok", "fake_capacity"},
        {"beyond,readok,fakevid", "fake_capacity"}, {"fake,readok,fs,beyond", "fake_capacity"}, {"fakevid,bigcap,readok,readerr", "fake_capacity"},
        {"bigcap,readok,blank,fakevid", "fake_capacity"},
        // Verbindungsproblem
        {"conn,timeout,readok", "connection"}, {"conn,readok,readerr", "connection"}, {"conn,timeout", "connection"}, {"conn,code43", "connection"},
        {"timeout,readok,conn,fsok", "connection"},
        // Kartenleser
        {"cardreader,nomedia", "card_reader"}, {"cardreader,nomedia,sense3a", "card_reader"}, {"cardreader,readok,fsok", "card_reader"},
        // In Ordnung
        {"readok,fsok", "healthy"}, {"readok,fsok,slow", "healthy"}, {"readok,fsok,bigcap", "healthy"},
    };
    return v;
}

class SymptomModel {
public:
    struct Prediction { std::string label; double p; };

    // Trainieren: eingebaute Fälle (mit Variationen) + bestätigte Diagnosen der Wissensbasis
    void train(const KnowledgeStore *kb = nullptr, int variations = 12) {
        data_.clear();
        std::mt19937 rng(20261122);
        for (const auto &c : builtinTrainingCases()) {
            auto f = split(c.features);
            data_.push_back({f, c.label, 1.0});
            for (int i = 0; i < variations; ++i) data_.push_back({vary(f, rng), c.label, 1.0});
        }
        builtin_ = data_.size();
        confirmed_ = 0;
        if (kb) for (const auto &r : kb->records())
            if (r.kind == 'D' && r.value == 1 && r.what != "unclear") { data_.push_back({split(r.symptoms), r.what, 6.0}); ++confirmed_; }
        fit();
    }

    std::vector<Prediction> predict(const std::string &symptoms) const { return predictSet(split(symptoms)); }

    // Kreuzvalidierung (jeder 5. Basisfall als Prüffall, ohne dessen Variationen im Training)
    double crossValidate() const {
        int ok = 0, n = 0;
        const auto &cases = builtinTrainingCases();
        for (int fold = 0; fold < 5; ++fold) {
            SymptomModel m;
            std::mt19937 rng(7 + fold);
            for (size_t i = 0; i < cases.size(); ++i) {
                if (int(i % 5) == fold) continue;
                auto f = split(cases[i].features);
                m.data_.push_back({f, cases[i].label, 1.0});
                for (int k = 0; k < 12; ++k) m.data_.push_back({m.vary(f, rng), cases[i].label, 1.0});
            }
            m.fit();
            for (size_t i = size_t(fold); i < cases.size(); i += 5) {
                auto p = m.predictSet(split(cases[i].features));
                ++n; if (!p.empty() && p[0].label == cases[i].label) ++ok;
            }
        }
        return n ? double(ok) / n : 0;
    }

    size_t trainingSize() const { return data_.size(); }
    size_t builtinCases() const { return builtinTrainingCases().size(); }
    size_t confirmedCases() const { return confirmed_; }
    size_t classes() const { return prior_.size(); }

    static std::set<std::string> split(const std::string &s) {
        std::set<std::string> t; std::istringstream in(s); std::string x;
        while (std::getline(in, x, ',')) if (!x.empty()) t.insert(x);
        return t;
    }

private:
    struct Sample { std::set<std::string> f; std::string label; double w; };

    std::set<std::string> vary(const std::set<std::string> &f, std::mt19937 &rng) const {
        // Reale Messungen sind unvollständig/verrauscht: Merkmale fehlen manchmal, selten kommt ein Störmerkmal dazu
        static const char *noise[] = {"slow", "timeout", "readerr", "logfs", "logbad", "conn", "bigcap", "zero0", "logpart"};
        std::uniform_real_distribution<double> u(0, 1);
        std::set<std::string> out;
        for (auto &x : f) if (f.size() == 1 || u(rng) > 0.18) out.insert(x);
        if (out.empty()) out = f;
        if (u(rng) < 0.25) out.insert(noise[rng() % 9]);
        return out;
    }

    void fit() {
        vocab_.clear(); prior_.clear(); count_.clear(); total_.clear();
        for (auto &s : data_) for (auto &x : s.f) vocab_.insert(x);
        double all = 0;
        for (auto &s : data_) { prior_[s.label] += s.w; total_[s.label] += s.w; all += s.w; for (auto &x : s.f) count_[s.label][x] += s.w; }
        for (auto &[k, v] : prior_) v /= all;
    }

    std::vector<Prediction> predictSet(const std::set<std::string> &f) const {
        std::vector<std::pair<std::string, double>> logp;
        for (auto &[label, pr] : prior_) {
            double lp = std::log(pr);
            const auto &cnt = count_.at(label);
            const double tot = total_.at(label);
            for (auto &x : vocab_) {
                auto it = cnt.find(x);
                double c = it == cnt.end() ? 0 : it->second;
                double p = (c + 0.5) / (tot + 1.0);           // Bernoulli mit Glättung
                lp += f.count(x) ? std::log(p) : std::log(1 - p);
            }
            logp.push_back({label, lp});
        }
        double mx = -1e300; for (auto &x : logp) mx = std::max(mx, x.second);
        double sum = 0; for (auto &x : logp) sum += std::exp(x.second - mx);
        std::vector<Prediction> out;
        for (auto &x : logp) out.push_back({x.first, std::exp(x.second - mx) / sum});
        std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.p > b.p; });
        return out;
    }

    std::vector<Sample> data_;
    std::set<std::string> vocab_;
    std::map<std::string, double> prior_, total_;
    std::map<std::string, std::map<std::string, double>> count_;
    size_t builtin_ = 0, confirmed_ = 0;
};

inline const char *hypothesisTitleShort(const std::string &id) {
    static const std::map<std::string, const char *> t = {
        {"not_detected", "Stick nicht erkannt"}, {"controller_safe_mode", "Controller im Notmodus"}, {"nand_readonly", "Speicher verschlissen (nur lesen)"},
        {"windows_readonly", "Schreibschutz durch Windows"}, {"partition_damaged", "Partitionstabelle defekt"}, {"fs_damaged", "Dateisystem defekt"},
        {"bad_sectors", "Defekte Speicherbereiche"}, {"fake_capacity", "Fake-Kapazität"}, {"connection", "Verbindungsproblem"},
        {"card_reader", "Kartenleser"}, {"healthy", "In Ordnung"}};
    auto it = t.find(id);
    return it == t.end() ? "?" : it->second;
}

} // namespace usbr
