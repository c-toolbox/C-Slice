/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore

import org.kde.kirigami as Kirigami

Dialog {
    id: dialog

    property var controller
    property string outputFile: ""
    property string lastAudioFolder: ""
    property var channelLayouts: defaultChannelLayouts()
    readonly property var layoutNames: channelLayouts.map(function(layout) { return layout.name })

    title: qsTr("Audio Muxer")
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 4 : 1180, 1180)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 4 : 820, 820)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.smallSpacing
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.16)
    }

    onOpened: {
        loadChannelLayouts()
        if (lastAudioFolder.length === 0) {
            lastAudioFolder = app.urlToPath(StandardPaths.writableLocation(StandardPaths.MusicLocation))
        }
        if (outputFile.length === 0) {
            outputFile = defaultOutputFile()
        }
    }

    function defaultChannelLayouts() {
        return [
            { name: qsTr("Stereo"), channels: [ qsTr("0: Left"), qsTr("1: Right") ] },
            { name: qsTr("5.1"), channels: [ qsTr("0: Front left"), qsTr("1: Front right"), qsTr("2: Front center"), qsTr("3: Low frequency"), qsTr("4: Back left"), qsTr("5: Back right") ] },
            { name: qsTr("7.1"), channels: [ qsTr("0: Front left"), qsTr("1: Front right"), qsTr("2: Front center"), qsTr("3: Low frequency"), qsTr("4: Back left"), qsTr("5: Back right"), qsTr("6: Side left"), qsTr("7: Side right") ] },
            { name: qsTr("9.1"), channels: [ qsTr("0: Front left"), qsTr("1: Front right"), qsTr("2: Front center"), qsTr("3: Low frequency"), qsTr("4: Back left"), qsTr("5: Back right"), qsTr("6: Side left"), qsTr("7: Side right"), qsTr("8: Upper front left"), qsTr("9: Upper front right") ] },
            { name: qsTr("Nrkp Dome"), channels: [ qsTr("01: Front left"), qsTr("02: Front right"), qsTr("03: Front center"), qsTr("04: Low frequency"), qsTr("05: Side left"), qsTr("06: Side right"), qsTr("07: Door left"), qsTr("08: Door right"), qsTr("09: TG CH1"), qsTr("10: TG CH2"), qsTr("11: TG CH3"), qsTr("12: TG CH4") ] }
        ]
    }

    function loadChannelLayouts() {
        const previousLayout = activeChannelLayout()
        const previousName = previousLayout ? previousLayout.name : ""
        let layouts = []
        try {
            const text = app.readTextFile(app.sliceDataPath("audio-channel-layouts.json"))
            if (text.length > 0) {
                const json = JSON.parse(text)
                const sourceLayouts = Array.isArray(json.layouts) ? json.layouts : []
                for (let i = 0; i < sourceLayouts.length; ++i) {
                    const sourceLayout = sourceLayouts[i]
                    const sourceChannels = Array.isArray(sourceLayout.channels) ? sourceLayout.channels : []
                    const channels = []
                    for (let channelIndex = 0; channelIndex < Math.min(channelModel.count, sourceChannels.length); ++channelIndex) {
                        channels.push(String(sourceChannels[channelIndex]))
                    }
                    if (sourceLayout.name && channels.length > 0) {
                        layouts.push({ name: String(sourceLayout.name), channels: channels })
                    }
                }
            }
        }
        catch (error) {
            console.warn("Failed to load Audio Muxer channel layouts:", error)
        }

        if (layouts.length === 0) {
            layouts = defaultChannelLayouts()
        }

        channelLayouts = layouts
        let index = channelLayouts.findIndex(function(layout) { return layout.name === previousName })
        if (index < 0) {
            index = channelLayouts.findIndex(function(layout) { return layout.name === "Nrkp Dome" })
        }
        channelLayoutCombo.currentIndex = index >= 0 ? index : 0
    }

    function activeChannelLayout() {
        if (!channelLayouts || channelLayouts.length === 0) {
            return null
        }
        const index = Math.max(0, Math.min(channelLayoutCombo.currentIndex, channelLayouts.length - 1))
        return channelLayouts[index]
    }

    function activeChannelCount() {
        const layout = activeChannelLayout()
        return layout ? layout.channels.length : 0
    }

    function channelLabel(index) {
        const layout = activeChannelLayout()
        return layout && index < layout.channels.length ? layout.channels[index] : index + ":"
    }

    function channelsForController() {
        const channels = []
        for (let i = 0; i < channelModel.count; ++i) {
            const item = channelModel.get(i)
            channels.push({ checked: item.checked, file: item.file, gain: item.gain })
        }
        return channels
    }

    function ensureWav(path) {
        if (path.length === 0) {
            return path
        }
        return path.toLowerCase().endsWith(".wav") ? path : path + ".wav"
    }

    function pathSeparator() {
        return Qt.platform.os === "windows" ? "\\" : "/"
    }

    function joinPath(directory, fileName) {
        if (directory.length === 0) {
            return fileName
        }
        if (directory.endsWith("/") || directory.endsWith("\\")) {
            return directory + fileName
        }
        return directory + pathSeparator() + fileName
    }

    function parentFolder(path) {
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return slash > 0 ? path.substring(0, slash) : lastAudioFolder
    }

    function defaultOutputFile() {
        const baseName = controller && controller.outputName.length > 0 ? controller.outputName + "_audio" : "audio"
        const directory = controller ? controller.outputDirectory : ""
        return ensureWav(joinPath(directory, baseName))
    }

    function outputIsValid() {
        return outputFile.trim().length > 0
    }

    function channelIsActive(index) {
        return index < activeChannelCount()
    }

    function channelIsInvalid(index, item) {
        return channelIsActive(index) && item.checked && item.file.trim().length === 0
    }

    function validationText() {
        if (!outputIsValid()) {
            return qsTr("Choose an output WAV filename.")
        }
        for (let i = 0; i < activeChannelCount(); ++i) {
            const item = channelModel.get(i)
            if (item.checked && item.file.trim().length === 0) {
                return qsTr("Choose an input WAV file for channel %1 or uncheck the channel to create silence.").arg(i + 1)
            }
        }
        return ""
    }

    function canMux() {
        return controller && !controller.audioMuxRunning && validationText().length === 0
    }

    ListModel {
        id: channelModel
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
        ListElement { checked: true; file: ""; gain: 100 }
    }

    FileDialog {
        id: audioInputDialog
        property int channelIndex: -1
        parentWindow: dialog.Window.window
        title: qsTr("Choose audio channel WAV")
        currentFolder: app.pathToUrl(lastAudioFolder)
        nameFilters: [ qsTr("WAV files (*.wav)"), qsTr("Audio files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.ogg)"), qsTr("All files (*)") ]
        onAccepted: {
            const path = app.urlToPath(selectedFile)
            if (channelIndex >= 0) {
                channelModel.setProperty(channelIndex, "file", path)
            }
            lastAudioFolder = parentFolder(path)
        }
    }

    FileDialog {
        id: audioOutputDialog
        parentWindow: dialog.Window.window
        title: qsTr("Choose output WAV")
        fileMode: FileDialog.SaveFile
        currentFolder: app.pathToUrl(parentFolder(outputFile.length > 0 ? outputFile : defaultOutputFile()))
        nameFilters: [ qsTr("WAV files (*.wav)"), qsTr("All files (*)") ]
        defaultSuffix: "wav"
        onAccepted: outputFile = ensureWav(app.urlToPath(selectedFile))
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: validationText().length === 0 ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
                    icon.source: validationText().length === 0 ? "dialog-information" : "dialog-warning"
                    visible: true
                    text: validationText().length === 0
                        ? qsTr("Mux mono WAV channel files into one multi-channel WAV. Unchecked channels are filled with silence.")
                        : validationText()
                }

                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.largeSpacing
                    Kirigami.Theme.colorSet: Kirigami.Theme.View
                    Kirigami.Theme.inherit: false

                    background: Rectangle {
                        color: Kirigami.Theme.backgroundColor
                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
                        radius: Kirigami.Units.smallSpacing
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: "audio-x-generic"
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                color: Kirigami.Theme.textColor
                            }
                            Kirigami.Heading {
                                text: qsTr("Output")
                                level: 2
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("FFmpeg %1").arg(app.ffmpegVersion.split("-")[0])
                                opacity: 0.65
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: Kirigami.Units.smallSpacing
                            columnSpacing: Kirigami.Units.smallSpacing

                            Label { text: qsTr("Channel layout"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.86 }
                            ComboBox {
                                id: channelLayoutCombo
                                Layout.fillWidth: true
                                model: layoutNames
                                currentIndex: 4
                            }
                            Item { Layout.preferredWidth: 1 }

                            Label { text: qsTr("Output WAV"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.86 }
                            TextField {
                                Layout.fillWidth: true
                                text: outputFile
                                placeholderText: qsTr("Output WAV file")
                                selectByMouse: true
                                onTextEdited: outputFile = text
                                background: Rectangle {
                                    color: outputIsValid() ? Kirigami.Theme.backgroundColor : Qt.rgba(1.0, 0.25, 0.25, 0.18)
                                    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25)
                                    radius: Kirigami.Units.smallSpacing / 2
                                }
                            }
                            Button {
                                text: qsTr("Browse…")
                                icon.name: "document-save-as"
                                icon.color: Kirigami.Theme.textColor
                                onClicked: audioOutputDialog.open()
                            }

                            Label { text: qsTr("Global volume"); Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; opacity: 0.86 }
                            Slider {
                                id: globalVolumeSlider
                                Layout.fillWidth: true
                                from: 0
                                to: 200
                                stepSize: 1
                                value: 100
                                snapMode: Slider.SnapAlways
                            }
                            Label {
                                text: qsTr("%1 %").arg(Math.round(globalVolumeSlider.value))
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                            }
                        }
                    }
                }

                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.largeSpacing
                    Kirigami.Theme.colorSet: Kirigami.Theme.View
                    Kirigami.Theme.inherit: false

                    background: Rectangle {
                        color: Kirigami.Theme.backgroundColor
                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
                        radius: Kirigami.Units.smallSpacing
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: "new-audio-alarm"
                                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                color: Kirigami.Theme.textColor
                            }
                            Kirigami.Heading {
                                text: qsTr("Inputs")
                                level: 2
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("%1 channels").arg(activeChannelCount())
                                opacity: 0.65
                            }
                        }

                        ScrollView {
                            id: inputsScrollView
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(Kirigami.Units.gridUnit * 18, Math.max(Kirigami.Units.gridUnit * 6, channelModel.count * Kirigami.Units.gridUnit * 2.4))
                            clip: true

                            GridLayout {
                                width: Math.max(inputsScrollView.availableWidth, implicitWidth)
                                columns: 5
                                rowSpacing: Kirigami.Units.smallSpacing / 2
                                columnSpacing: Kirigami.Units.smallSpacing

                                Label { text: qsTr("Use"); font.bold: true; opacity: 0.8 }
                                Label { text: qsTr("Channel"); font.bold: true; opacity: 0.8 }
                                Label { text: qsTr("Input WAV"); font.bold: true; opacity: 0.8; Layout.fillWidth: true }
                                Label { text: qsTr("Gain %"); font.bold: true; opacity: 0.8 }
                                Label { text: "" }

                                Repeater {
                                    model: channelModel

                                    delegate: Item {
                                        id: rowRoot
                                        readonly property bool active: channelIsActive(index)
                                        readonly property bool invalid: channelIsInvalid(index, model)
                                        Layout.fillWidth: true
                                        Layout.columnSpan: 5
                                        implicitHeight: channelRow.implicitHeight
                                        opacity: active ? 1.0 : 0.42

                                        GridLayout {
                                            id: channelRow
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            columns: 5
                                            columnSpacing: Kirigami.Units.smallSpacing

                                            CheckBox {
                                                enabled: rowRoot.active
                                                checked: model.checked
                                                onToggled: channelModel.setProperty(index, "checked", checked)
                                                ToolTip.text: checked ? qsTr("Use this channel file") : qsTr("Create silence for this channel")
                                                ToolTip.visible: hovered
                                            }

                                            Label {
                                                text: channelLabel(index)
                                                Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                                                elide: Text.ElideRight
                                            }

                                            TextField {
                                                Layout.fillWidth: true
                                                enabled: rowRoot.active && model.checked
                                                text: model.file
                                                placeholderText: rowRoot.active ? qsTr("WAV file") : qsTr("Disabled for this layout")
                                                selectByMouse: true
                                                onTextEdited: channelModel.setProperty(index, "file", text)
                                                background: Rectangle {
                                                    color: rowRoot.invalid ? Qt.rgba(1.0, 0.25, 0.25, 0.18) : Kirigami.Theme.backgroundColor
                                                    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25)
                                                    radius: Kirigami.Units.smallSpacing / 2
                                                }
                                            }

                                            SpinBox {
                                                enabled: rowRoot.active
                                                from: 0
                                                to: 500
                                                value: model.gain
                                                editable: true
                                                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                                onValueModified: channelModel.setProperty(index, "gain", value)
                                            }

                                            Button {
                                                enabled: rowRoot.active && model.checked
                                                text: qsTr("Browse…")
                                                icon.name: "document-open"
                                                icon.color: Kirigami.Theme.textColor
                                                display: AbstractButton.TextBesideIcon
                                                Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                                onClicked: {
                                                    audioInputDialog.channelIndex = index
                                                    audioInputDialog.currentFolder = app.pathToUrl(model.file.length > 0 ? parentFolder(model.file) : lastAudioFolder)
                                                    audioInputDialog.open()
                                                }
                                                ToolTip.text: qsTr("Choose input WAV for this channel")
                                                ToolTip.visible: hovered
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.largeSpacing
                    Kirigami.Theme.colorSet: Kirigami.Theme.View
                    Kirigami.Theme.inherit: false

                    background: Rectangle {
                        color: Kirigami.Theme.backgroundColor
                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
                        radius: Kirigami.Units.smallSpacing
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading {
                                text: qsTr("Command preview")
                                level: 3
                                Layout.fillWidth: true
                            }
                        }

                        ScrollView {
                            id: commandPreviewScrollView
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                            contentWidth: availableWidth
                            clip: true

                            TextArea {
                                width: commandPreviewScrollView.availableWidth
                                readOnly: true
                                wrapMode: Text.WrapAnywhere
                                text: controller ? controller.buildAudioMuxCommandLine(outputFile, activeChannelCount(), Math.round(globalVolumeSlider.value), channelsForController()) : ""
                                font.family: "monospace"
                                opacity: 0.9
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: qsTr("Reset")
                icon.name: "edit-reset"
                icon.color: Kirigami.Theme.textColor
                enabled: !controller.audioMuxRunning
                onClicked: {
                    outputFile = defaultOutputFile()
                    globalVolumeSlider.value = 100
                    const nrkpIndex = channelLayouts.findIndex(function(layout) { return layout.name === "Nrkp Dome" })
                    channelLayoutCombo.currentIndex = nrkpIndex >= 0 ? nrkpIndex : 0
                    for (let i = 0; i < channelModel.count; ++i) {
                        channelModel.setProperty(i, "checked", true)
                        channelModel.setProperty(i, "file", "")
                        channelModel.setProperty(i, "gain", 100)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Close")
                icon.name: "dialog-close"
                icon.color: Kirigami.Theme.textColor
                onClicked: dialog.close()
            }
            Button {
                visible: controller.audioMuxRunning
                text: qsTr("Abort")
                icon.name: "process-stop"
                icon.color: Kirigami.Theme.textColor
                onClicked: controller.abortAudioMux()
            }
            Button {
                text: controller.audioMuxRunning ? qsTr("Muxing…") : qsTr("Mux")
                icon.name: "media-playback-start"
                icon.color: Kirigami.Theme.textColor
                enabled: canMux()
                onClicked: controller.muxAudio(outputFile, activeChannelCount(), Math.round(globalVolumeSlider.value), channelsForController())
            }
        }
    }
}
