// Butchies USB Recovery Studio AI – Qt-Oberfläche und Verbindung zum KI-Agenten
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QIcon>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTimer>
#include <QThread>
#include <QTime>
#include <QMap>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "core/agent.h"

using namespace usbr;

static QString q(const std::string &s) { return QString::fromStdString(s); }
static std::string s(const QString &v) { return v.toStdString(); }

class RecoveryController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString currentTask READ currentTask NOTIFY busyChanged)
    Q_PROPERTY(QVariantList monitor READ monitor NOTIFY monitorChanged)
    Q_PROPERTY(QVariantMap diagnosis READ diagnosis NOTIFY diagnosisChanged)
    Q_PROPERTY(QVariantMap knowledge READ knowledge NOTIFY knowledgeChanged)
    Q_PROPERTY(QVariantMap approval READ approval NOTIFY approvalChanged)
    Q_PROPERTY(QVariantList chat READ chat NOTIFY chatChanged)
    Q_PROPERTY(QString targetDir READ targetDir WRITE setTargetDir NOTIFY optionsChanged)
    Q_PROPERTY(bool stickIsEmpty READ stickIsEmpty WRITE setStickIsEmpty NOTIFY optionsChanged)
    Q_PROPERTY(QString formatFs READ formatFs WRITE setFormatFs NOTIFY optionsChanged)
    Q_PROPERTY(QString llmUrl READ llmUrl WRITE setLlmUrl NOTIFY optionsChanged)
    Q_PROPERTY(QString llmModel READ llmModel WRITE setLlmModel NOTIFY optionsChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)
    Q_PROPERTY(QString caseDir READ caseDir NOTIFY lastResultChanged)
    Q_PROPERTY(QString hostWarning READ hostWarning NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap run READ run NOTIFY runChanged)

public:
    explicit RecoveryController(QObject *parent = nullptr) : QObject(parent) {
        m_work = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(m_work);
        m_kb.load(s(m_work + "/wissensbasis.txt"));
        m_agent = std::make_unique<RecoveryAgent>(m_kb, s(QDir::toNativeSeparators(m_work)));
        m_modelAccuracy = int(m_agent->model().crossValidate() * 100 + 0.5);
        QSettings st;
        m_target = st.value("targetDir").toString();
        m_fs = st.value("formatFs", "exfat").toString();
        m_llmUrl = st.value("llmUrl").toString();
        m_llmModel = st.value("llmModel", "local").toString();
        auto k = m_kb.summary();
        log(LogKind::Info, fmt("KI-Agent aktiv. Wissensbasis: %d Reparaturversuche (%d erfolgreich), %d bestätigte/verworfene Diagnosen, %d Geräte.",
                               k.actions, k.successes, k.diagnoses, k.devices));
        log(LogKind::Info, fmt("Startwissen: %zu Controller-Kennungen, %zu Windows-Ereignisse, %zu Kernel-Muster.",
                               controllerHints().size(), eventCatalog().size(), kernelPatterns().size()));
        updateKnowledge();
        log(LogKind::Lernen, m_agent->modelSummary());
        QTimer::singleShot(200, this, [this] { refresh(); });
        // Neue Sticks automatisch bemerken
        connect(&m_poll, &QTimer::timeout, this, [this] { if (!m_busy) refresh(true); });
        m_poll.start(4000);
    }
    ~RecoveryController() override { m_cancel.cancel(); answerApproval(false); if (m_worker.joinable()) m_worker.join(); }

    QVariantList devices() const { return m_devList; }
    int selected() const { return m_selected; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString currentTask() const { return m_task; }
    QVariantList monitor() const { return m_monitor; }
    QVariantMap diagnosis() const { return m_diag; }
    QVariantMap knowledge() const { return m_knowledge; }
    // Nur aufrufen, wenn kein Hintergrundvorgang läuft (die Wissensbasis gehört dann dem Agenten)
    void updateKnowledge() {
        if (m_busy) return;
        m_knowledge = buildKnowledge();
        emit knowledgeChanged();
    }
    QVariantMap buildKnowledge() const {
        auto k = m_kb.summary();
        QVariantList recent;
        const auto &r = m_kb.records();
        for (int i = int(r.size()) - 1, n = 0; i >= 0 && n < 40; --i, ++n)
            recent << QVariantMap{{"kind", r[size_t(i)].kind == 'A' ? "Versuch" : "Diagnose"}, {"time", q(r[size_t(i)].ts)}, {"device", q(r[size_t(i)].id)},
                                  {"what", q(r[size_t(i)].what)}, {"symptoms", q(r[size_t(i)].symptoms)}, {"ok", r[size_t(i)].value == 1}};
        return {{"modelAccuracy", m_modelAccuracy}, {"modelExamples", int(m_agent->model().trainingSize())}, {"modelConfirmed", int(m_agent->model().confirmedCases())},
                {"actions", k.actions}, {"successes", k.successes}, {"diagnoses", k.diagnoses}, {"devices", k.devices},
                {"controllers", int(controllerHints().size())}, {"recent", recent}, {"path", m_work + "/wissensbasis.txt"}};
    }
    QVariantMap approval() const { return m_approval; }
    QVariantMap run() const {
        return {{"active", m_runActive}, {"title", m_runTitle}, {"steps", m_runSteps}, {"started", m_runStarted}, {"device", m_runDevice}};
    }
    Q_INVOKABLE void clearRun() { if (m_busy) return; m_runSteps.clear(); m_runTitle.clear(); emit runChanged(); }
    QVariantList chat() const { return m_chat; }
    QString targetDir() const { return m_target; }
    void setTargetDir(const QString &v) { m_target = v; QSettings().setValue("targetDir", v); emit optionsChanged(); }
    bool stickIsEmpty() const { return m_empty; }
    void setStickIsEmpty(bool v) { m_empty = v; emit optionsChanged(); }
    QString formatFs() const { return m_fs; }
    void setFormatFs(const QString &v) { m_fs = v; QSettings().setValue("formatFs", v); emit optionsChanged(); }
    QString llmUrl() const { return m_llmUrl; }
    void setLlmUrl(const QString &v) { m_llmUrl = v.trimmed(); QSettings().setValue("llmUrl", m_llmUrl); emit optionsChanged(); }
    QString llmModel() const { return m_llmModel; }
    void setLlmModel(const QString &v) { m_llmModel = v.trimmed(); QSettings().setValue("llmModel", m_llmModel); emit optionsChanged(); }
    QString lastResult() const { return m_lastResult; }
    QString caseDir() const { return q(m_agent->caseDir()); }
    QString hostWarning() const {
        auto h = readHostPolicy();
        if (h.registryWriteProtect) return "Auf diesem PC ist der Registry-Schreibschutz für USB-Datenträger aktiv.";
        if (h.policyDenyWrite) return "Eine Gruppenrichtlinie verbietet Schreibzugriffe auf Wechseldatenträger.";
        return {};
    }

    Q_INVOKABLE void refresh(bool quiet = false) {
        auto list = enumerateUsbDevices();
        for (auto &img : m_images) list.push_back(img);
        bool changed = list.size() != m_devs.size();
        for (size_t i = 0; !changed && i < list.size(); ++i)
            if (list[i].path != m_devs[i].path || list[i].usbInstanceId != m_devs[i].usbInstanceId || list[i].size != m_devs[i].size || list[i].mediaPresent != m_devs[i].mediaPresent) changed = true;
        if (!changed && quiet) return;
        std::string prevId = m_selected >= 0 && size_t(m_selected) < m_devs.size() ? m_devs[size_t(m_selected)].usbInstanceId + m_devs[size_t(m_selected)].path : std::string();
        m_devs = list;
        m_devList.clear();
        int newSel = -1;
        for (size_t i = 0; i < m_devs.size(); ++i) {
            const auto &d = m_devs[i];
            const auto *c = d.hasVidPid ? findController(d.vid, d.pid) : nullptr;
            QString state = !d.isDisk ? "Nicht erkannt" : !d.mediaPresent ? "Kein Medium" : d.readOnlyAttr ? "Schreibgeschützt" : d.size < (64ull << 20) ? "Falsche Größe" : "Bereit";
            QString level = !d.isDisk || !d.mediaPresent ? "bad" : (d.readOnlyAttr || d.size < (64ull << 20)) ? "warn" : "good";
            QStringList letters; for (auto &l : d.driveLetters) letters << q(l);
            m_devList << QVariantMap{{"name", q(d.displayName())}, {"id", q(d.idString())}, {"size", d.mediaPresent ? q(humanSize(d.size)) : QString("–")},
                                     {"path", q(d.path)}, {"letters", letters.join(", ")}, {"state", state}, {"level", level},
                                     {"controller", c ? q(std::string(c->vendor) + " · " + c->family) : QString("unbekannt")},
                                     {"problem", q(problemCodeText(d.problemCode))}, {"serial", q(d.serial)}, {"image", d.imageFile}, {"uas", d.uas}};
            if (!prevId.empty() && d.usbInstanceId + d.path == prevId) newSel = int(i);
        }
        if (!quiet || newSel != m_selected) { m_selected = newSel; emit selectedChanged(); }
        emit devicesChanged();
        if (!quiet) log(LogKind::Messung, fmt("Geräteliste aktualisiert: %zu USB-Datenträger/-Geräte", m_devs.size()));
        else if (changed) log(LogKind::Messung, "Geräteänderung erkannt – Liste aktualisiert");
    }

    Q_INVOKABLE void select(int i) {
        if (i < 0 || size_t(i) >= m_devs.size() || m_busy) return;
        m_selected = i; emit selectedChanged();
        diagnose();
    }

    Q_INVOKABLE void openImage(const QString &path) {
        RawDisk d; std::string e;
        if (!d.open(s(path), false, e)) { setResult("Abbild nicht lesbar: " + q(e)); return; }
        m_images.push_back(imageAsDevice(s(path), d.size()));
        refresh();
        select(int(m_devs.size()) - 1);
    }

    Q_INVOKABLE void diagnose() {
        if (!current()) return;
        UsbDevice dev = *current();
        start("Diagnose", [this, dev] {
            m_agent->diagnose(dev, callbacks(), &m_cancel, true);
            if (dev.imageFile) m_agent->setImage(dev.path);
        });
    }

    Q_INVOKABLE void runAction(const QString &id) {
        if (!current() || m_busy) return;
        RunOptions o = options();
        std::string aid = s(id);
        beginRun(actionTitle(id), {QVariantMap{{"id", id}, {"title", actionTitle(id)}, {"state", 0}, {"info", "wartet"}}});
        start(actionTitle(id), [this, aid, o] {
            PlannedAction a; a.id = aid; a.title = s(actionTitle(q(aid))); a.risk = riskOf(q(aid));
            for (auto &p : m_agent->diagnosis().plan) if (p.id == aid) { Risk r = std::max(a.risk, p.risk); a = p; a.risk = r; }
            if (a.risk >= Risk::Harmlos && !a.manual) {
                stepUpdate(aid, a.title, 1, "wartet auf Freigabe");
                if (!askApproval(a, a.why + "\nRisiko: " + riskName(a.risk))) { log(LogKind::Entscheidung, "Abgelehnt: " + a.title); stepUpdate(aid, a.title, 5, "abgelehnt"); return; }
            }
            stepUpdate(aid, a.title, 1, "läuft");
            auto r = m_agent->execute(aid, o, callbacks(), &m_cancel);
            stepUpdate(aid, a.title, r.ok ? 2 : r.manual ? 6 : (r.executed ? 3 : 4), r.message);
            QMetaObject::invokeMethod(this, [this, r] { setResult(q(r.message)); }, Qt::QueuedConnection);
        });
    }

    Q_INVOKABLE void autoRun() {
        if (!current() || m_busy) return;
        RunOptions o = options();
        UsbDevice dev = *current();
        beginRun("Automatische Reparatur", {});
        start("Automatische Reparatur", [this, o, dev] {
            if (m_agent->diagnosis().hypotheses.empty()) m_agent->diagnose(dev, callbacks(), &m_cancel, true);
            auto res = m_agent->autoRun(o, callbacks(), &m_cancel);
            QString sum; int ok = 0;
            for (auto &r : res) { if (r.ok) ++ok; sum += (r.ok ? "✔ " : r.manual ? "ℹ " : "✖ ") + q(r.message) + "\n"; }
            QMetaObject::invokeMethod(this, [this, sum, ok, n = res.size()] { setResult(QString("%1 von %2 Schritten erfolgreich\n").arg(ok).arg(n) + sum); }, Qt::QueuedConnection);
        });
    }

    Q_INVOKABLE void cancel() { m_cancel.cancel(); answerApproval(false); log(LogKind::Warnung, "Abbruch angefordert"); }
    Q_INVOKABLE void answerApproval(bool yes) {
        std::lock_guard<std::mutex> l(m_amx);
        m_answer = yes ? 1 : 0; m_acv.notify_all();
    }
    Q_INVOKABLE void confirmDiagnosis(const QString &hyp, bool correct) {
        if (m_busy) return;
        m_agent->confirm(s(hyp), correct, callbacks());
        updateKnowledge();
    }
    Q_INVOKABLE void openFolder(const QString &p) { QDesktopServices::openUrl(QUrl::fromLocalFile(p)); }
    Q_INVOKABLE void clearMonitor() { m_monitor.clear(); emit monitorChanged(); }

    Q_INVOKABLE void ask(const QString &question) {
        if (question.trimmed().isEmpty()) return;
        m_chat << QVariantMap{{"from", "du"}, {"text", question}};
        emit chatChanged();
        if (m_llmUrl.isEmpty()) { reply(builtinAnswer(question)); return; }
        QJsonArray msgs;
        msgs.append(QJsonObject{{"role", "system"}, {"content", "Du bist der KI-Assistent von Butchies USB Recovery Studio AI. Antworte auf Deutsch, kurz und verständlich für Laien. "
                                                             "Stütze dich nur auf die gelieferten Messwerte und Befunde. Empfiehl vor schreibenden Schritten immer zuerst die Datensicherung."}});
        msgs.append(QJsonObject{{"role", "user"}, {"content", "Aktueller Befund und Messwerte:\n" + contextText()}});
        for (const auto &c : m_chat) {
            auto m = c.toMap();
            msgs.append(QJsonObject{{"role", m["from"] == "du" ? "user" : "assistant"}, {"content", m["text"].toString()}});
        }
        QJsonObject body{{"model", m_llmModel}, {"messages", msgs}, {"temperature", 0.3}, {"stream", false}};
        QNetworkRequest req{QUrl(m_llmUrl)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(120000);
        auto *r = m_net.post(req, QJsonDocument(body).toJson());
        connect(r, &QNetworkReply::finished, this, [this, r, question] {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) { reply("Lokale KI nicht erreichbar (" + r->errorString() + "). Eingebaute Antwort:\n\n" + builtinAnswer(question)); return; }
            auto o = QJsonDocument::fromJson(r->readAll()).object();
            QString t = o["choices"].toArray().at(0).toObject()["message"].toObject()["content"].toString();
            reply(t.isEmpty() ? builtinAnswer(question) : t.trimmed());
        });
    }

signals:
    void devicesChanged();
    void selectedChanged();
    void busyChanged();
    void progressChanged();
    void monitorChanged();
    void diagnosisChanged();
    void knowledgeChanged();
    void approvalChanged();
    void chatChanged();
    void optionsChanged();
    void lastResultChanged();
    void runChanged();

private:
    const UsbDevice *current() const { return m_selected >= 0 && size_t(m_selected) < m_devs.size() ? &m_devs[size_t(m_selected)] : nullptr; }
    RunOptions options() const { RunOptions o; o.targetDir = s(QDir::toNativeSeparators(m_target)); o.stickIsEmpty = m_empty; o.formatFs = s(m_fs); return o; }

    QString actionTitle(const QString &id) const {
        for (auto &p : m_agent->diagnosis().plan) if (q(p.id) == id) return q(p.title);
        static const QMap<QString, QString> t{{"image", "Rettungsabbild"}, {"file_rescue", "Dateien retten"}, {"carve", "Signatursuche"}, {"surface_scan", "Oberflächentest"},
                                               {"capacity_test", "Kapazitätstest"}, {"clear_readonly", "Schreibschutzattribut entfernen"}, {"restart_device", "USB-Gerät neu starten"},
                                               {"restore_boot", "Bootsektor wiederherstellen"}, {"rebuild_mbr", "Partitionstabelle neu schreiben"}, {"fix_fake", "Auf echte Größe einrichten"},
                                               {"format_fat32", "Formatieren FAT32"}, {"format_exfat", "Formatieren exFAT"}, {"write_test", "Schreibtest"}, {"clear_host_policy", "Registry-Schreibschutz zurücksetzen"}};
        return t.value(id, id);
    }
    static Risk riskOf(const QString &id) {
        if (id == "image" || id == "file_rescue" || id == "carve" || id == "surface_scan") return Risk::Lesend;
        if (id == "clear_readonly" || id == "restart_device" || id == "clear_host_policy") return Risk::Harmlos;
        if (id == "fix_fake" || id.startsWith("format")) return Risk::Loeschend;
        return Risk::Mittel;
    }

    void start(const QString &task, std::function<void()> fn) {
        if (m_busy) return;
        if (m_worker.joinable()) m_worker.join();
        m_cancel.reset();
        m_busy = true; m_task = task; m_progress = 0; m_progressText = task;
        emit busyChanged(); emit progressChanged();
        m_worker = std::thread([this, fn] {
            fn();
            QMetaObject::invokeMethod(this, [this] {
                m_busy = false; m_task.clear(); m_progress = 100;
                if (m_runActive) {
                    m_runActive = false;
                    for (auto &v : m_runSteps) { auto m = v.toMap(); if (m["state"].toInt() == 0) { m["state"] = 4; m["info"] = "nicht mehr nötig"; v = m; }
                                                 else if (m["state"].toInt() == 1) { m["state"] = 3; m["info"] = "abgebrochen"; v = m; } }
                    emit runChanged();
                }
                emit busyChanged(); emit progressChanged(); emit lastResultChanged();
                updateKnowledge();
            }, Qt::QueuedConnection);
        });
    }

    void beginRun(const QString &title, const QVariantList &steps) {
        m_runActive = true; m_runTitle = title; m_runSteps = steps;
        m_runStarted = double(QDateTime::currentMSecsSinceEpoch());
        m_runDevice = current() ? q(current()->displayName()) : QString();
        emit runChanged();
    }
    // Schritt anlegen oder aktualisieren (thread-sicher über die Ereignisschleife)
    void stepUpdate(const std::string &id, const std::string &title, int st, const std::string &info) {
        QMetaObject::invokeMethod(this, [this, id = q(id), title = q(title), st, info = q(info)] {
            for (auto &v : m_runSteps) {
                auto m = v.toMap();
                if (m["id"].toString() != id) continue;
                if (st == 0 && m["state"].toInt() != 0) return;   // Neuplanung überschreibt keinen laufenden/fertigen Schritt
                m["state"] = st; m["info"] = info.left(120); m["title"] = title; v = m;
                emit runChanged(); return;
            }
            m_runSteps << QVariantMap{{"id", id}, {"title", title}, {"state", st}, {"info", info.left(120)}};
            emit runChanged();
        }, Qt::QueuedConnection);
    }

    bool askApproval(const PlannedAction &a, const std::string &details) {
        {
            std::lock_guard<std::mutex> l(m_amx); m_answer = -1;
        }
        QVariantMap m{{"id", q(a.id)}, {"title", q(a.title)}, {"details", q(details)}, {"risk", int(a.risk)}, {"riskText", riskName(a.risk)}};
        QMetaObject::invokeMethod(this, [this, m] { m_approval = m; emit approvalChanged(); }, Qt::QueuedConnection);
        std::unique_lock<std::mutex> l(m_amx);
        m_acv.wait(l, [this] { return m_answer >= 0; });
        bool yes = m_answer == 1 && !m_cancel.cancelled();
        QMetaObject::invokeMethod(this, [this] { m_approval.clear(); emit approvalChanged(); }, Qt::QueuedConnection);
        return yes;
    }

    AgentCallbacks callbacks() {
        AgentCallbacks cb;
        cb.log = [this](LogKind k, const std::string &t) { log(k, t); };
        cb.progress = [this](double p, const std::string &t) {
            const uint64_t now = nowMs();
            if (now - m_lastProg < 150 && p < 100 && p >= 0) return;   // Oberfläche nicht fluten
            m_lastProg = now;
            QMetaObject::invokeMethod(this, [this, p, txt = q(t)] { if (p >= 0) m_progress = p; m_progressText = txt; emit progressChanged(); }, Qt::QueuedConnection);
        };
        cb.approve = [this](const PlannedAction &a, const std::string &d) { return askApproval(a, d); };
        cb.step = [this](const std::string &id, const std::string &title, int st, const std::string &info) { stepUpdate(id, title, st, info); };
        cb.diagnosisChanged = [this](const Diagnosis &d, const DeviceFacts &f) {
            QVariantMap m = toMap(d, f);
            QMetaObject::invokeMethod(this, [this, m] { m_diag = m; emit diagnosisChanged(); }, Qt::QueuedConnection);
        };
        return cb;
    }

    void log(LogKind k, const std::string &t) {
        QVariantMap e{{"kind", logKindName(k)}, {"text", q(t)}, {"time", QTime::currentTime().toString("HH:mm:ss")}};
        auto add = [this, e] { m_monitor << e; while (m_monitor.size() > 3000) m_monitor.removeFirst(); emit monitorChanged(); };
        if (QThread::currentThread() == thread()) add();
        else QMetaObject::invokeMethod(this, add, Qt::QueuedConnection);
    }

    static QVariantMap toMap(const Diagnosis &d, const DeviceFacts &f) {
        QVariantList hyps, plan, logs, facts;
        for (auto &h : d.hypotheses) {
            QStringList ev; for (auto &e : h.evidence) ev << q(e);
            hyps << QVariantMap{{"id", q(h.id)}, {"title", q(h.title)}, {"confidence", int(h.confidence * 100 + 0.5)}, {"explanation", q(h.explanation)},
                                {"evidence", ev}, {"dataChance", q(h.dataChance)}, {"dataPercent", h.dataPercent}, {"repair", q(h.repair)}, {"repairable", h.repairable}};
        }
        for (auto &a : d.plan)
            plan << QVariantMap{{"id", q(a.id)}, {"title", q(a.title)}, {"why", q(a.why)}, {"risk", int(a.risk)}, {"riskText", a.manual ? QString("Anleitung") : QString(riskName(a.risk))},
                                {"manual", a.manual}, {"needsImage", a.needsImage}, {"needsTarget", a.needsTarget},
                                {"learned", a.learnedTries ? QString("%1 von %2 erfolgreich").arg(int(a.learnedRate * a.learnedTries + 0.5)).arg(a.learnedTries) : QString()}};
        for (auto &e : f.logs.entries)
            logs << QVariantMap{{"time", q(e.time)}, {"source", q(e.source)}, {"id", e.id}, {"category", q(e.category)}, {"weight", e.weight},
                                {"meaning", q(e.meaning)}, {"raw", q(e.raw)}, {"matched", e.matched}};
        for (auto &x : d.facts) facts << q(x);
        QVariantList model;
        for (size_t i = 0; i < d.model.size() && i < 3; ++i) model << QVariantMap{{"title", hypothesisTitleShort(d.model[i].label)}, {"p", int(d.model[i].p * 100 + 0.5)}};
        const Hypothesis *t = d.top();
        QVariantList curve; if (f.scan) for (double v : f.scan->speedCurve) curve << v;
        return {{"valid", t != nullptr}, {"headline", q(d.headline)}, {"topId", t ? q(t->id) : QString()}, {"confidence", t ? int(t->confidence * 100 + 0.5) : 0},
                {"explanation", t ? q(t->explanation) : QString()}, {"dataChance", t ? q(t->dataChance) : QString()}, {"dataPercent", d.dataPercent},
                {"repair", t ? q(t->repair) : QString()}, {"repairable", d.repairable}, {"hypotheses", hyps}, {"plan", plan}, {"logs", logs}, {"facts", facts},
                {"symptoms", q(d.symptoms)}, {"imageAvailable", f.imageAvailable}, {"imagePath", q(f.imagePath)},
                {"fake", f.capacity && f.capacity->fake}, {"realSize", f.capacity ? q(humanSize(f.capacity->real)) : QString()}, {"speedCurve", curve},
                {"device", q(f.dev.displayName())}, {"deviceId", q(f.dev.idString())}, {"model", model}};
    }

    QString contextText() const {
        QString t;
        t += "Gerät: " + m_diag["device"].toString() + " (" + m_diag["deviceId"].toString() + ")\n";
        t += "Befund: " + m_diag["headline"].toString() + QString(" (%1 %)\n").arg(m_diag["confidence"].toInt());
        for (auto &h : m_diag["hypotheses"].toList()) {
            auto m = h.toMap();
            t += QString("- %1: %2 % – Belege: %3\n").arg(m["title"].toString()).arg(m["confidence"].toInt()).arg(m["evidence"].toStringList().join("; "));
        }
        for (auto &f : m_diag["facts"].toList()) t += "Messwert: " + f.toString() + "\n";
        int n = 0;
        for (auto &l : m_diag["logs"].toList()) { auto m = l.toMap(); if (m["matched"].toBool() && n++ < 15) t += "Protokoll: " + m["time"].toString() + " " + m["source"].toString() + " – " + m["meaning"].toString() + "\n"; }
        for (auto &p : m_diag["plan"].toList()) t += "Geplanter Schritt: " + p.toMap()["title"].toString() + "\n";
        return t;
    }

    QString builtinAnswer(const QString &question) const {
        if (!m_diag["valid"].toBool()) return "Wähle zuerst links einen Stick aus – ich untersuche ihn dann automatisch und erkläre dir das Ergebnis.";
        QString ql = question.toLower();
        auto hyps = m_diag["hypotheses"].toList();
        auto top = hyps.isEmpty() ? QVariantMap() : hyps.first().toMap();
        if (ql.contains("daten") || ql.contains("retten") || ql.contains("fotos") || ql.contains("bilder"))
            return "Datenrettung: " + m_diag["dataChance"].toString() + QString(" (Chance etwa %1 %).").arg(m_diag["dataPercent"].toInt()) +
                   "\nMein Vorschlag: zuerst „Rettungsabbild“, danach „Dateien retten“ in einen Ordner auf deinem PC. Reicht das nicht, findet die „Signatursuche“ Fotos, Videos und Dokumente auch ohne Dateisystem.";
        if (ql.contains("reparier") || ql.contains("wieder") || ql.contains("geht") || ql.contains("formatier"))
            return "Reparatur: " + m_diag["repair"].toString() + (m_diag["repairable"].toBool() ? "\nMit „Automatisch reparieren“ gehe ich die Schritte von harmlos nach riskant durch und frage dich vor jedem Schreibzugriff." : "");
        if (ql.contains("warum") || ql.contains("woher") || ql.contains("beleg") || ql.contains("sicher"))
            return "Darauf stütze ich mich:\n• " + top["evidence"].toStringList().join("\n• ");
        if (ql.contains("protokoll") || ql.contains("fehler") || ql.contains("ereignis")) {
            QString t; int n = 0;
            for (auto &l : m_diag["logs"].toList()) { auto m = l.toMap(); if (n++ < 6) t += "• " + m["time"].toString() + " – " + m["meaning"].toString() + "\n"; }
            return t.isEmpty() ? "In den Windows-Protokollen habe ich zu diesem Stick nichts Auffälliges gefunden." : "Das steht in den Protokollen:\n" + t;
        }
        if (ql.contains("fake") || ql.contains("größe") || ql.contains("kapazit"))
            return m_diag["fake"].toBool() ? "Der Stick ist gefälscht: echt sind nur etwa " + m_diag["realSize"].toString() + ". Mit „Auf echte Kapazität einrichten“ wird er in dieser Größe zuverlässig nutzbar."
                                           : "Ob die Größe echt ist, zeigt der Kapazitätstest. Er ist erst nach einem Rettungsabbild oder mit der Bestätigung „Stick ist leer“ freigeschaltet, weil er sonst bei Fake-Sticks Daten überschreiben könnte.";
        return m_diag["headline"].toString() + QString(" (Sicherheit %1 %).\n\n").arg(m_diag["confidence"].toInt()) + m_diag["explanation"].toString() +
               "\n\nFrag mich z. B.: „Kann ich meine Daten retten?“, „Warum glaubst du das?“, „Was steht in den Protokollen?“ oder „Kann man ihn reparieren?“";
    }
    void reply(const QString &t) { m_chat << QVariantMap{{"from", "ki"}, {"text", t}}; emit chatChanged(); }
    void setResult(const QString &r) { m_lastResult = r; emit lastResultChanged(); }

    QString m_work, m_target, m_fs, m_llmUrl, m_llmModel, m_lastResult, m_task, m_progressText;
    KnowledgeStore m_kb;
    std::unique_ptr<RecoveryAgent> m_agent;
    std::vector<UsbDevice> m_devs, m_images;
    QVariantList m_devList, m_monitor, m_chat;
    QVariantMap m_diag, m_approval, m_knowledge;
    int m_selected = -1;
    bool m_runActive = false;
    QString m_runTitle, m_runDevice;
    QVariantList m_runSteps;
    double m_runStarted = 0;
    int m_modelAccuracy = 0;
    bool m_busy = false, m_empty = false;
    double m_progress = 0;
    uint64_t m_lastProg = 0;
    CancelToken m_cancel;
    std::thread m_worker;
    std::mutex m_amx;
    std::condition_variable m_acv;
    int m_answer = -1;
    QNetworkAccessManager m_net;
    QTimer m_poll;
};

int main(int argc, char *argv[]) {
    QQuickStyle::setStyle("Basic");   // eigenes Blau/Weiß-Design statt Windows-Stil
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Butchies");
    app.setApplicationName("Butchies USB Recovery Studio AI");
    app.setWindowIcon(QIcon(":/qt/qml/UsbRecovery/icons/logo.svg"));
    RecoveryController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("UsbRecovery", "Main");
    // Für den Build: echtes Bildschirmfoto der Oberfläche speichern und beenden
    if (qEnvironmentVariableIsSet("USBR_SCREENSHOT")) {
        const int delay = qEnvironmentVariableIntValue("USBR_SCREENSHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("USBR_SCREENSHOT_DELAY") : 7000;
        QTimer::singleShot(delay, &app, [&engine, &app] {
            if (!engine.rootObjects().isEmpty())
                if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) w->grabWindow().save(qEnvironmentVariable("USBR_SCREENSHOT"));
            app.quit();
        });
    }
    return app.exec();
}

#include "main.moc"
