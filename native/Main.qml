import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Butchies USB Recovery Studio AI – Blau/Weiß-Design
// Aufbau: blaue Seitenleiste · weiße Kopfzeile · Arbeitsfläche · KI-Assistent rechts
ApplicationWindow {
    id: window
    width: 1480; height: 920
    minimumWidth: 1180; minimumHeight: 720
    visible: true
    title: "Butchies USB Recovery Studio AI"
    color: theme.bg
    required property QtObject controller

    QtObject {
        id: theme
        readonly property color bg: "#f3f7fd"
        readonly property color card: "#ffffff"
        readonly property color line: "#e3ebf7"
        readonly property color text: "#1e293b"
        readonly property color strong: "#0b1b3f"
        readonly property color muted: "#64748b"
        readonly property color blue: "#1d4ed8"
        readonly property color blue2: "#2563eb"
        readonly property color sky: "#3b82f6"
        readonly property color soft: "#eaf1ff"
        readonly property color softer: "#f5f8ff"
        readonly property color blueText: "#1e40af"
        readonly property color good: "#16a34a"
        readonly property color goodSoft: "#e7f7ee"
        readonly property color warn: "#d97706"
        readonly property color warnSoft: "#fff4e0"
        readonly property color bad: "#dc2626"
        readonly property color badSoft: "#fdecec"
        readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
    }
    font.family: interRegular.status === FontLoader.Ready ? interRegular.name : "Segoe UI"
    font.pixelSize: 14
    palette.window: theme.bg
    palette.base: "#ffffff"
    palette.button: "#ffffff"
    palette.buttonText: "#334155"
    palette.text: theme.text
    palette.windowText: theme.text
    palette.highlight: theme.blue2

    FontLoader { id: interRegular; source: "fonts/Inter-Regular.otf" }
    FontLoader { source: "fonts/Inter-SemiBold.otf" }
    FontLoader { source: "fonts/Inter-Bold.otf" }

    property int page: 0
    readonly property var diag: controller.diagnosis || ({})
    readonly property bool hasDiag: diag.valid === true
    readonly property var dev: controller.selected >= 0 && controller.selected < controller.devices.length ? controller.devices[controller.selected] : null
    readonly property var pages: [
        { title: "Übersicht", sub: "Alle USB-Sticks auf einen Blick", icon: "home" },
        { title: "Diagnose", sub: "Was mit dem Stick los ist – mit Belegen", icon: "spark" },
        { title: "Datenrettung", sub: "Erst sichern, dann reparieren", icon: "save" },
        { title: "Reparatur", sub: "Von harmlos nach riskant – du gibst jeden Schreibschritt frei", icon: "tool" },
        { title: "KI-Live-Monitor", sub: "Was die KI misst, denkt, entscheidet und lernt", icon: "chip" },
        { title: "Protokolle", sub: "Windows-Ereignisse – übersetzt und dem Stick zugeordnet", icon: "folder" },
        { title: "Wissensbasis", sub: "Was die KI bisher gelernt hat", icon: "box" },
        { title: "Einstellungen", sub: "Lokale KI und Sicherheit", icon: "refresh" }
    ]

    function levelColor(l) { return l === "bad" ? theme.bad : l === "warn" ? theme.warn : l === "good" ? theme.good : l === "blue" ? theme.blue2 : "#94a3b8" }
    function levelSoft(l) { return l === "bad" ? theme.badSoft : l === "warn" ? theme.warnSoft : l === "good" ? theme.goodSoft : l === "blue" ? theme.soft : "#eef2f7" }
    function levelText(l) { return l === "bad" ? "#b91c1c" : l === "warn" ? "#b45309" : l === "good" ? "#15803d" : l === "blue" ? theme.blueText : "#475569" }
    function icon(name, color) { return "icons/" + name + "-" + (color || "grey") + ".svg" }
    function riskLevel(r) { return r <= 1 ? "good" : r === 2 ? "warn" : "bad" }
    function topLevel() {
        if (!hasDiag) return "blue"
        if (diag.topId === "healthy") return "good"
        if (diag.dataPercent >= 70) return "warn"
        return "bad"
    }
    function kindLevel(k) {
        return k === "Fehler" ? "bad" : k === "Warnung" ? "warn" : (k === "Hypothese" || k === "Entscheidung") ? "blue" : (k === "Prüfung" || k === "Lernen") ? "good" : "grey"
    }
    function stepState(i) {
        // 0 Erkennen · 1 Diagnose · 2 Retten · 3 Reparieren  →  0 offen, 1 aktuell, 2 erledigt
        if (i === 0) return window.dev ? 2 : 1
        if (i === 1) return hasDiag ? 2 : (window.dev ? 1 : 0)
        if (i === 2) return hasDiag && diag.imageAvailable ? 2 : (hasDiag ? 1 : 0)
        return hasDiag && diag.topId === "healthy" ? 2 : (hasDiag ? 1 : 0)
    }
    function fileUrlToPath(u) {
        var p = decodeURIComponent(u.toString().replace(/^file:\/\//, ""))
        if (Qt.platform.os === "windows") p = p.replace(/^\//, "").replace(/\//g, "\\")
        return p
    }

    // ================================================================ Bausteine
    component Card: Rectangle {
        default property alias content: inner.data
        property alias spacing: inner.spacing
        property int pad: 20
        color: theme.card; radius: 18; border.color: theme.line
        implicitHeight: inner.implicitHeight + pad * 2
        Layout.fillWidth: true
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: 18; color: "#0b1b3f"; opacity: 0.04 }
        ColumnLayout { id: inner; anchors.fill: parent; anchors.margins: parent.pad; spacing: 12 }
    }
    component Heading: RowLayout {
        id: hd
        property string text
        property string iconName: ""
        property string sub: ""
        spacing: 10
        Layout.fillWidth: true
        Rectangle { visible: hd.iconName !== ""; width: 32; height: 32; radius: 10; color: theme.soft
            Image { anchors.centerIn: parent; source: hd.iconName ? window.icon(hd.iconName, "blue") : ""; sourceSize: Qt.size(17, 17) } }
        ColumnLayout { spacing: 0; Layout.fillWidth: true
            Label { text: hd.text; font.pixelSize: 15; font.bold: true; color: theme.strong }
            Label { visible: hd.sub !== ""; text: hd.sub; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
    }
    component Pill: Rectangle {
        property string text
        property string level: "grey"
        radius: 999; color: window.levelSoft(level)
        implicitWidth: pillLabel.implicitWidth + 20; implicitHeight: 24
        Label { id: pillLabel; anchors.centerIn: parent; text: parent.text; font.pixelSize: 12; font.bold: true; color: window.levelText(parent.level) }
    }
    component ActionButton: Button {
        id: ab
        property string iconName: ""
        property bool primary: false
        property bool danger: false
        implicitHeight: 40
        leftPadding: 16; rightPadding: 16
        hoverEnabled: true
        contentItem: RowLayout {
            spacing: 8
            Image { visible: ab.iconName !== ""; source: ab.iconName ? window.icon(ab.iconName, ab.primary ? "white" : (ab.danger ? "red" : "blue")) : ""; sourceSize: Qt.size(16, 16) }
            Label { text: ab.text; font.bold: true; color: ab.primary ? "#ffffff" : (ab.danger ? "#b91c1c" : theme.blueText); opacity: ab.enabled ? 1 : 0.5 }
        }
        background: Rectangle {
            radius: 11
            gradient: ab.primary ? btnGrad : null
            color: ab.primary ? theme.blue2 : (ab.down ? "#e0eaff" : (ab.hovered ? theme.softer : "#ffffff"))
            border.color: ab.primary ? "transparent" : (ab.danger ? "#fbc4c4" : "#cfdcf5")
            opacity: ab.enabled ? 1 : 0.55
            Gradient { id: btnGrad; GradientStop { position: 0; color: ab.down ? "#1e40af" : theme.sky } GradientStop { position: 1; color: ab.down ? "#1e3a8a" : theme.blue } }
        }
    }
    component Bar: Rectangle {
        property real value: 0
        property color barColor: theme.blue2
        implicitHeight: 8; radius: 4; color: "#e8eef9"
        Rectangle { width: parent.width * Math.max(0, Math.min(1, parent.value / 100)); height: parent.height; radius: 4; color: parent.barColor
            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } } }
    }
    component Ring: Canvas {
        id: ring
        property real value: 0
        property color ringColor: theme.blue2
        property string caption: ""
        implicitWidth: 120; implicitHeight: 120
        onValueChanged: requestPaint()
        onRingColorChanged: requestPaint()
        onPaint: {
            var c = getContext("2d"); c.reset()
            var r = Math.min(width, height) / 2 - 8
            c.lineWidth = 11; c.lineCap = "round"
            c.strokeStyle = "#e8eef9"; c.beginPath(); c.arc(width / 2, height / 2, r, 0, 2 * Math.PI); c.stroke()
            c.strokeStyle = ringColor; c.beginPath()
            c.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * Math.max(0, Math.min(1, value / 100))); c.stroke()
        }
        ColumnLayout { anchors.centerIn: parent; spacing: 0
            Label { text: Math.round(ring.value) + " %"; font.pixelSize: 24; font.bold: true; color: theme.strong; Layout.alignment: Qt.AlignHCenter }
            Label { text: ring.caption; font.pixelSize: 11; color: theme.muted; Layout.alignment: Qt.AlignHCenter } }
    }
    component Tile: Rectangle {
        id: tile
        property string label
        property string value
        property string note
        property string level: "blue"
        property string iconName: "spark"
        property real fill: -1
        Layout.fillWidth: true
        implicitHeight: 126; radius: 18; color: theme.card; border.color: theme.line
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 4
            RowLayout { Rectangle { width: 28; height: 28; radius: 9; color: window.levelSoft(tile.level)
                    Image { anchors.centerIn: parent; source: window.icon(tile.iconName, tile.level === "bad" ? "red" : tile.level === "warn" ? "amber" : tile.level === "good" ? "green" : "blue"); sourceSize: Qt.size(15, 15) } }
                Label { text: tile.label; color: theme.muted; font.pixelSize: 13 } }
            Label { text: tile.value; font.pixelSize: 22; font.bold: true; color: theme.strong; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: tile.note; visible: tile.note !== ""; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
            Bar { visible: tile.fill >= 0; Layout.fillWidth: true; value: tile.fill; barColor: window.levelColor(tile.level) }
        }
    }
    component NavItem: Rectangle {
        id: nav
        property int index
        property string badge: ""
        readonly property bool active: window.page === index
        Layout.fillWidth: true
        implicitHeight: 44; radius: 12
        color: active ? "#ffffff" : (hover.hovered ? "#1fffffff" : "transparent")
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 12
            Image { source: window.icon(window.pages[nav.index].icon, nav.active ? "blue" : "white"); sourceSize: Qt.size(18, 18) }
            Label { text: window.pages[nav.index].title; Layout.fillWidth: true; font.bold: nav.active; color: nav.active ? theme.blue : "#ffffff" }
            Rectangle { visible: nav.badge !== ""; radius: 999; color: nav.active ? theme.soft : "#33ffffff"; implicitWidth: bl.implicitWidth + 14; implicitHeight: 20
                Label { id: bl; anchors.centerIn: parent; text: nav.badge; font.pixelSize: 11; font.bold: true; color: nav.active ? theme.blue : "#ffffff" } }
        }
        TapHandler { onTapped: window.page = nav.index }
    }
    component Step: ColumnLayout {
        id: st
        property int stepIndex
        property string label
        readonly property int stepState: window.stepState(stepIndex)
        spacing: 6
        Rectangle { Layout.alignment: Qt.AlignHCenter; width: 40; height: 40; radius: 20
            color: st.stepState > 0 ? "#ffffff" : "#26ffffff"
            border.color: "#bfdbfe"; border.width: st.stepState === 1 ? 4 : 0
            Label { anchors.centerIn: parent; text: st.stepState === 2 ? "✓" : String(st.stepIndex + 1); font.bold: true; font.pixelSize: 16; color: st.stepState === 0 ? "#ffffff" : theme.blue } }
        Label { Layout.alignment: Qt.AlignHCenter; text: st.label; color: "#ffffff"; font.bold: st.stepState === 1; opacity: st.stepState === 0 ? 0.75 : 1 }
    }

    // ================================================================ Dialoge
    FolderDialog {
        id: targetDialog
        title: "Zielordner für gerettete Daten (nicht auf dem defekten Stick!)"
        onAccepted: controller.targetDir = window.fileUrlToPath(selectedFolder)
    }
    FileDialog {
        id: imageDialog
        title: "Abbilddatei öffnen"
        nameFilters: ["Abbilder (*.img *.dd *.bin *.raw *.iso)", "Alle Dateien (*)"]
        onAccepted: { controller.openImage(window.fileUrlToPath(selectedFile)); window.page = 1 }
    }
    Popup {
        id: approvalPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 580; padding: 0
        modal: true; focus: true
        closePolicy: Popup.NoAutoClose
        visible: controller.approval.id !== undefined
        Overlay.modal: Rectangle { color: "#800b1b3f" }
        background: Rectangle { radius: 20; color: "#ffffff" }
        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 74; radius: 20
                gradient: Gradient { orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: controller.approval.risk === 3 ? "#b91c1c" : theme.blue }
                    GradientStop { position: 1; color: controller.approval.risk === 3 ? "#ef4444" : theme.sky } }
                RowLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                    Image { source: window.icon("warn", "white"); sourceSize: Qt.size(26, 26) }
                    Label { text: "Freigabe nötig"; font.pixelSize: 20; font.bold: true; color: "#ffffff" } }
            }
            ColumnLayout { Layout.margins: 22; spacing: 12
                Label { text: controller.approval.title || ""; font.pixelSize: 17; font.bold: true; color: theme.strong; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Pill { text: controller.approval.riskText || ""; level: window.riskLevel(controller.approval.risk) }
                Label { text: controller.approval.details || ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Rectangle { visible: controller.approval.risk === 3; Layout.fillWidth: true; radius: 12; color: theme.badSoft; implicitHeight: wl.implicitHeight + 20
                    Label { id: wl; anchors.fill: parent; anchors.margins: 10; wrapMode: Text.WordWrap; color: "#b91c1c"; font.bold: true
                        text: "Dieser Schritt löscht alle Daten auf dem Stick. Nur fortfahren, wenn die Daten gesichert oder unwichtig sind." } }
                RowLayout { Layout.alignment: Qt.AlignRight; spacing: 10; Layout.topMargin: 6
                    ActionButton { text: "Ablehnen"; onClicked: controller.answerApproval(false) }
                    ActionButton { text: "Freigeben"; primary: controller.approval.risk !== 3; danger: controller.approval.risk === 3; onClicked: controller.answerApproval(true) } }
            }
        }
    }

    // ================================================================ Startbild
    Rectangle {
        id: splash
        parent: Overlay.overlay; anchors.fill: parent; z: 10000
        visible: opacity > 0
        gradient: Gradient { GradientStop { position: 0; color: "#1e3a8a" } GradientStop { position: 1; color: "#2563eb" } }
        Rectangle { width: 520; height: 520; radius: 260; x: -160; y: -200; color: "#ffffff"; opacity: 0.06 }
        Rectangle { width: 420; height: 420; radius: 210; x: parent.width - 260; y: parent.height - 240; color: "#ffffff"; opacity: 0.06 }
        MouseArea { anchors.fill: parent; onClicked: splashOut.start() }
        ColumnLayout { anchors.centerIn: parent; spacing: 18
            Image { id: splashLogo; Layout.alignment: Qt.AlignHCenter; source: "icons/logo.svg"; sourceSize: Qt.size(320, 320); Layout.preferredWidth: 150; Layout.preferredHeight: 150; scale: 0.3; opacity: 0 }
            RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 10; opacity: splashLogo.opacity
                Label { text: "Butchies USB Recovery Studio"; font.pixelSize: 34; font.bold: true; color: "#ffffff" }
                Label { text: "AI"; font.pixelSize: 34; font.bold: true; color: "#93c5fd" } }
            Label { Layout.alignment: Qt.AlignHCenter; text: "Erkennen · Retten · Reparieren"; color: "#dbeafe"; font.pixelSize: 15; opacity: splashLogo.opacity }
            Rectangle { Layout.alignment: Qt.AlignHCenter; width: 240; height: 4; radius: 2; color: "#33ffffff"; opacity: splashLogo.opacity
                Rectangle { id: splashFill; width: 0; height: 4; radius: 2; color: "#ffffff" } }
        }
        SequentialAnimation {
            running: true
            ParallelAnimation {
                NumberAnimation { target: splashLogo; property: "opacity"; to: 1; duration: 500 }
                NumberAnimation { target: splashLogo; property: "scale"; to: 1; duration: 900; easing.type: Easing.OutBack; easing.overshoot: 1.8 }
            }
            NumberAnimation { target: splashFill; property: "width"; to: 240; duration: 1300; easing.type: Easing.InOutQuad }
            ScriptAction { script: splashOut.start() }
        }
        NumberAnimation { id: splashOut; target: splash; property: "opacity"; to: 0; duration: 450 }
    }

    // ================================================================ Hauptlayout
    RowLayout {
        anchors.fill: parent; spacing: 0

        // ------------------------------------------------ Seitenleiste (blau)
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 248
            clip: true
            gradient: Gradient { GradientStop { position: 0; color: "#1e40af" } GradientStop { position: 1; color: "#1e3a8a" } }
            Rectangle { width: 300; height: 300; radius: 150; x: -120; y: parent.height - 220; color: "#ffffff"; opacity: 0.05 }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 4
                RowLayout { Layout.margins: 6; Layout.bottomMargin: 18; spacing: 10
                    Image { source: "icons/logo.svg"; sourceSize: Qt.size(84, 84); Layout.preferredWidth: 42; Layout.preferredHeight: 42 }
                    ColumnLayout { spacing: 0
                        Label { text: "USB Recovery"; font.pixelSize: 17; font.bold: true; color: "#ffffff" }
                        Label { text: "Studio AI · Butchies"; font.pixelSize: 12; color: "#bfdbfe" } } }
                NavItem { index: 0; badge: controller.devices.length > 0 ? String(controller.devices.length) : "" }
                NavItem { index: 1 }
                NavItem { index: 2 }
                NavItem { index: 3; badge: window.hasDiag && diag.plan.length > 0 ? String(diag.plan.length) : "" }
                NavItem { index: 4 }
                NavItem { index: 5; badge: window.hasDiag && diag.logs.length > 0 ? String(diag.logs.length) : "" }
                NavItem { index: 6 }
                NavItem { index: 7 }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true; radius: 14; color: "#1affffff"; border.color: "#33ffffff"
                    implicitHeight: selCol.implicitHeight + 24
                    ColumnLayout { id: selCol; anchors.fill: parent; anchors.margins: 12; spacing: 3
                        Label { text: window.dev ? "AUSGEWÄHLT" : "KEIN STICK GEWÄHLT"; color: "#bfdbfe"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1 }
                        Label { visible: window.dev !== null; text: window.dev ? window.dev.name : ""; font.bold: true; color: "#ffffff"; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { visible: window.dev !== null; text: window.dev ? window.dev.id + " · " + window.dev.size : ""; color: "#dbeafe"; font.pixelSize: 12 }
                        RowLayout { visible: window.hasDiag; spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: window.levelColor(window.topLevel()) }
                            Label { text: window.hasDiag ? diag.headline : ""; color: "#ffffff"; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true } }
                    }
                }
            }
        }

        // ------------------------------------------------ Mitte
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0

            Rectangle {   // Kopfzeile
                Layout.fillWidth: true; implicitHeight: 76; color: "#ffffff"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 26; anchors.rightMargin: 22; spacing: 16
                    ColumnLayout { spacing: 2
                        Label { text: window.pages[window.page].title; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        Label { text: window.pages[window.page].sub; color: theme.muted; font.pixelSize: 13 } }
                    Item { Layout.fillWidth: true }
                    ColumnLayout {
                        visible: controller.busy; Layout.preferredWidth: 380; spacing: 4
                        RowLayout { Label { text: controller.currentTask; font.bold: true; color: theme.strong; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: Math.round(controller.progress) + " %"; font.bold: true; color: theme.blue2 } }
                        Bar { Layout.fillWidth: true; value: controller.progress }
                        Label { text: controller.progressText; color: theme.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true } }
                    ActionButton { visible: controller.busy; text: "Abbrechen"; iconName: "x"; danger: true; onClicked: controller.cancel() }
                    Rectangle { visible: !controller.busy; radius: 999; color: theme.soft; implicitHeight: 30; implicitWidth: aiRow.implicitWidth + 24
                        RowLayout { id: aiRow; anchors.centerIn: parent; spacing: 8
                            Rectangle { width: 8; height: 8; radius: 4; color: theme.good
                                SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 900 } NumberAnimation { to: 1; duration: 900 } } }
                            Label { text: "KI-Agent aktiv"; font.bold: true; color: theme.blueText; font.pixelSize: 13 } } }
                }
            }

            Rectangle {   // Ergebnis-Hinweis
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 16
                visible: controller.lastResult !== "" && !controller.busy
                radius: 14; color: theme.soft; border.color: "#c9daff"
                implicitHeight: resultRow.implicitHeight + 22
                RowLayout { id: resultRow; anchors.fill: parent; anchors.margins: 11; spacing: 10
                    Image { source: window.icon("check", "blue"); sourceSize: Qt.size(18, 18); Layout.alignment: Qt.AlignTop }
                    Label { text: controller.lastResult; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.blueText }
                    ActionButton { visible: controller.caseDir !== ""; text: "Fallakte"; iconName: "folder"; onClicked: controller.openFolder(controller.caseDir) } }
            }

            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: window.page

                // ============================================ 0 Übersicht
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 18
                        Item { implicitHeight: 4 }
                        Rectangle {   // Hero
                            Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                            radius: 22; implicitHeight: 214; clip: true
                            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#1d4ed8" } GradientStop { position: 1; color: "#3b82f6" } }
                            Rectangle { width: 280; height: 280; radius: 140; x: parent.width - 200; y: -90; color: "#ffffff"; opacity: 0.08 }
                            Rectangle { width: 160; height: 160; radius: 80; x: parent.width - 330; y: 120; color: "#ffffff"; opacity: 0.06 }
                            Image { source: "icons/logo.svg"; sourceSize: Qt.size(200, 200); width: 92; height: 92; x: parent.width - 122; y: 18 }
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 26; spacing: 10
                                Label { text: window.hasDiag ? diag.headline : (window.dev ? "Bereit zur Untersuchung" : "Stick einstecken – die KI übernimmt den Rest")
                                    font.pixelSize: 24; font.bold: true; color: "#ffffff"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.rightMargin: 140 }
                                Label { text: window.hasDiag ? ("Sicherheit " + diag.confidence + " % · Datenrettung " + diag.dataPercent + " % · " + (diag.repairable ? "reparierbar" : "nicht oder nur eingeschränkt reparierbar"))
                                                             : "Erkennt auch Sticks mit 0 MB, „kein Medium“ oder Code 43. Alles ist zuerst nur lesend."
                                    color: "#dbeafe"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.rightMargin: 140 }
                                Item { Layout.fillHeight: true }
                                RowLayout { Layout.fillWidth: true; spacing: 0
                                    Step { stepIndex: 0; label: "Erkennen" }
                                    Rectangle { Layout.fillWidth: true; height: 2; color: "#55ffffff"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 20 }
                                    Step { stepIndex: 1; label: "Diagnose" }
                                    Rectangle { Layout.fillWidth: true; height: 2; color: "#55ffffff"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 20 }
                                    Step { stepIndex: 2; label: "Retten" }
                                    Rectangle { Layout.fillWidth: true; height: 2; color: "#55ffffff"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 20 }
                                    Step { stepIndex: 3; label: "Reparieren" } }
                            }
                        }
                        RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Tile { label: "USB-Geräte"; value: String(controller.devices.length); note: "Interne Laufwerke werden nie angezeigt"; iconName: "usb" }
                            Tile { label: "KI-Modell"; value: (controller.knowledge.modelAccuracy || 0) + " % Treffer"; note: (controller.knowledge.modelExamples || 0) + " Trainingsbeispiele"; iconName: "chip"; level: "good" }
                            Tile { label: "Gelernte Fälle"; value: String(controller.knowledge.actions || 0); note: (controller.knowledge.successes || 0) + " Reparaturen erfolgreich"; iconName: "box" }
                        }
                        RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Label { text: "USB-Geräte"; font.pixelSize: 17; font.bold: true; color: theme.strong; Layout.fillWidth: true }
                            ActionButton { text: "Abbild öffnen"; iconName: "folder"; onClicked: imageDialog.open(); enabled: !controller.busy }
                            ActionButton { text: "Neu erkennen"; iconName: "refresh"; primary: true; onClicked: controller.refresh(false); enabled: !controller.busy } }
                        Rectangle { visible: controller.hostWarning !== ""; Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                            radius: 14; color: theme.warnSoft; implicitHeight: 46
                            Label { anchors.fill: parent; anchors.margins: 13; text: "⚠  " + controller.hostWarning; color: "#b45309"; font.bold: true; verticalAlignment: Text.AlignVCenter } }
                        Card {
                            visible: controller.devices.length === 0; Layout.leftMargin: 22; Layout.rightMargin: 22
                            RowLayout { spacing: 16
                                Rectangle { width: 56; height: 56; radius: 16; color: theme.soft
                                    Image { anchors.centerIn: parent; source: window.icon("usb", "blue"); sourceSize: Qt.size(26, 26) } }
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Label { text: "Noch kein USB-Stick gefunden"; font.bold: true; font.pixelSize: 15; color: theme.strong }
                                    Label { text: "Einfach einstecken – er erscheint automatisch, auch wenn Windows ihn nicht öffnen kann."; color: theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
                        }
                        Repeater {
                            model: controller.devices
                            delegate: Card {
                                Layout.leftMargin: 22; Layout.rightMargin: 22; pad: 16
                                border.color: index === controller.selected ? theme.sky : theme.line
                                border.width: index === controller.selected ? 2 : 1
                                RowLayout { spacing: 16
                                    Rectangle { width: 56; height: 56; radius: 16; color: window.levelSoft(modelData.level)
                                        Image { anchors.centerIn: parent; source: window.icon("usb", modelData.level === "bad" ? "red" : modelData.level === "warn" ? "amber" : "green"); sourceSize: Qt.size(26, 26) } }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        RowLayout { spacing: 8
                                            Label { text: modelData.name; font.pixelSize: 16; font.bold: true; color: theme.strong }
                                            Pill { text: modelData.state; level: modelData.level }
                                            Pill { visible: modelData.image; text: "Abbild"; level: "blue" } }
                                        Label { text: modelData.id + "  ·  " + modelData.size + (modelData.letters ? "  ·  Laufwerk " + modelData.letters : "") + (modelData.uas ? "  ·  UAS" : ""); color: theme.muted }
                                        Label { text: "Controller: " + modelData.controller; color: theme.muted; font.pixelSize: 12 }
                                        Label { visible: modelData.problem !== ""; text: modelData.problem; color: "#b91c1c"; font.pixelSize: 12 } }
                                    ActionButton { text: "Untersuchen"; iconName: "spark"; primary: true; enabled: !controller.busy; onClicked: { controller.select(index); window.page = 1 } }
                                }
                            }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 1 Diagnose
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { implicitHeight: 4 }
                        Card { visible: !window.hasDiag; Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: controller.busy ? "Ich untersuche den Stick …" : "Noch keine Diagnose"; iconName: "spark"
                                sub: controller.busy ? "Die einzelnen Messungen siehst du live im KI-Live-Monitor." : "Wähle in der Übersicht einen Stick aus. Die Untersuchung startet automatisch und ist rein lesend." } }
                        Card { visible: window.hasDiag; Layout.leftMargin: 22; Layout.rightMargin: 22
                            RowLayout { spacing: 22
                                Ring { value: diag.confidence || 0; caption: "sicher"; ringColor: window.levelColor(window.topLevel()) }
                                ColumnLayout { Layout.fillWidth: true; spacing: 6
                                    RowLayout { Pill { text: "Befund"; level: window.topLevel() }
                                        Label { text: window.hasDiag ? diag.device + " · " + diag.deviceId : ""; color: theme.muted; font.pixelSize: 12 } }
                                    Label { text: diag.headline || ""; font.pixelSize: 21; font.bold: true; color: theme.strong; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: diag.explanation || ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.text } }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: theme.line }
                            RowLayout { spacing: 8
                                Label { text: "Stimmt der Befund? Deine Antwort trainiert die KI."; color: theme.muted }
                                ActionButton { text: "Ja, stimmt"; iconName: "check"; onClicked: controller.confirmDiagnosis(diag.topId, true) }
                                ActionButton { text: "Nein"; iconName: "x"; onClicked: controller.confirmDiagnosis(diag.topId, false) }
                                Item { Layout.fillWidth: true }
                                ActionButton { text: "Neu untersuchen"; iconName: "refresh"; enabled: !controller.busy; onClicked: controller.diagnose() }
                                ActionButton { text: "Zur Reparatur"; iconName: "tool"; primary: true; onClicked: window.page = 3 } }
                        }
                        RowLayout { visible: window.hasDiag; Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Tile { label: "Datenrettung"; iconName: "save"; value: (diag.dataPercent || 0) + " %"; note: diag.dataChance || ""; level: diag.dataPercent >= 70 ? "good" : diag.dataPercent >= 30 ? "warn" : "bad"; fill: diag.dataPercent || 0 }
                            Tile { label: "Reparierbar"; iconName: "tool"; value: diag.repairable ? "Ja" : "Eingeschränkt"; note: diag.repair || ""; level: diag.repairable ? "good" : "warn" }
                            Tile { label: "Rettungsabbild"; iconName: "box"; value: diag.imageAvailable ? "Vorhanden" : "Noch keins"; note: diag.imageAvailable ? diag.imagePath : "Vor Schreibzugriffen empfohlen"; level: diag.imageAvailable ? "good" : "warn" }
                        }
                        RowLayout { visible: window.hasDiag; Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Card { Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 3
                                Heading { text: "Hypothesen der KI"; iconName: "spark"; sub: "Jede Annahme mit ihren Belegen" }
                                Repeater {
                                    model: diag.hypotheses || []
                                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 5
                                        RowLayout { Label { text: modelData.title; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.strong }
                                            Label { text: modelData.confidence + " %"; font.bold: true; color: theme.blue2 } }
                                        Bar { Layout.fillWidth: true; value: modelData.confidence; barColor: index === 0 ? theme.blue2 : "#93c5fd" }
                                        Repeater { model: modelData.evidence
                                            delegate: Label { text: "•  " + modelData; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                        Item { implicitHeight: 6 } }
                                }
                            }
                            ColumnLayout { Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 2; Layout.fillWidth: true; spacing: 16
                                Card {
                                    Heading { text: "Trainiertes Modell"; iconName: "chip"; sub: "Erkennt bekannte Fehlerbilder am Gesamtmuster" }
                                    Repeater { model: diag.model || []
                                        delegate: ColumnLayout { Layout.fillWidth: true; spacing: 4
                                            RowLayout { Label { text: modelData.title; Layout.fillWidth: true }
                                                Label { text: modelData.p + " %"; font.bold: true; color: theme.blue2 } }
                                            Bar { Layout.fillWidth: true; value: modelData.p; barColor: index === 0 ? theme.blue2 : "#bfdbfe" } } }
                                }
                                Card {
                                    Heading { text: "Messwerte"; iconName: "usb" }
                                    Repeater { model: diag.facts || []
                                        delegate: Label { text: modelData; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 13; color: theme.text } }
                                }
                            }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 2 Datenrettung
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { implicitHeight: 4 }
                        Card { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Zielordner"; iconName: "folder"; sub: "Gerettete Dateien landen hier – ein Ordner auf dem PC, niemals auf dem defekten Stick." }
                            RowLayout {
                                TextField { Layout.fillWidth: true; text: controller.targetDir; placeholderText: "Kein Zielordner gewählt"; onEditingFinished: controller.targetDir = text }
                                ActionButton { text: "Wählen …"; iconName: "folder"; onClicked: targetDialog.open() }
                                ActionButton { text: "Öffnen"; enabled: controller.targetDir !== ""; onClicked: controller.openFolder(controller.targetDir) } }
                        }
                        RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Repeater {
                                model: [
                                    { n: "1", t: "Rettungsabbild", i: "save", a: "image", needsTarget: false, d: "Liest den ganzen Stick in vier Durchgängen (schnell → Lücken → sektorweise → Wiederholung). Fortsetzbar. Danach arbeitet die KI nur noch mit dem Abbild." },
                                    { n: "2", t: "Dateien retten", i: "folder", a: "file_rescue", needsTarget: true, d: "FAT und exFAT mit Ordnern und Namen – auch gelöschte Dateien und Sticks mit zerstörtem Bootsektor." },
                                    { n: "3", t: "Signatursuche", i: "spark", a: "carve", needsTarget: true, d: "Findet Fotos, Videos, Musik, PDFs und Office-Dateien am Inhalt – auch bei RAW, NTFS oder nach dem Formatieren." }
                                ]
                                delegate: Card { Layout.alignment: Qt.AlignTop
                                    RowLayout { Rectangle { width: 34; height: 34; radius: 17; gradient: Gradient { GradientStop { position: 0; color: theme.sky } GradientStop { position: 1; color: theme.blue } }
                                            Label { anchors.centerIn: parent; text: modelData.n; color: "#ffffff"; font.bold: true } }
                                        Label { text: modelData.t; font.pixelSize: 16; font.bold: true; color: theme.strong } }
                                    Label { text: modelData.d; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted; Layout.preferredHeight: 64 }
                                    ActionButton { text: "Starten"; primary: true; iconName: modelData.i; Layout.fillWidth: true
                                        enabled: !controller.busy && window.dev !== null && (!modelData.needsTarget || controller.targetDir !== "")
                                        onClicked: controller.runAction(modelData.a) }
                                }
                            }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 3 Reparatur
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { implicitHeight: 4 }
                        Rectangle {
                            Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; radius: 20; implicitHeight: 104
                            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#1d4ed8" } GradientStop { position: 1; color: "#3b82f6" } }
                            RowLayout { anchors.fill: parent; anchors.margins: 22; spacing: 16
                                ColumnLayout { Layout.fillWidth: true; spacing: 4
                                    Label { text: "Automatisch reparieren"; color: "#ffffff"; font.pixelSize: 19; font.bold: true }
                                    Label { text: "Die KI arbeitet den Plan ab, fragt vor jedem Schreibzugriff und prüft nach jedem Schritt selbst, ob es besser geworden ist."; color: "#dbeafe"; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                Button { id: autoBtn; text: "Start"; enabled: !controller.busy && window.hasDiag; implicitHeight: 44; implicitWidth: 120
                                    onClicked: { controller.autoRun(); window.page = 4 }
                                    contentItem: Label { text: autoBtn.text; font.bold: true; color: theme.blue; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { radius: 12; color: autoBtn.down ? "#dbeafe" : "#ffffff"; opacity: autoBtn.enabled ? 1 : 0.6 } }
                            }
                        }
                        Card { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Plan der KI"; iconName: "spark"; sub: "Erst sichern, dann von harmlos nach riskant. Was bei ähnlichen Sticks geholfen hat, kommt zuerst." }
                            Label { visible: !window.hasDiag; text: "Erst einen Stick untersuchen."; color: theme.muted }
                            Repeater {
                                model: diag.plan || []
                                delegate: Rectangle { Layout.fillWidth: true; radius: 14; color: theme.softer; border.color: theme.line; implicitHeight: planRow.implicitHeight + 22
                                    RowLayout { id: planRow; anchors.fill: parent; anchors.margins: 11; spacing: 14
                                        Rectangle { width: 30; height: 30; radius: 15; color: theme.soft
                                            Label { anchors.centerIn: parent; text: String(index + 1); font.bold: true; color: theme.blue } }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                                            RowLayout { spacing: 8
                                                Label { text: modelData.title; font.bold: true; color: theme.strong }
                                                Pill { text: modelData.riskText; level: modelData.manual ? "blue" : window.riskLevel(modelData.risk) }
                                                Pill { visible: modelData.learned !== ""; text: "Erfahrung: " + modelData.learned; level: "good" } }
                                            Label { text: modelData.why; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                        ActionButton { text: modelData.manual ? "Anleitung" : "Ausführen"; enabled: !controller.busy; danger: modelData.risk === 3 && !modelData.manual; onClicked: controller.runAction(modelData.id) } }
                                }
                            }
                        }
                        RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Card { Layout.alignment: Qt.AlignTop
                                Heading { text: "Optionen"; iconName: "refresh" }
                                CheckBox { text: "Stick enthält keine wichtigen Daten\n(schaltet Kapazitätstest und Formatieren ohne Abbild frei)"; checked: controller.stickIsEmpty; onToggled: controller.stickIsEmpty = checked }
                                RowLayout { Label { text: "Dateisystem:" }
                                    ComboBox { model: ["exfat", "fat32", "ntfs"]; currentIndex: Math.max(0, model.indexOf(controller.formatFs)); onActivated: controller.formatFs = currentText } }
                            }
                            Card { Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 2; Layout.fillWidth: true
                                Heading { text: "Einzelne Werkzeuge"; iconName: "tool" }
                                Flow { Layout.fillWidth: true; spacing: 8
                                    ActionButton { text: "Oberflächentest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("surface_scan") }
                                    ActionButton { text: "Kapazitätstest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("capacity_test") }
                                    ActionButton { text: "Schreibtest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("write_test") }
                                    ActionButton { text: "USB-Gerät neu starten"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("restart_device") }
                                    ActionButton { text: "Schreibschutz entfernen"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("clear_readonly") }
                                    ActionButton { text: "Bootsektor wiederherstellen"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("restore_boot") }
                                    ActionButton { text: "Partitionstabelle neu"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("rebuild_mbr") }
                                    ActionButton { text: "Neuflash-Anleitung"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("reflash_guide") }
                                    ActionButton { text: "Auf echte Größe einrichten"; danger: true; enabled: !controller.busy && window.dev !== null && diag.fake === true; onClicked: controller.runAction("fix_fake") }
                                    ActionButton { text: "Formatieren FAT32"; danger: true; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("format_fat32") }
                                    ActionButton { text: "Formatieren exFAT"; danger: true; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("format_exfat") } }
                            }
                        }
                        Card { visible: window.hasDiag && diag.speedCurve !== undefined && diag.speedCurve.length > 0; Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Lesegeschwindigkeit über den Stick"; iconName: "clock"; sub: "Rot = auffällig langsame Bereiche (Verschleiß)" }
                            Canvas {
                                Layout.fillWidth: true; implicitHeight: 150
                                property var values: diag.speedCurve || []
                                onValuesChanged: requestPaint()
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    var c = getContext("2d"); c.reset()
                                    var v = values; if (!v.length) return
                                    var max = 0; for (var i = 0; i < v.length; ++i) max = Math.max(max, v[i])
                                    if (max <= 0) return
                                    var w = width / v.length
                                    for (var k = 0; k < v.length; ++k) {
                                        var h = (height - 20) * v[k] / max
                                        c.fillStyle = v[k] < max * 0.3 ? "#ef4444" : "#3b82f6"
                                        c.fillRect(k * w + 1, height - 16 - h, Math.max(1, w - 2), h)
                                    }
                                    c.fillStyle = "#64748b"; c.font = "11px sans-serif"
                                    c.fillText("Anfang", 0, height - 2); c.fillText("Ende", width - 30, height - 2); c.fillText(max.toFixed(1) + " MB/s", 4, 12)
                                }
                            }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 4 KI-Live-Monitor
                ColumnLayout {
                    spacing: 12
                    RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22; Layout.topMargin: 18; spacing: 8
                        Repeater { model: ["Alle", "Messung", "Hypothese", "Entscheidung", "Aktion", "Prüfung", "Lernen", "Warnung"]
                            delegate: Rectangle { radius: 999; implicitHeight: 30; implicitWidth: fl.implicitWidth + 24
                                color: monList.filter === modelData ? theme.blue2 : "#ffffff"; border.color: monList.filter === modelData ? theme.blue2 : "#cfdcf5"
                                Label { id: fl; anchors.centerIn: parent; text: modelData; font.bold: true; font.pixelSize: 12; color: monList.filter === modelData ? "#ffffff" : theme.blueText }
                                TapHandler { onTapped: monList.filter = modelData } } }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: "Leeren"; iconName: "x"; onClicked: controller.clearMonitor() }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 22; Layout.topMargin: 0
                        radius: 18; color: "#ffffff"; border.color: theme.line
                        ListView {
                            id: monList
                            property string filter: "Alle"
                            anchors.fill: parent; anchors.margins: 12; clip: true; spacing: 4
                            model: controller.monitor
                            onCountChanged: positionViewAtEnd()
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Item {
                                width: monList.width - 14
                                readonly property bool shown: monList.filter === "Alle" || modelData.kind === monList.filter
                                height: shown ? monRow.implicitHeight + 6 : 0
                                visible: shown
                                RowLayout { id: monRow; width: parent.width; spacing: 10
                                    Label { text: modelData.time; font.family: theme.mono; color: theme.muted; font.pixelSize: 12; Layout.alignment: Qt.AlignTop; Layout.topMargin: 3 }
                                    Pill { text: modelData.kind; level: window.kindLevel(modelData.kind); Layout.preferredWidth: 100; Layout.alignment: Qt.AlignTop }
                                    Label { text: modelData.text; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 13; Layout.topMargin: 3 } }
                            }
                        }
                    }
                }

                // ============================================ 5 Protokolle
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 12
                        Item { implicitHeight: 4 }
                        Card { visible: !window.hasDiag || diag.logs.length === 0; Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Keine passenden Einträge"; iconName: "folder"; sub: "Ausgewertet werden Ereignisanzeige (System, Kernel-PnP), setupapi.dev.log und Geräte-Manager der letzten 14 Tage." } }
                        Repeater {
                            model: window.hasDiag ? diag.logs : []
                            delegate: Card { Layout.leftMargin: 22; Layout.rightMargin: 22; pad: 16; spacing: 6
                                RowLayout { spacing: 8
                                    Rectangle { width: 6; height: 30; radius: 3; color: modelData.weight >= 3 ? theme.bad : modelData.weight === 2 ? theme.warn : "#94a3b8" }
                                    Label { text: modelData.time; font.family: theme.mono; color: theme.muted }
                                    Label { text: modelData.source + (modelData.id ? " · " + modelData.id : ""); font.bold: true; Layout.fillWidth: true; color: theme.strong }
                                    Pill { text: modelData.matched ? "diesem Stick zugeordnet" : "allgemein"; level: modelData.matched ? "blue" : "grey" }
                                    Pill { text: modelData.weight >= 3 ? "schwer" : modelData.weight === 2 ? "mittel" : "leicht"; level: modelData.weight >= 3 ? "bad" : modelData.weight === 2 ? "warn" : "grey" } }
                                Label { text: modelData.meaning; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.strong; font.pixelSize: 14 }
                                Label { visible: modelData.raw !== ""; text: modelData.raw; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted; font.pixelSize: 12 }
                            }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 6 Wissensbasis
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { implicitHeight: 4 }
                        RowLayout { Layout.leftMargin: 22; Layout.rightMargin: 22; spacing: 16
                            Tile { label: "Modell-Treffer"; iconName: "chip"; value: (controller.knowledge.modelAccuracy || 0) + " %"; note: "Kreuzvalidierung"; level: "good"; fill: controller.knowledge.modelAccuracy || 0 }
                            Tile { label: "Trainingsbeispiele"; iconName: "box"; value: String(controller.knowledge.modelExamples || 0); note: (controller.knowledge.modelConfirmed || 0) + " von dir bestätigt" }
                            Tile { label: "Reparaturversuche"; iconName: "tool"; value: String(controller.knowledge.actions || 0); note: (controller.knowledge.successes || 0) + " erfolgreich"; level: "good" }
                            Tile { label: "Geräte"; iconName: "usb"; value: String(controller.knowledge.devices || 0); note: (controller.knowledge.controllers || 0) + " bekannte Controller-Kennungen" }
                        }
                        Card { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Zuletzt gelernt"; iconName: "box"; sub: controller.knowledge.path || "" }
                            Label { visible: !controller.knowledge.recent || controller.knowledge.recent.length === 0; text: "Noch nichts – jeder Reparaturversuch und jede bestätigte Diagnose landet hier."; color: theme.muted }
                            Repeater { model: controller.knowledge.recent || []
                                delegate: RowLayout { Layout.fillWidth: true; spacing: 10
                                    Label { text: modelData.time; font.family: theme.mono; color: theme.muted; font.pixelSize: 12 }
                                    Pill { text: modelData.kind; level: "blue" }
                                    Label { text: modelData.device + " · " + modelData.what + (modelData.symptoms ? " · [" + modelData.symptoms + "]" : ""); Layout.fillWidth: true; elide: Text.ElideRight }
                                    Pill { text: modelData.ok ? (modelData.kind === "Versuch" ? "geholfen" : "bestätigt") : (modelData.kind === "Versuch" ? "ohne Erfolg" : "verworfen"); level: modelData.ok ? "good" : "warn" } } }
                        }
                        Item { implicitHeight: 18 }
                    }
                }

                // ============================================ 7 Einstellungen
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 16
                        Item { implicitHeight: 4 }
                        Card { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Lokale KI (optional, ohne Internet)"; iconName: "chip"; sub: "OpenAI-kompatibler Endpunkt, z. B. der llama-server von Butchies KI Studio. Leer lassen = eingebauter Assistent." }
                            TextField { Layout.fillWidth: true; text: controller.llmUrl; placeholderText: "http://butchies-ki.local:8081/v1/chat/completions"; onEditingFinished: controller.llmUrl = text }
                            RowLayout { Label { text: "Modellname:" }
                                TextField { Layout.preferredWidth: 260; text: controller.llmModel; onEditingFinished: controller.llmModel = text } }
                        }
                        Card { Layout.leftMargin: 22; Layout.rightMargin: 22
                            Heading { text: "Sicherheit"; iconName: "warn" }
                            Label { wrapMode: Text.WordWrap; Layout.fillWidth: true; lineHeight: 1.3
                                text: "•  Interne Laufwerke und der Systemdatenträger werden nie angezeigt.\n•  Alles ist zuerst nur lesend.\n•  Jeder Schreibzugriff braucht deine Freigabe; Originalsektoren werden vorher in die Rückgängig-Datei der Fallakte gesichert.\n•  Kapazitätstest und Formatieren sind ohne Rettungsabbild gesperrt (oder mit „Stick ist leer“).\n•  Ein Controller-Neuflash wird nur erklärt, nie selbst ausgeführt." }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ KI-Assistent
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 350; color: "#ffffff"
            Rectangle { width: 1; height: parent.height; color: theme.line }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle { Layout.fillWidth: true; implicitHeight: 76
                    gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#eaf1ff" } GradientStop { position: 1; color: "#ffffff" } }
                    RowLayout { anchors.fill: parent; anchors.margins: 18; spacing: 12
                        Rectangle { width: 40; height: 40; radius: 12; gradient: Gradient { GradientStop { position: 0; color: theme.sky } GradientStop { position: 1; color: theme.blue } }
                            Image { anchors.centerIn: parent; source: window.icon("spark", "white"); sourceSize: Qt.size(20, 20) } }
                        ColumnLayout { spacing: 0; Layout.fillWidth: true
                            Label { text: "KI-Assistent"; font.pixelSize: 16; font.bold: true; color: theme.strong }
                            Label { text: controller.llmUrl !== "" ? "lokale KI verbunden" : "eingebaut · offline"; color: theme.muted; font.pixelSize: 12 } } }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line } }
                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 16; spacing: 10
                    Label { visible: controller.chat.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                        text: "Frag mich zum ausgewählten Stick:" }
                    Repeater { model: controller.chat.length === 0 ? ["Kann ich meine Fotos retten?", "Warum glaubst du das?", "Was steht in den Protokollen?", "Kann man ihn reparieren?"] : []
                        delegate: Rectangle { Layout.fillWidth: true; radius: 12; color: theme.softer; border.color: "#d6e3fb"; implicitHeight: 38
                            Label { anchors.fill: parent; anchors.leftMargin: 12; verticalAlignment: Text.AlignVCenter; text: modelData; color: theme.blueText }
                            TapHandler { onTapped: controller.ask(modelData) } } }
                    ListView {
                        id: chatList
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
                        model: controller.chat
                        onCountChanged: positionViewAtEnd()
                        delegate: Item {
                            id: chatItem
                            width: chatList.width
                            readonly property bool mine: modelData.from === "du"
                            implicitHeight: bubble.implicitHeight
                            height: implicitHeight
                            Rectangle {
                                id: bubble
                                x: chatItem.mine ? chatItem.width - width : 0
                                width: Math.min(chatList.width - 24, chatText.implicitWidth + 26)
                                radius: 14
                                color: chatItem.mine ? theme.blue2 : theme.softer
                                border.color: chatItem.mine ? theme.blue2 : "#d6e3fb"
                                implicitHeight: chatText.implicitHeight + 20
                                height: implicitHeight
                                Label { id: chatText; width: Math.min(implicitWidth, chatList.width - 50); x: 13; y: 10; text: modelData.text; wrapMode: Text.WordWrap
                                    color: chatItem.mine ? "#ffffff" : theme.text; textFormat: Text.PlainText }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; radius: 14; color: theme.softer; border.color: "#cfdcf5"; implicitHeight: 50
                        RowLayout { anchors.fill: parent; anchors.margins: 5; spacing: 6
                            TextField { id: askField; Layout.fillWidth: true; placeholderText: "Frage stellen …"; background: Item {}; onAccepted: { controller.ask(text); text = "" } }
                            ActionButton { text: "Senden"; primary: true; implicitHeight: 38; onClicked: { controller.ask(askField.text); askField.text = "" } } } }
                }
            }
        }
    }
}
