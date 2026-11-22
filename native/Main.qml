import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Butchies USB Recovery Studio AI – helle Oberfläche im Stil von NVMe Recovery Studio AI
// Aufbau: Kopfzeile · Navigation links · Arbeitsfläche · KI-Assistent rechts
ApplicationWindow {
    id: window
    width: 1480; height: 920
    minimumWidth: 1150; minimumHeight: 700
    visible: true
    title: "Butchies USB Recovery Studio AI"
    color: theme.bg
    required property QtObject controller

    QtObject {
        id: theme
        readonly property color bg: "#f5f8fc"
        readonly property color card: "#ffffff"
        readonly property color line: "#e5ebf3"
        readonly property color text: "#1f2937"
        readonly property color strong: "#0f172a"
        readonly property color muted: "#667085"
        readonly property color accent: "#2563eb"
        readonly property color accent2: "#3b82f6"
        readonly property color accentSoft: "#e8f0fe"
        readonly property color accentText: "#1d4ed8"
        readonly property color good: "#22c55e"
        readonly property color goodSoft: "#e8f8ef"
        readonly property color goodText: "#15803d"
        readonly property color warn: "#f59e0b"
        readonly property color warnSoft: "#fff5e6"
        readonly property color warnText: "#b45309"
        readonly property color bad: "#ef4444"
        readonly property color badSoft: "#fdeeee"
        readonly property color badText: "#c0262d"
        readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
    }
    font.family: interRegular.status === FontLoader.Ready ? interRegular.name : "Segoe UI"
    font.pixelSize: 14
    palette.window: theme.bg
    palette.base: "#ffffff"
    palette.button: "#ffffff"
    palette.buttonText: "#344054"
    palette.text: theme.text
    palette.windowText: theme.text
    palette.highlight: theme.accent

    FontLoader { id: interRegular; source: "fonts/Inter-Regular.otf" }
    FontLoader { source: "fonts/Inter-SemiBold.otf" }
    FontLoader { source: "fonts/Inter-Bold.otf" }

    property int page: 0
    readonly property var diag: controller.diagnosis || ({})
    readonly property bool hasDiag: diag.valid === true
    readonly property var dev: controller.selected >= 0 ? controller.devices[controller.selected] : null

    function levelColor(l) { return l === "bad" ? theme.bad : l === "warn" ? theme.warn : l === "good" ? theme.good : "#98a2b3" }
    function levelSoft(l) { return l === "bad" ? theme.badSoft : l === "warn" ? theme.warnSoft : l === "good" ? theme.goodSoft : "#eef1f5" }
    function levelText(l) { return l === "bad" ? theme.badText : l === "warn" ? theme.warnText : l === "good" ? theme.goodText : "#475467" }
    function icon(name, color) { return "icons/" + name + "-" + (color || "grey") + ".svg" }
    function riskLevel(r) { return r === 0 ? "good" : r === 1 ? "good" : r === 2 ? "warn" : "bad" }
    function topLevel() {
        if (!hasDiag) return "grey"
        if (diag.topId === "healthy") return "good"
        if (diag.dataPercent >= 70) return "warn"
        return "bad"
    }
    function kindLevel(k) {
        return k === "Fehler" ? "bad" : k === "Warnung" ? "warn" : k === "Hypothese" || k === "Entscheidung" ? "blue" : k === "Prüfung" || k === "Lernen" ? "good" : "grey"
    }
    function fileUrlToPath(u) {
        var p = decodeURIComponent(u.toString().replace(/^file:\/\//, ""))
        if (Qt.platform.os === "windows") p = p.replace(/^\//, "").replace(/\//g, "\\")
        return p
    }

    // ------------------------------------------------------- Bausteine
    component Card: Rectangle {
        default property alias content: inner.data
        property alias spacing: inner.spacing
        color: theme.card; radius: 16; border.color: theme.line
        implicitHeight: inner.implicitHeight + 36
        Layout.fillWidth: true
        ColumnLayout { id: inner; anchors.fill: parent; anchors.margins: 18; spacing: 10 }
    }
    component Heading: RowLayout {
        property string text
        property string iconName: ""
        spacing: 8
        Image { visible: iconName !== ""; source: iconName ? window.icon(iconName) : ""; sourceSize: Qt.size(18, 18) }
        Label { text: parent.text; font.pixelSize: 15; font.bold: true; color: theme.strong }
    }
    component Pill: Rectangle {
        property string text
        property string level: "grey"
        radius: 999
        color: level === "blue" ? theme.accentSoft : window.levelSoft(level)
        implicitWidth: pillLabel.implicitWidth + 20; implicitHeight: 24
        Label { id: pillLabel; anchors.centerIn: parent; text: parent.text; font.pixelSize: 12; font.bold: true; color: parent.level === "blue" ? theme.accentText : window.levelText(parent.level) }
    }
    component ActionButton: Button {
        id: ab
        property string iconName: ""
        property bool primary: false
        property bool danger: false
        implicitHeight: 40
        leftPadding: 16; rightPadding: 16
        contentItem: RowLayout {
            spacing: 8
            Image { visible: ab.iconName !== ""; source: ab.iconName ? window.icon(ab.iconName, ab.primary ? "white" : (ab.danger ? "red" : "grey")) : ""; sourceSize: Qt.size(16, 16) }
            Label { text: ab.text; font.bold: true; color: ab.primary ? "#ffffff" : (ab.danger ? theme.badText : "#344054"); opacity: ab.enabled ? 1 : 0.5 }
        }
        background: Rectangle {
            radius: 10
            color: ab.primary ? (ab.down ? "#1d4ed8" : theme.accent) : (ab.down ? "#f2f4f7" : "#ffffff")
            border.color: ab.primary ? "transparent" : (ab.danger ? "#f4b4ae" : "#d9dde5")
            opacity: ab.enabled ? 1 : 0.6
        }
    }
    component Bar: Rectangle {
        property real value: 0
        property color barColor: theme.accent
        implicitHeight: 8; radius: 4; color: "#eef1f5"
        Rectangle { width: parent.width * Math.max(0, Math.min(1, parent.value / 100)); height: parent.height; radius: 4; color: parent.barColor
            Behavior on width { NumberAnimation { duration: 200 } } }
    }
    component Tile: Rectangle {
        property string label
        property string value
        property string note
        property string level: "grey"
        property real fill: -1
        Layout.fillWidth: true
        implicitHeight: 118; radius: 16; color: theme.card; border.color: theme.line
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 4
            Label { text: label; color: theme.muted }
            Label { text: value; font.pixelSize: 24; font.bold: true; color: theme.strong; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: note; visible: note !== ""; color: window.levelText(level); font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
            Bar { visible: fill >= 0; Layout.fillWidth: true; value: fill; barColor: window.levelColor(level) }
        }
    }
    component NavItem: Rectangle {
        property string text
        property string iconName
        property int index
        property string badge: ""
        Layout.fillWidth: true
        implicitHeight: 42; radius: 10
        color: window.page === index ? theme.accentSoft : (hover.hovered ? "#f2f4f7" : "transparent")
        HoverHandler { id: hover }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 10; spacing: 10
            Image { source: window.icon(iconName, window.page === index ? "blue" : "grey"); sourceSize: Qt.size(18, 18) }
            Label { text: parent.parent.text; Layout.fillWidth: true; font.bold: window.page === index; color: window.page === index ? theme.accentText : theme.text }
            Pill { visible: badge !== ""; text: badge; level: "blue" }
        }
        TapHandler { onTapped: window.page = index }
    }

    // ------------------------------------------------------- Dialoge
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
        width: 560
        modal: true; focus: true
        closePolicy: Popup.NoAutoClose
        visible: controller.approval && controller.approval.id !== undefined
        background: Rectangle { radius: 18; color: "#ffffff"; border.color: theme.line }
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                spacing: 10
                Image { source: window.icon("warn", window.riskLevel(controller.approval.risk) === "bad" ? "red" : "amber"); sourceSize: Qt.size(26, 26) }
                Label { text: "Freigabe nötig"; font.pixelSize: 20; font.bold: true; color: theme.strong }
            }
            Label { text: controller.approval.title || ""; font.pixelSize: 16; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Pill { text: controller.approval.riskText || ""; level: window.riskLevel(controller.approval.risk) }
            Label { text: controller.approval.details || ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.text }
            Label {
                visible: controller.approval.risk === 3
                text: "Achtung: Dieser Schritt löscht alle Daten auf dem Stick. Nur fortfahren, wenn die Daten gesichert oder unwichtig sind."
                color: theme.badText; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 10
                ActionButton { text: "Ablehnen"; onClicked: controller.answerApproval(false) }
                ActionButton { text: "Freigeben"; primary: controller.approval.risk !== 3; danger: controller.approval.risk === 3; onClicked: controller.answerApproval(true) }
            }
        }
    }

    // ------------------------------------------------------- Kopfzeile
    header: Rectangle {
        height: 68; color: "#ffffff"; border.color: theme.line
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20; spacing: 14
            Image { source: "icons/logo.svg"; sourceSize: Qt.size(36, 36) }
            ColumnLayout {
                spacing: 0
                RowLayout { spacing: 6
                    Label { text: "Butchies USB Recovery Studio"; font.pixelSize: 18; font.bold: true; color: theme.strong }
                    Label { text: "AI"; font.pixelSize: 18; font.bold: true; color: theme.accent } }
                Label { text: "Diagnose · Datenrettung · Reparatur für USB-Sticks"; color: theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                visible: controller.busy
                Layout.preferredWidth: 420; spacing: 4
                RowLayout {
                    Label { text: controller.currentTask; font.bold: true; color: theme.strong }
                    Item { Layout.fillWidth: true }
                    Label { text: Math.round(controller.progress) + " %"; font.bold: true; color: theme.accentText }
                }
                Bar { Layout.fillWidth: true; value: controller.progress }
                Label { text: controller.progressText; color: theme.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            ActionButton { visible: controller.busy; text: "Abbrechen"; iconName: "x"; danger: true; onClicked: controller.cancel() }
            Pill { visible: !controller.busy; text: "KI-Agent aktiv"; level: "good" }
        }
    }

    RowLayout {
        anchors.fill: parent; spacing: 0

        // --------------------------------------------------- Navigation
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 230; color: "#ffffff"; border.color: theme.line
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 4
                NavItem { text: "Geräte"; iconName: "usb"; index: 0; badge: controller.devices.length > 0 ? controller.devices.length : "" }
                NavItem { text: "Diagnose"; iconName: "spark"; index: 1 }
                NavItem { text: "Datenrettung"; iconName: "save"; index: 2 }
                NavItem { text: "Reparatur"; iconName: "tool"; index: 3 }
                NavItem { text: "KI-Live-Monitor"; iconName: "chip"; index: 4 }
                NavItem { text: "Protokolle"; iconName: "folder"; index: 5; badge: window.hasDiag && diag.logs.length > 0 ? diag.logs.length : "" }
                NavItem { text: "Wissensbasis"; iconName: "box"; index: 6 }
                NavItem { text: "Einstellungen"; iconName: "home"; index: 7 }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true; visible: window.dev !== null; radius: 12; color: "#f8fafc"; border.color: theme.line
                    implicitHeight: selCol.implicitHeight + 20
                    ColumnLayout { id: selCol; anchors.fill: parent; anchors.margins: 10; spacing: 2
                        Label { text: "Ausgewählt"; color: theme.muted; font.pixelSize: 11 }
                        Label { text: window.dev ? window.dev.name : ""; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: window.dev ? window.dev.id + " · " + window.dev.size : ""; color: theme.muted; font.pixelSize: 12 } }
                }
            }
        }

        // --------------------------------------------------- Arbeitsfläche
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.margins: 16; Layout.bottomMargin: 0
                visible: controller.lastResult !== "" && !controller.busy
                radius: 12; color: theme.accentSoft; border.color: "#c7d7fe"
                implicitHeight: resultRow.implicitHeight + 20
                RowLayout { id: resultRow; anchors.fill: parent; anchors.margins: 10; spacing: 10
                    Image { source: window.icon("check", "blue"); sourceSize: Qt.size(18, 18); Layout.alignment: Qt.AlignTop }
                    Label { text: controller.lastResult; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.accentText }
                    ActionButton { visible: controller.caseDir !== ""; text: "Fallakte"; iconName: "folder"; onClicked: controller.openFolder(controller.caseDir) } }
            }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: window.page

                // ============================================ 0 Geräte
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        RowLayout { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Label { text: "USB-Geräte"; font.pixelSize: 22; font.bold: true; color: theme.strong; Layout.fillWidth: true }
                            ActionButton { text: "Abbild öffnen"; iconName: "folder"; onClicked: imageDialog.open(); enabled: !controller.busy }
                            ActionButton { text: "Neu erkennen"; iconName: "refresh"; primary: true; onClicked: controller.refresh(false); enabled: !controller.busy }
                        }
                        Rectangle { visible: controller.hostWarning !== ""; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                            radius: 12; color: theme.warnSoft; implicitHeight: 44
                            Label { anchors.fill: parent; anchors.margins: 12; text: "⚠ " + controller.hostWarning; color: theme.warnText; font.bold: true; verticalAlignment: Text.AlignVCenter } }
                        Label {
                            visible: controller.devices.length === 0; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                            text: "Kein USB-Stick gefunden. Stick einstecken – er erscheint automatisch, auch wenn Windows ihn nicht öffnen kann (0 MB, „kein Medium“, Code 43). Interne Laufwerke werden nie angezeigt."
                        }
                        Repeater {
                            model: controller.devices
                            delegate: Card {
                                Layout.leftMargin: 16; Layout.rightMargin: 16
                                border.color: index === controller.selected ? theme.accent : theme.line
                                RowLayout { spacing: 14
                                    Rectangle { width: 52; height: 52; radius: 14; color: window.levelSoft(modelData.level)
                                        Image { anchors.centerIn: parent; source: window.icon("usb", modelData.level === "bad" ? "red" : modelData.level === "warn" ? "amber" : "green"); sourceSize: Qt.size(26, 26) } }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        RowLayout { Label { text: modelData.name; font.pixelSize: 16; font.bold: true; color: theme.strong }
                                            Pill { text: modelData.state; level: modelData.level }
                                            Pill { visible: modelData.image; text: "Abbild"; level: "blue" } }
                                        Label { text: modelData.id + " · " + modelData.size + (modelData.letters ? " · Laufwerk " + modelData.letters : "") + (modelData.uas ? " · UAS" : ""); color: theme.muted }
                                        Label { text: "Controller: " + modelData.controller; color: theme.muted; font.pixelSize: 12 }
                                        Label { visible: modelData.problem !== ""; text: modelData.problem; color: theme.badText; font.pixelSize: 12 }
                                    }
                                    ActionButton { text: "Untersuchen"; iconName: "spark"; primary: true; enabled: !controller.busy; onClicked: { controller.select(index); window.page = 1 } }
                                }
                            }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 1 Diagnose
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        Label { Layout.leftMargin: 16; text: "Diagnose"; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        Label { visible: !window.hasDiag; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                            text: controller.busy ? "Ich untersuche den Stick … Die einzelnen Messungen siehst du im KI-Live-Monitor." : "Wähle unter „Geräte“ einen Stick aus. Die Untersuchung startet automatisch und ist rein lesend." }
                        Card {
                            visible: window.hasDiag; Layout.leftMargin: 16; Layout.rightMargin: 16
                            RowLayout { spacing: 18
                                Rectangle { width: 92; height: 92; radius: 46; color: window.levelSoft(window.topLevel()); border.color: window.levelColor(window.topLevel()); border.width: 4
                                    ColumnLayout { anchors.centerIn: parent; spacing: 0
                                        Label { text: (diag.confidence || 0) + " %"; font.pixelSize: 22; font.bold: true; color: window.levelText(window.topLevel()); Layout.alignment: Qt.AlignHCenter }
                                        Label { text: "sicher"; font.pixelSize: 11; color: theme.muted; Layout.alignment: Qt.AlignHCenter } } }
                                ColumnLayout { Layout.fillWidth: true; spacing: 6
                                    Label { text: "Befund"; color: theme.muted }
                                    Label { text: diag.headline || ""; font.pixelSize: 20; font.bold: true; color: theme.strong; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: diag.explanation || ""; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                            }
                            RowLayout { spacing: 8
                                Label { text: "Stimmt der Befund?"; color: theme.muted }
                                ActionButton { text: "Ja, stimmt"; iconName: "check"; onClicked: controller.confirmDiagnosis(diag.topId, true) }
                                ActionButton { text: "Nein"; iconName: "x"; onClicked: controller.confirmDiagnosis(diag.topId, false) }
                                Item { Layout.fillWidth: true }
                                ActionButton { text: "Neu untersuchen"; iconName: "refresh"; enabled: !controller.busy; onClicked: controller.diagnose() }
                            }
                        }
                        RowLayout { visible: window.hasDiag; Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 14
                            Tile { label: "Datenrettung"; value: (diag.dataPercent || 0) + " %"; note: diag.dataChance || ""; level: diag.dataPercent >= 70 ? "good" : diag.dataPercent >= 30 ? "warn" : "bad"; fill: diag.dataPercent || 0 }
                            Tile { label: "Reparierbar"; value: diag.repairable ? "Ja" : "Nein / eingeschränkt"; note: diag.repair || ""; level: diag.repairable ? "good" : "warn" }
                            Tile { label: "Rettungsabbild"; value: diag.imageAvailable ? "Vorhanden" : "Noch keins"; note: diag.imageAvailable ? diag.imagePath : "Vor Schreibzugriffen empfohlen"; level: diag.imageAvailable ? "good" : "warn" }
                        }
                        Card { visible: window.hasDiag; Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Hypothesen der KI (mit Belegen)"; iconName: "spark" }
                            Repeater {
                                model: diag.hypotheses || []
                                delegate: ColumnLayout { Layout.fillWidth: true; spacing: 4
                                    RowLayout { Label { text: modelData.title; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                        Label { text: modelData.confidence + " %"; font.bold: true; color: theme.accentText } }
                                    Bar { Layout.fillWidth: true; value: modelData.confidence; barColor: index === 0 ? theme.accent : "#93c5fd" }
                                    Repeater { model: modelData.evidence
                                        delegate: Label { text: "• " + modelData; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                    Item { implicitHeight: 6 }
                                }
                            }
                        }
                        Card { visible: window.hasDiag; Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Messwerte"; iconName: "chip" }
                            Repeater { model: diag.facts || []
                                delegate: Label { text: modelData; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 13 } }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 2 Datenrettung
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        Label { Layout.leftMargin: 16; text: "Datenrettung"; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Zielordner"; iconName: "folder" }
                            Label { text: "Gerettete Dateien landen hier – bitte einen Ordner auf dem PC wählen, niemals auf dem defekten Stick."; color: theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout {
                                TextField { Layout.fillWidth: true; text: controller.targetDir; placeholderText: "Kein Zielordner gewählt"; onEditingFinished: controller.targetDir = text }
                                ActionButton { text: "Wählen …"; iconName: "folder"; onClicked: targetDialog.open() }
                                ActionButton { text: "Öffnen"; enabled: controller.targetDir !== ""; onClicked: controller.openFolder(controller.targetDir) }
                            }
                        }
                        RowLayout { Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 14
                            Card {
                                Heading { text: "1. Rettungsabbild"; iconName: "save" }
                                Label { text: "Liest den ganzen Stick in mehreren Durchgängen (schnell → Lücken → sektorweise → Wiederholung) in eine Datei. Fortsetzbar. Danach arbeitet die KI nur noch mit dem Abbild."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted }
                                ActionButton { text: "Abbild erstellen"; primary: true; iconName: "save"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("image") }
                            }
                            Card {
                                Heading { text: "2. Dateien retten"; iconName: "folder" }
                                Label { text: "FAT12/16/32 und exFAT mit Ordnern und Namen – auch gelöschte Dateien und Volumes mit zerstörtem Bootsektor (über die Sicherung)."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted }
                                ActionButton { text: "Dateien retten"; primary: true; iconName: "folder"; enabled: !controller.busy && window.dev !== null && controller.targetDir !== ""; onClicked: controller.runAction("file_rescue") }
                            }
                            Card {
                                Heading { text: "3. Signatursuche"; iconName: "spark" }
                                Label { text: "Findet Fotos, Videos, Musik, PDFs, Office- und ZIP-Dateien anhand ihres Inhalts – auch bei RAW, NTFS oder nach dem Formatieren."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted }
                                ActionButton { text: "Signatursuche"; primary: true; iconName: "spark"; enabled: !controller.busy && window.dev !== null && controller.targetDir !== ""; onClicked: controller.runAction("carve") }
                            }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 3 Reparatur
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        RowLayout { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Label { text: "Reparatur"; font.pixelSize: 22; font.bold: true; color: theme.strong; Layout.fillWidth: true }
                            ActionButton { text: "Automatisch reparieren"; iconName: "tool"; primary: true; enabled: !controller.busy && window.hasDiag; onClicked: { controller.autoRun(); window.page = 4 } }
                        }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Plan der KI"; iconName: "spark" }
                            Label { text: "Reihenfolge: erst sichern, dann von harmlos nach riskant. Was bei ähnlichen Sticks schon geholfen hat, kommt zuerst. Vor jedem Schreibzugriff wirst du gefragt, und nach jedem Schritt prüft die KI selbst, ob es besser geworden ist."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted }
                            Label { visible: !window.hasDiag; text: "Erst einen Stick untersuchen."; color: theme.muted }
                            Repeater {
                                model: diag.plan || []
                                delegate: Rectangle { Layout.fillWidth: true; radius: 12; color: "#f8fafc"; border.color: theme.line; implicitHeight: planRow.implicitHeight + 20
                                    RowLayout { id: planRow; anchors.fill: parent; anchors.margins: 10; spacing: 12
                                        Label { text: (index + 1) + "."; font.bold: true; color: theme.accentText }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                                            RowLayout { Label { text: modelData.title; font.bold: true }
                                                Pill { text: modelData.riskText; level: modelData.manual ? "blue" : window.riskLevel(modelData.risk) }
                                                Pill { visible: modelData.learned !== ""; text: "Erfahrung: " + modelData.learned; level: "good" } }
                                            Label { text: modelData.why; color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                                        ActionButton { text: modelData.manual ? "Anleitung" : "Ausführen"; enabled: !controller.busy; danger: modelData.risk === 3 && !modelData.manual; onClicked: controller.runAction(modelData.id) }
                                    } }
                            }
                        }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Optionen"; iconName: "home" }
                            CheckBox { text: "Stick enthält keine wichtigen Daten (schaltet Kapazitätstest und Formatieren auch ohne Abbild frei)"; checked: controller.stickIsEmpty; onToggled: controller.stickIsEmpty = checked }
                            RowLayout { Label { text: "Dateisystem beim Neuformatieren / Fake-Korrektur:" }
                                ComboBox { model: ["exfat", "fat32", "ntfs"]; currentIndex: Math.max(0, model.indexOf(controller.formatFs)); onActivated: controller.formatFs = currentText } }
                        }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Einzelne Werkzeuge"; iconName: "tool" }
                            Flow { Layout.fillWidth: true; spacing: 8
                                ActionButton { text: "Oberflächentest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("surface_scan") }
                                ActionButton { text: "Kapazitätstest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("capacity_test") }
                                ActionButton { text: "Schreibtest"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("write_test") }
                                ActionButton { text: "USB-Gerät neu starten"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("restart_device") }
                                ActionButton { text: "Schreibschutzattribut entfernen"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("clear_readonly") }
                                ActionButton { text: "Bootsektor wiederherstellen"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("restore_boot") }
                                ActionButton { text: "Partitionstabelle neu"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("rebuild_mbr") }
                                ActionButton { text: "Auf echte Größe einrichten"; danger: true; enabled: !controller.busy && window.dev !== null && diag.fake === true; onClicked: controller.runAction("fix_fake") }
                                ActionButton { text: "Formatieren FAT32"; danger: true; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("format_fat32") }
                                ActionButton { text: "Formatieren exFAT"; danger: true; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("format_exfat") }
                                ActionButton { text: "Neuflash-Anleitung"; enabled: !controller.busy && window.dev !== null; onClicked: controller.runAction("reflash_guide") }
                            }
                        }
                        Card { visible: window.hasDiag && diag.speedCurve && diag.speedCurve.length > 0; Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Lesegeschwindigkeit über den Stick (Oberflächentest)"; iconName: "clock" }
                            Canvas {
                                id: curve; Layout.fillWidth: true; implicitHeight: 140
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
                                        var h = (height - 18) * v[k] / max
                                        c.fillStyle = v[k] < max * 0.3 ? "#ef4444" : "#3b82f6"
                                        c.fillRect(k * w + 1, height - 14 - h, Math.max(1, w - 2), h)
                                    }
                                    c.fillStyle = "#667085"; c.font = "11px sans-serif"
                                    c.fillText("Anfang", 0, height - 2); c.fillText("Ende", width - 30, height - 2)
                                    c.fillText(max.toFixed(1) + " MB/s", 4, 12)
                                }
                            }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 4 KI-Live-Monitor
                ColumnLayout {
                    spacing: 10
                    RowLayout { Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 16
                        Label { text: "KI-Live-Monitor"; font.pixelSize: 22; font.bold: true; color: theme.strong; Layout.fillWidth: true }
                        ComboBox { id: kindFilter; model: ["Alle", "Messung", "Hypothese", "Entscheidung", "Aktion", "Prüfung", "Lernen", "Warnung", "Fehler", "Info"] }
                        ActionButton { text: "Leeren"; onClicked: controller.clearMonitor() }
                    }
                    Label { Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                        text: "Hier siehst du live, was die KI misst, welche Hypothesen sie bildet, wie sie entscheidet, was sie ausführt, wie sie das Ergebnis prüft und was sie daraus lernt." }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 16; Layout.topMargin: 0
                        radius: 14; color: "#ffffff"; border.color: theme.line
                        ListView {
                            id: monList
                            anchors.fill: parent; anchors.margins: 10; clip: true; spacing: 2
                            model: controller.monitor
                            onCountChanged: positionViewAtEnd()
                            ScrollBar.vertical: ScrollBar {}
                            delegate: RowLayout {
                                width: monList.width - 12; spacing: 8
                                visible: kindFilter.currentIndex === 0 || modelData.kind === kindFilter.currentText
                                height: visible ? implicitHeight : 0
                                Label { text: modelData.time; font.family: theme.mono; color: theme.muted; font.pixelSize: 12; Layout.alignment: Qt.AlignTop }
                                Pill { text: modelData.kind; level: window.kindLevel(modelData.kind); Layout.preferredWidth: 96; Layout.alignment: Qt.AlignTop }
                                Label { text: modelData.text; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 13 }
                            }
                        }
                    }
                }

                // ============================================ 5 Protokolle
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 10
                        Item { implicitHeight: 4 }
                        Label { Layout.leftMargin: 16; text: "Protokolle (übersetzt)"; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        Label { Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                            text: "Aus Ereignisanzeige (System, Kernel-PnP), setupapi.dev.log und Geräte-Manager der letzten 14 Tage. „Zugeordnet“ heißt: eindeutig dieser Stick." }
                        Label { visible: !window.hasDiag || diag.logs.length === 0; Layout.leftMargin: 16; color: theme.muted; text: "Keine passenden Einträge." }
                        Repeater {
                            model: window.hasDiag ? diag.logs : []
                            delegate: Card { Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 4
                                RowLayout { Label { text: modelData.time; font.family: theme.mono; color: theme.muted }
                                    Label { text: modelData.source + (modelData.id ? " · " + modelData.id : ""); font.bold: true; Layout.fillWidth: true }
                                    Pill { text: modelData.matched ? "zugeordnet" : "allgemein"; level: modelData.matched ? "blue" : "grey" }
                                    Pill { text: modelData.weight >= 3 ? "schwer" : modelData.weight === 2 ? "mittel" : "leicht"; level: modelData.weight >= 3 ? "bad" : modelData.weight === 2 ? "warn" : "grey" } }
                                Label { text: modelData.meaning; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.strong }
                                Label { visible: modelData.raw !== ""; text: modelData.raw; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted; font.pixelSize: 12 }
                            }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 6 Wissensbasis
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        Label { Layout.leftMargin: 16; text: "Wissensbasis"; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        RowLayout { Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 14
                            Tile { label: "Reparaturversuche"; value: controller.knowledge.actions; note: controller.knowledge.successes + " erfolgreich"; level: "good" }
                            Tile { label: "Bewertete Diagnosen"; value: controller.knowledge.diagnoses; note: "durch deine Bestätigung" }
                            Tile { label: "Geräte"; value: controller.knowledge.devices; note: "verschiedene Kennungen" }
                            Tile { label: "Startwissen"; value: controller.knowledge.controllers; note: "Controller-Kennungen" }
                        }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Zuletzt gelernt"; iconName: "box" }
                            Label { text: "Datei: " + controller.knowledge.path; color: theme.muted; font.pixelSize: 12 }
                            Repeater { model: controller.knowledge.recent
                                delegate: RowLayout { Layout.fillWidth: true
                                    Label { text: modelData.time; font.family: theme.mono; color: theme.muted; font.pixelSize: 12 }
                                    Pill { text: modelData.kind; level: "blue" }
                                    Label { text: modelData.device + " · " + modelData.what + (modelData.symptoms ? " · [" + modelData.symptoms + "]" : ""); Layout.fillWidth: true; elide: Text.ElideRight }
                                    Pill { text: modelData.ok ? (modelData.kind === "Versuch" ? "geholfen" : "bestätigt") : (modelData.kind === "Versuch" ? "ohne Erfolg" : "verworfen"); level: modelData.ok ? "good" : "warn" } } }
                        }
                        Item { implicitHeight: 16 }
                    }
                }

                // ============================================ 7 Einstellungen
                ScrollView {
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 14
                        Item { implicitHeight: 4 }
                        Label { Layout.leftMargin: 16; text: "Einstellungen"; font.pixelSize: 22; font.bold: true; color: theme.strong }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Lokale KI (optional, ohne Internet)"; iconName: "chip" }
                            Label { text: "OpenAI-kompatibler Endpunkt, z. B. der llama-server von Butchies KI Studio. Leer lassen = eingebauter Assistent (funktioniert immer)."; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: theme.muted }
                            TextField { Layout.fillWidth: true; text: controller.llmUrl; placeholderText: "http://butchies-ki.local:8081/v1/chat/completions"; onEditingFinished: controller.llmUrl = text }
                            RowLayout { Label { text: "Modellname:" }
                                TextField { Layout.preferredWidth: 260; text: controller.llmModel; onEditingFinished: controller.llmModel = text } }
                        }
                        Card { Layout.leftMargin: 16; Layout.rightMargin: 16
                            Heading { text: "Sicherheit"; iconName: "warn" }
                            Label { wrapMode: Text.WordWrap; Layout.fillWidth: true
                                text: "• Interne Laufwerke und der Systemdatenträger werden nie angezeigt.\n• Alles ist zuerst nur lesend.\n• Jeder Schreibzugriff braucht deine Freigabe; Originalsektoren werden vorher in die Rückgängig-Datei der Fallakte gesichert.\n• Kapazitätstest und Formatieren sind ohne Rettungsabbild gesperrt (oder mit „Stick ist leer“).\n• Ein Controller-Neuflash wird nur erklärt, nie selbst ausgeführt." }
                        }
                    }
                }
            }
        }

        // --------------------------------------------------- KI-Assistent
        Rectangle {
            Layout.fillHeight: true; Layout.preferredWidth: 340; color: "#ffffff"; border.color: theme.line
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                RowLayout { Image { source: window.icon("spark", "blue"); sourceSize: Qt.size(20, 20) }
                    Label { text: "KI-Assistent"; font.pixelSize: 16; font.bold: true; color: theme.strong; Layout.fillWidth: true }
                    Pill { text: controller.llmUrl !== "" ? "lokale KI" : "eingebaut"; level: "blue" } }
                Label { visible: controller.chat.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.muted
                    text: "Frag mich zum ausgewählten Stick, z. B. „Kann ich meine Fotos retten?“, „Warum glaubst du das?“ oder „Was steht in den Protokollen?“" }
                ListView {
                    id: chatList
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                    model: controller.chat
                    onCountChanged: positionViewAtEnd()
                    delegate: Rectangle {
                        width: chatList.width
                        radius: 12
                        color: modelData.from === "du" ? theme.accentSoft : "#f8fafc"
                        border.color: theme.line
                        implicitHeight: chatText.implicitHeight + 18
                        Label { id: chatText; anchors.fill: parent; anchors.margins: 9; text: modelData.text; wrapMode: Text.WordWrap
                            color: modelData.from === "du" ? theme.accentText : theme.text; textFormat: Text.PlainText }
                    }
                }
                RowLayout {
                    TextField { id: askField; Layout.fillWidth: true; placeholderText: "Frage stellen …"; onAccepted: { controller.ask(text); text = "" } }
                    ActionButton { text: "Senden"; primary: true; onClicked: { controller.ask(askField.text); askField.text = "" } }
                }
            }
        }
    }
}
