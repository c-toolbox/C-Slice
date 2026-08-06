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
import QtQuick.Window
import QtCore

import org.kde.kirigami as Kirigami
import org.ctoolbox.cslice

Kirigami.ApplicationWindow {
    id: window

    readonly property var controller: app.controller
    readonly property bool compactHeader: width < 1300
    property real inputFrameRateCustomNum: 30
    property real inputFrameRateCustomDen: 1

    title: qsTr("C-Slice %1").arg(app.version())
    visible: true
    color: Kirigami.Theme.alternateBackgroundColor
    minimumWidth: 1120
    minimumHeight: 720
    width: 1360
    height: 1024

    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Window

    FileDialog {
        id: saveQueueDialog
        title: qsTr("Save Job Queue")
        fileMode: FileDialog.SaveFile
        nameFilters: [ qsTr("C-Slice Job Queue (*.json)") ]
        onAccepted: controller.saveInternalQueue(app.urlToPath(selectedFile))
    }

    FileDialog {
        id: loadQueueDialog
        title: qsTr("Load Job Queue")
        fileMode: FileDialog.OpenFile
        nameFilters: [ qsTr("C-Slice Job Queue (*.json)") ]
        onAccepted: {
            if (controller.internalQueueSize > 0) {
                replaceQueueDialog.filePath = app.urlToPath(selectedFile)
                replaceQueueDialog.open()
            } else {
                controller.loadInternalQueue(app.urlToPath(selectedFile))
            }
        }
    }

    Dialog {
        id: replaceQueueDialog
        property string filePath: ""
        title: qsTr("Replace Job Queue?")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: controller.loadInternalQueue(filePath)
        Label { text: qsTr("Loading a queue replaces all current queued jobs.") }
    }

    Component.onCompleted: {
        if (controller.outputDirectory.length === 0) {
            controller.outputDirectory = app.urlToPath(StandardPaths.writableLocation(StandardPaths.MoviesLocation))
        }
    }

    Connections {
        target: controller
        function onInputFrameRateNumChanged() {
            const index = inputFrameRatePresetIndex(controller.inputFrameRateNum, controller.inputFrameRateDen)
            if (index !== 4) {
                setInputFrameRateCustomValues(index)
                setFrameRatePreset(outputFrameRatePresetIndex(index))
            } else {
                syncInputFrameRateFromController()
            }
        }
        function onInputFrameRateDenChanged() {
            const index = inputFrameRatePresetIndex(controller.inputFrameRateNum, controller.inputFrameRateDen)
            if (index !== 4) {
                setInputFrameRateCustomValues(index)
                setFrameRatePreset(outputFrameRatePresetIndex(index))
            } else {
                syncInputFrameRateFromController()
            }
        }
    }

    function externalVersion(version) {
        const versionNumber = String(version).split("-")[0]
        let cleanVersion = ""
        for (let i = 0; i < versionNumber.length; ++i) {
            const character = versionNumber.charAt(i)
            if (!/[A-Za-z]/.test(character)) {
                cleanVersion += character
            }
        }
        return cleanVersion
    }

    function isMovieCodec(codec) {
        return codec !== "PNG" && codec !== "JPEG" && codec !== "TGA"
    }

    function isVideoInput() {
        return controller.inputType === "Video"
    }

    function hasQuality(codec) {
        return codec !== "Hap" && codec !== "FFV1"
    }

    function isCrfCodec(codec) {
        return codec === "H264" || codec === "H265"
    }

    function hasPreset(codec) {
        return codec === "H264" || codec === "H265" || codec === "H264 NVENC" || codec === "H265 NVENC"
    }

    function isNvencCodec(codec) {
        return codec === "H264 NVENC" || codec === "H265 NVENC"
    }

    function presetModel(codec) {
        if (isNvencCodec(codec)) {
            return [
                qsTr("Fastest (P1)"),
                qsTr("Very fast (P2)"),
                qsTr("Faster (P3)"),
                qsTr("Balanced fast (P4)"),
                qsTr("Balanced quality (P5)"),
                qsTr("High quality (P6)"),
                qsTr("Best quality (P7)")
            ]
        }
        return [ "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" ]
    }

    function isTuneCodec(codec) {
        return codec === "H264" || codec === "H265" || isNvencCodec(codec)
    }

    function tuneModel(codec) {
        if (isNvencCodec(codec)) {
            return [ qsTr("High quality"), qsTr("Low latency"), qsTr("Ultra low latency"), qsTr("Lossless") ]
        }
        return [ qsTr("none"), "film", "animation", "grain", "stillimage", "psnr", "ssim", "fastdecode", "zerolatency" ]
    }

    function tuneValue(codec) {
        return isNvencCodec(codec) ? controller.nvencTune : controller.libxTune
    }

    function setTuneValue(codec, value) {
        if (isNvencCodec(codec)) {
            controller.nvencTune = value
        }
        else {
            controller.libxTune = value
        }
    }

    function supportsEncodingBitDepth(codec) {
        return codec === "H264" || codec === "H265" || codec === "H264 NVENC" || codec === "H265 NVENC" || codec === "ProRes" || codec === "FFV1"
    }

    function hasBitrate(codec) {
        return isMovieCodec(codec) && !hasQuality(codec) && codec !== "FFV1"
    }

    function qualityLabel(codec) {
        if (isNvencCodec(codec)) {
            return qsTr("CQ")
        }
        return isCrfCodec(codec) ? qsTr("CRF") : qsTr("QScale")
    }

    function qualityValue(codec) {
        if (isNvencCodec(codec)) {
            return controller.cq
        }
        return isCrfCodec(codec) ? controller.crf : controller.qscale
    }

    function setQualityValue(codec, value) {
        if (isNvencCodec(codec)) {
            controller.cq = value
        }
        else if (isCrfCodec(codec)) {
            controller.crf = value
        }
        else {
            controller.qscale = value
        }
    }

    function frameRatePresetIndex(num, den) {
        if (num === 1 && den === 30) {
            return 0
        }
        if (num === 1 && den === 60) {
            return 1
        }
        if (num === 1001 && den === 30000) {
            return 2
        }
        if (num === 1001 && den === 60000) {
            return 3
        }
        return 4
    }

    function setFrameRatePreset(index) {
        if (index === 0) {
            controller.frameRateNum = 1
            controller.frameRateDen = 30
        }
        else if (index === 1) {
            controller.frameRateNum = 1
            controller.frameRateDen = 60
        }
        else if (index === 2) {
            controller.frameRateNum = 1001
            controller.frameRateDen = 30000
        }
        else if (index === 3) {
            controller.frameRateNum = 1001
            controller.frameRateDen = 60000
        }
    }

    function roundedValue(value, decimals) {
        const scale = Math.pow(10, decimals)
        return Math.round(Number(value) * scale) / scale
    }

    // Exact NTSC fps values, shared by the input frame-rate combo below.
    readonly property real ntscFps2997: 30000 / 1001
    readonly property real ntscFps5994: 60000 / 1001

    function inputFrameRatePresetIndex(num, den) {
        const fps = num / den
        const tolerance = 0.01
        if (num === 30000 && den === 1001) {
            return 2
        }
        if (num === 60000 && den === 1001) {
            return 3
        }
        if (num === 30 && den === 1) {
            return 0
        }
        if (num === 60 && den === 1) {
            return 1
        }
        return 4
    }

    function outputFrameRatePresetIndex(inputPresetIndex) {
        if (controller.preferNtscOutputFrameRates) {
            if (inputPresetIndex === 0) {
                return 2
            }
            if (inputPresetIndex === 1) {
                return 3
            }
        }
        return inputPresetIndex
    }

    function setInputFrameRateCustomValues(index) {
        if (index === 0) {
            inputFrameRateCustomNum = 30
            inputFrameRateCustomDen = 1
        }
        else if (index === 1) {
            inputFrameRateCustomNum = 60
            inputFrameRateCustomDen = 1
        }
        else if (index === 2) {
            inputFrameRateCustomNum = 30000
            inputFrameRateCustomDen = 1001
        }
        else if (index === 3) {
            inputFrameRateCustomNum = 60000
            inputFrameRateCustomDen = 1001
        }
    }

    function setInputFrameRatePreset(index) {
        if (index !== 4) {
            setInputFrameRateCustomValues(index)
        }
        controller.inputFrameRateNum = inputFrameRateCustomNum
        controller.inputFrameRateDen = inputFrameRateCustomDen
    }

    function syncInputFrameRateFromController() {
        // When in Custom mode, ensure the custom values match the controller's actual values
        if (inputFrameRatePresetCombo.currentIndex === 4) {
            inputFrameRateCustomNum = controller.inputFrameRateNum
            inputFrameRateCustomDen = controller.inputFrameRateDen
        }
    }

    function stereoModeIndex(mode) {
        const modes = [ "2D (mono)", "3D (side-by-side)", "3D (top-bottom)", "3D (top-bottom+flip)" ]
        return Math.max(0, modes.indexOf(mode))
    }

    function decimalSpinText(value, locale) {
        return Number(value / 10).toLocaleString(locale, "f", 1)
    }

    function decimalSpinValue(text, locale) {
        return Math.round(Number.fromLocaleString(locale, text) * 10)
    }

    function updateEncodingPreset() {
        const currentPreset = controller.preset
        const presets = presetModel(controller.codec)
        presetCombo.currentIndex = Math.max(0, presets.indexOf(currentPreset))
    }

    component FormLabel: Label {
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        opacity: enabled ? 0.86 : 0.45
        horizontalAlignment: Text.AlignRight
    }
    component FormComment: Label {
        Layout.alignment: Qt.AlignLeft
        horizontalAlignment: Text.AlignLeft
        font.italic: true
        font.pixelSize: 10
        opacity: 0.86
    }

    component BrowseButton: Button {
        property var dialog
        text: qsTr("Browse…")
        icon.name: "document-open"
        icon.color: Kirigami.Theme.textColor
        display: window.width < 1180 ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        onClicked: dialog.open()
    }

    component SectionPane: Pane {
        id: section

        property string title
        property string iconName
        property string trailingText
        default property alias sectionContent: sectionContent.data

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
                    source: section.iconName
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    color: Kirigami.Theme.textColor
                }
                Kirigami.Heading {
                    text: section.title
                    level: 2
                    Layout.fillWidth: true
                }
                Label {
                    visible: section.trailingText.length > 0
                    text: section.trailingText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    opacity: 0.65
                    Layout.maximumWidth: section.width * 0.45
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }
                Rectangle {
                    visible: section.trailingText.length === 0
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                    height: 1
                    color: Kirigami.Theme.alternateBackgroundColor
                    opacity: 0.8
                }
            }

            ColumnLayout {
                id: sectionContent
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.smallSpacing
            }
            Item { Layout.fillHeight: true }
        }
    }

    Action {
        id: startAction
        text: qsTr("Start")
        icon.name: "media-playback-start"
        enabled: !controller.running
        onTriggered: controller.launchSlice()
    }
    Action {
        id: verifyAction
        text: qsTr("Verify")
        icon.name: "dialog-ok"
        enabled: !controller.running && controller.inputType !== "Video"
        onTriggered: controller.verifySliceInputs()
    }
    Action {
        id: abortAction
        text: qsTr("Abort")
        icon.name: "process-stop"
        enabled: controller.running
        onTriggered: controller.abortSlice()
    }
    Action {
        id: pauseResumeAction
        text: controller.slicePaused ? qsTr("Resume") : qsTr("Pause")
        icon.name: controller.slicePaused ? "media-playback-start" : "media-playback-pause"
        enabled: controller.running
        onTriggered: controller.slicePaused ? controller.resumeSlice() : controller.pauseSlice()
    }
    Action {
        id: clearLogAction
        text: qsTr("Clear Log")
        icon.name: "edit-clear-history"
        onTriggered: controller.clearLog()
    }
    Action {
        id: openOutputDirectoryAction
        text: qsTr("Open Output Directory")
        icon.name: "folder-open"
        onTriggered: controller.openOutputDirectory()
    }
    Action {
        id: audioMuxerAction
        text: qsTr("Audio Muxer")
        icon.name: "new-audio-alarm"
        onTriggered: audioMuxerDialog.open()
    }
    Action {
        id: preferencesAction
        text: qsTr("Preferences")
        icon.name: "configure"
        onTriggered: settingsDialog.open()
    }
    Action {
        id: imageSequencePreviewAction
        text: controller.inputType === "Video" ? qsTr("Video Preview") : qsTr("Image Sequence Preview")
        icon.name: "view-preview"
        onTriggered: {
            sequencePreviewWindow.visible = true
            sequencePreviewWindow.raise()
            sequencePreviewWindow.requestActivate()
        }
    }

    header: ToolBar {
        position: ToolBar.Header
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
        }

        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            ToolButton {
                action: startAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            ToolTip.text: qsTr("Launch C-Slice node mode with the current settings")
                ToolTip.visible: hovered
            }
            ToolButton {
                action: verifyAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                ToolTip.text: qsTr("Verify all selected left and right input images before slicing. Checks that every file can be decoded and that image dimensions match.")
                ToolTip.visible: hovered
            }
            ToolButton {
                action: abortAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                ToolTip.text: qsTr("Abort the running C-Slice node process")
                ToolTip.visible: hovered
            }
            ToolButton {
                action: pauseResumeAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                ToolTip.text: controller.slicePaused ? qsTr("Resume the paused C-Slice node process") : qsTr("Pause the C-Slice node process")
                ToolTip.visible: hovered
            }
            ToolButton {
                action: preferencesAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                ToolTip.text: qsTr("Edit C-Slice startup defaults")
                ToolTip.visible: hovered
            }
            ToolButton {
                action: imageSequencePreviewAction
                focusPolicy: Qt.NoFocus
                icon.color: Kirigami.Theme.textColor
                display: window.compactHeader ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                ToolTip.text: isVideoInput() ? qsTr("Preview the selected left or right video input") : qsTr("Preview the selected left or right image sequence")
                ToolTip.visible: hovered
            }
            Item { Layout.fillWidth: true }
        }
    }

    menuBar: MenuBar {
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
        }

        Menu {
            title: qsTr("File")
            MenuItem { text: qsTr("Choose SGCT Configuration…"); icon.name: "document-open"; onTriggered: configDialog.open() }
            MenuItem { text: isVideoInput() ? qsTr("Choose Left/Input Video…") : qsTr("Choose Left/Input Image(s)…"); icon.name: isVideoInput() ? "video-x-generic" : "insert-image"; enabled: !controller.sequenceIndexing; onTriggered: leftDialog.open() }
            MenuItem { text: isVideoInput() ? qsTr("Choose Right-Eye Video…") : qsTr("Choose Right-Eye Image(s)…"); icon.name: isVideoInput() ? "video-x-generic" : "insert-image"; enabled: controller.stereo && !controller.sequenceIndexing; onTriggered: rightDialog.open() }
            MenuItem { text: qsTr("Choose Output Directory…"); icon.name: "folder-open"; onTriggered: outputDialog.open() }
            MenuItem { action: openOutputDirectoryAction }
            MenuSeparator {}
            MenuItem { text: qsTr("Save Job Queue…"); icon.name: "document-save"; enabled: !controller.running && !controller.internalQueueRunning && !controller.cjobEnabled && !controller.editingQueuedJob; onTriggered: saveQueueDialog.open() }
            MenuItem { text: qsTr("Load Job Queue…"); icon.name: "document-open"; enabled: !controller.running && !controller.internalQueueRunning && !controller.cjobEnabled && !controller.editingQueuedJob; onTriggered: loadQueueDialog.open() }
            MenuSeparator {}
            MenuItem { action: startAction }
            MenuItem { action: verifyAction }
            MenuItem { action: abortAction }
            MenuSeparator {}
            MenuItem { text: qsTr("Quit"); icon.name: "application-exit"; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("Tools")
            MenuItem { action: audioMuxerAction }
            MenuItem { action: imageSequencePreviewAction }
            MenuSeparator {}
            MenuItem { action: preferencesAction }
            MenuSeparator {}
            MenuItem { action: clearLogAction }
        }
        Menu {
            title: qsTr("Help")
            MenuItem {
                text: qsTr("C-Slice Documentation")
                icon.name: "system-help"
                onTriggered: Qt.openUrlExternally("https://c-toolbox.github.io/C-Slice/")
            }
        }
    }

    FileDialog {
        id: configDialog
        parentWindow: window
        title: qsTr("Choose SGCT configuration")
        currentFolder: app.pathToUrl(app.sliceDataPath("configs"))
        nameFilters: [ qsTr("SGCT JSON configurations (*.json)"), qsTr("All files (*)") ]
        onAccepted: controller.configuration = app.urlToPath(selectedFile)
    }
    FileDialog {
        id: leftDialog
        parentWindow: window
        title: isVideoInput() ? qsTr("Choose left/input video") : qsTr("Choose left/input image")
        currentFolder: app.pathToUrl(controller.leftInputDialogLocation)
        nameFilters: isVideoInput() ? [ qsTr("Videos (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg)"), qsTr("All files (*)") ] : [ qsTr("Images (*.png *.jpg *.jpeg *.tga)"), qsTr("All files (*)") ]
        onAccepted: controller.leftInput = app.urlToPath(selectedFile)
    }
    FileDialog {
        id: rightDialog
        parentWindow: window
        title: isVideoInput() ? qsTr("Choose right-eye video") : qsTr("Choose right-eye image")
        currentFolder: app.pathToUrl(controller.rightInputDialogLocation)
        nameFilters: isVideoInput() ? [ qsTr("Videos (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg)"), qsTr("All files (*)") ] : [ qsTr("Images (*.png *.jpg *.jpeg *.tga)"), qsTr("All files (*)") ]
        onAccepted: controller.rightInput = app.urlToPath(selectedFile)
    }
    FileDialog {
        id: parameterDialog
        parentWindow: window
        title: qsTr("Choose FFmpeg parameter JSON")
        currentFolder: app.pathToUrl(app.sliceDataPath("parameters"))
        nameFilters: [ qsTr("JSON files (*.json)"), qsTr("All files (*)") ]
        onAccepted: controller.parameterFile = app.urlToPath(selectedFile)
    }
    FolderDialog {
        id: outputDialog
        parentWindow: window
        title: qsTr("Choose output directory")
        currentFolder: app.pathToUrl(outputDirectoryField.text.length > 0 ? outputDirectoryField.text : controller.outputDirectoryDialogLocation)
        onAccepted: controller.outputDirectory = app.urlToPath(selectedFolder)
    }

    SettingsDialog {
        id: settingsDialog
        controller: window.controller
    }

    AudioMuxerDialog {
        id: audioMuxerDialog
        controller: window.controller
    }

    Kirigami.ApplicationWindow {
        id: sequencePreviewWindow

        property real previewScale: Math.min(sequencePreviewImage.paintedWidth / Math.max(1, sequencePreviewImage.sourceSize.width), sequencePreviewImage.paintedHeight / Math.max(1, sequencePreviewImage.sourceSize.height))
        property real previewImageX: sequencePreviewImage.x + (sequencePreviewImage.width - sequencePreviewImage.paintedWidth) / 2
        property real previewImageY: sequencePreviewImage.y + (sequencePreviewImage.height - sequencePreviewImage.paintedHeight) / 2
        property real previewImageWidth: sequencePreviewImage.paintedWidth
        property real previewImageHeight: sequencePreviewImage.paintedHeight

        function setPreviewRoiFromPixels(x, y, width, height) {
            const imageWidth = Math.max(1, previewImageWidth)
            const imageHeight = Math.max(1, previewImageHeight)
            const roiWidth = Math.max(1, Math.min(width, imageWidth))
            const roiHeight = Math.max(1, Math.min(height, imageHeight))
            const roiX = Math.max(0, Math.min(x - previewImageX, imageWidth - roiWidth))
            const roiY = Math.max(0, Math.min(y - previewImageY, imageHeight - roiHeight))
            controller.layerRoiX = roiX / imageWidth
            controller.layerRoiY = roiY / imageHeight
            controller.layerRoiWidth = roiWidth / imageWidth
            controller.layerRoiHeight = roiHeight / imageHeight
        }

        function resetPreviewRoi() {
            controller.layerRoiX = 0
            controller.layerRoiY = 0
            controller.layerRoiWidth = 1
            controller.layerRoiHeight = 1
        }

        title: isVideoInput() ? qsTr("Video Preview") : qsTr("Image Sequence Preview")
        visible: false
        width: 920
        height: 680
        minimumWidth: 640
        minimumHeight: 420
        color: Kirigami.Theme.alternateBackgroundColor

        Component.onCompleted: {
            x = Math.max(0, window.x + Math.round((window.width - width) / 2))
            y = Math.max(0, window.y + Math.round((window.height - height) / 2))
        }

        header: ToolBar {
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false

            RowLayout {
                anchors.fill: parent
                spacing: Kirigami.Units.smallSpacing

                ComboBox {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                    model: [ qsTr("Left"), qsTr("Right") ]
                    currentIndex: controller.sequencePreviewRight ? 1 : 0
                    enabled: controller.stereo
                    onActivated: controller.sequencePreviewRight = currentIndex === 1
                }
                SpinBox {
                    id: sequencePreviewFrameSpin
                    from: sequencePreviewSlider.from
                    to: sequencePreviewSlider.to
                    value: sequencePreviewSlider.value
                    editable: true
                    onValueModified: controller.sequencePreviewFrame = value
                }
                Label {
                    text: qsTr("/ %1").arg(sequencePreviewSlider.to)
                    opacity: 0.7
                }
                Slider {
                    id: sequencePreviewSlider
                    Layout.fillWidth: true
                    from: controller.sequencePreviewMinimum
                    to: Math.max(controller.sequencePreviewMinimum, controller.sequencePreviewMaximum)
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: controller.sequencePreviewFrame
                    onPositionChanged: controller.sequencePreviewFrame = Math.round(value)
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Qt.rgba(0, 0, 0, 0)
                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.18)
                radius: Kirigami.Units.smallSpacing
                clip: true

                Image {
                    id: sequencePreviewImage
                    visible: !isVideoInput()
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    source: controller.sequencePreviewPath.length > 0 ? app.pathToUrl(controller.sequencePreviewPath) : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }

                MpvPreviewItem {
                    id: sequencePreviewVideo
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    visible: isVideoInput() && controller.sequencePreviewPath.length > 0
                    source: visible ? controller.sequencePreviewPath : ""
                    frame: controller.sequencePreviewFrame
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width * 0.72
                    visible: isVideoInput() && controller.sequencePreviewPath.length === 0
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "video-x-generic"
                        implicitWidth: Kirigami.Units.iconSizes.huge
                        implicitHeight: Kirigami.Units.iconSizes.huge
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        Layout.fillWidth: true
                        text: controller.sequencePreviewStatus
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        opacity: 0.76
                    }
                    Label {
                        Layout.fillWidth: true
                        text: controller.sequencePreviewPath
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        opacity: 0.54
                    }
                }

                Rectangle {
                    id: sequencePreviewRoi

                    property real handleSize: Kirigami.Units.gridUnit * 0.8
                    property real dragStartX: 0
                    property real dragStartY: 0
                    property real dragStartWidth: 0
                    property real dragStartHeight: 0
                    property real dragStartMouseX: 0
                    property real dragStartMouseY: 0

                    visible: !isVideoInput()
                             && controller.layerRoiEnabled
                             && sequencePreviewImage.status === Image.Ready
                             && sequencePreviewWindow.previewImageWidth > 1
                             && sequencePreviewWindow.previewImageHeight > 1
                    x: sequencePreviewWindow.previewImageX + controller.layerRoiX * sequencePreviewWindow.previewImageWidth
                    y: sequencePreviewWindow.previewImageY + controller.layerRoiY * sequencePreviewWindow.previewImageHeight
                    width: controller.layerRoiWidth * sequencePreviewWindow.previewImageWidth
                    height: controller.layerRoiHeight * sequencePreviewWindow.previewImageHeight
                    color: "#354682B4"
                    border.color: "steelblue"
                    border.width: 2

                    function beginResize(mouseArea, mouse) {
                        dragStartX = x
                        dragStartY = y
                        dragStartWidth = width
                        dragStartHeight = height
                        dragStartMouseX = mouseArea.mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).x
                        dragStartMouseY = mouseArea.mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).y
                    }

                    MouseArea {
                        anchors.fill: parent
                        drag.axis: Drag.XAndYAxis
                        drag.target: parent
                        drag.minimumX: sequencePreviewWindow.previewImageX
                        drag.minimumY: sequencePreviewWindow.previewImageY
                        drag.maximumX: sequencePreviewWindow.previewImageX + sequencePreviewWindow.previewImageWidth - sequencePreviewRoi.width
                        drag.maximumY: sequencePreviewWindow.previewImageY + sequencePreviewWindow.previewImageHeight - sequencePreviewRoi.height
                        onPositionChanged: {
                            if (drag.active) {
                                sequencePreviewWindow.setPreviewRoiFromPixels(sequencePreviewRoi.x, sequencePreviewRoi.y, sequencePreviewRoi.width, sequencePreviewRoi.height)
                            }
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: sequencePreviewRoi.handleSize
                        height: sequencePreviewRoi.handleSize
                        radius: width / 2
                        color: "green"
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            onPressed: mouse => sequencePreviewRoi.beginResize(this, mouse)
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const current = mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).x
                                const dx = current - sequencePreviewRoi.dragStartMouseX
                                const newX = Math.max(sequencePreviewWindow.previewImageX, Math.min(sequencePreviewRoi.dragStartX + dx, sequencePreviewRoi.dragStartX + sequencePreviewRoi.dragStartWidth - 1))
                                sequencePreviewWindow.setPreviewRoiFromPixels(newX, sequencePreviewRoi.dragStartY, sequencePreviewRoi.dragStartWidth + sequencePreviewRoi.dragStartX - newX, sequencePreviewRoi.dragStartHeight)
                            }
                        }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: sequencePreviewRoi.handleSize
                        height: sequencePreviewRoi.handleSize
                        radius: width / 2
                        color: "red"
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            onPressed: mouse => sequencePreviewRoi.beginResize(this, mouse)
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const current = mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).x
                                const dx = current - sequencePreviewRoi.dragStartMouseX
                                const maxWidth = sequencePreviewWindow.previewImageX + sequencePreviewWindow.previewImageWidth - sequencePreviewRoi.dragStartX
                                sequencePreviewWindow.setPreviewRoiFromPixels(sequencePreviewRoi.dragStartX, sequencePreviewRoi.dragStartY, Math.max(1, Math.min(sequencePreviewRoi.dragStartWidth + dx, maxWidth)), sequencePreviewRoi.dragStartHeight)
                            }
                        }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.top
                        width: sequencePreviewRoi.handleSize
                        height: sequencePreviewRoi.handleSize
                        radius: width / 2
                        color: "yellow"
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onPressed: mouse => sequencePreviewRoi.beginResize(this, mouse)
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const current = mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).y
                                const dy = current - sequencePreviewRoi.dragStartMouseY
                                const newY = Math.max(sequencePreviewWindow.previewImageY, Math.min(sequencePreviewRoi.dragStartY + dy, sequencePreviewRoi.dragStartY + sequencePreviewRoi.dragStartHeight - 1))
                                sequencePreviewWindow.setPreviewRoiFromPixels(sequencePreviewRoi.dragStartX, newY, sequencePreviewRoi.dragStartWidth, sequencePreviewRoi.dragStartHeight + sequencePreviewRoi.dragStartY - newY)
                            }
                        }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.bottom
                        width: sequencePreviewRoi.handleSize
                        height: sequencePreviewRoi.handleSize
                        radius: width / 2
                        color: "blue"
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onPressed: mouse => sequencePreviewRoi.beginResize(this, mouse)
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const current = mapToItem(sequencePreviewWindow.contentItem, mouse.x, mouse.y).y
                                const dy = current - sequencePreviewRoi.dragStartMouseY
                                const maxHeight = sequencePreviewWindow.previewImageY + sequencePreviewWindow.previewImageHeight - sequencePreviewRoi.dragStartY
                                sequencePreviewWindow.setPreviewRoiFromPixels(sequencePreviewRoi.dragStartX, sequencePreviewRoi.dragStartY, sequencePreviewRoi.dragStartWidth, Math.max(1, Math.min(sequencePreviewRoi.dragStartHeight + dy, maxHeight)))
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: !isVideoInput() && (sequencePreviewImage.status === Image.Null || sequencePreviewImage.status === Image.Error)
                    text: controller.sequencePreviewStatus
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    width: parent.width * 0.72
                    opacity: 0.72
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: {
                        if (wheel.angleDelta.y > 0) {
                            controller.sequencePreviewPrevious()
                        }
                        else if (wheel.angleDelta.y < 0) {
                            controller.sequencePreviewNext()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                enabled: !isVideoInput() && sequencePreviewImage.status === Image.Ready
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    checked: controller.layerRoiEnabled
                    text: qsTr("ROI")
                    onToggled: controller.layerRoiEnabled = checked
                }
                FormLabel { text: qsTr("X"); enabled: controller.layerRoiEnabled }
                SpinBox { enabled: controller.layerRoiEnabled; from: 0; to: Math.max(0, sequencePreviewImage.sourceSize.width); value: Math.round(controller.layerRoiX * sequencePreviewImage.sourceSize.width); onValueModified: controller.layerRoiX = value / Math.max(1, sequencePreviewImage.sourceSize.width) }
                FormLabel { text: qsTr("Y"); enabled: controller.layerRoiEnabled }
                SpinBox { enabled: controller.layerRoiEnabled; from: 0; to: Math.max(0, sequencePreviewImage.sourceSize.height); value: Math.round(controller.layerRoiY * sequencePreviewImage.sourceSize.height); onValueModified: controller.layerRoiY = value / Math.max(1, sequencePreviewImage.sourceSize.height) }
                FormLabel { text: qsTr("W"); enabled: controller.layerRoiEnabled }
                SpinBox { enabled: controller.layerRoiEnabled; from: 1; to: Math.max(1, sequencePreviewImage.sourceSize.width); value: Math.round(controller.layerRoiWidth * sequencePreviewImage.sourceSize.width); onValueModified: controller.layerRoiWidth = value / Math.max(1, sequencePreviewImage.sourceSize.width) }
                FormLabel { text: qsTr("H"); enabled: controller.layerRoiEnabled }
                SpinBox { enabled: controller.layerRoiEnabled; from: 1; to: Math.max(1, sequencePreviewImage.sourceSize.height); value: Math.round(controller.layerRoiHeight * sequencePreviewImage.sourceSize.height); onValueModified: controller.layerRoiHeight = value / Math.max(1, sequencePreviewImage.sourceSize.height) }
                Button { text: qsTr("Reset"); enabled: controller.layerRoiEnabled; onClicked: sequencePreviewWindow.resetPreviewRoi() }
                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.fillWidth: true
                text: controller.sequencePreviewStatus
                elide: Text.ElideMiddle
                opacity: 0.76
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing
            anchors.margins: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                icon.source: "dialog-information"
                visible: true
         text: qsTr("C-Slice starts in master UI mode by default. Press Start to launch this executable again in node mode with SGCT and slice-compatible command-line arguments.")
            }

            RowLayout {
                id: inputOutputProgressRow

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.largeSpacing

                SectionPane {
                    title: isVideoInput() ? qsTr("Video input") : qsTr("Input sequence")
                    iconName: isVideoInput() ? "video-x-generic" : "insert-image"
                    trailingText: controller.sequenceIndexing ? controller.sequenceStatus : ""
                    Layout.fillHeight: true
                    Layout.preferredWidth: 480

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        rowSpacing: Kirigami.Units.smallSpacing
                        columnSpacing: Kirigami.Units.smallSpacing

                        FormLabel { text: qsTr("Input type") }
                        ComboBox {
                            Layout.fillWidth: true
                            model: [ qsTr("Image sequence"), qsTr("Video") ]
                            currentIndex: controller.inputType === "Video" ? 1 : 0
                            onActivated: controller.inputType = currentIndex === 1 ? "Video" : "Image sequence"
                        }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("Left/input") }
                        TextField { text: controller.leftInput; enabled: !controller.sequenceIndexing; Layout.fillWidth: true; onEditingFinished: controller.leftInput = text }
                        BrowseButton { dialog: leftDialog; enabled: !controller.sequenceIndexing }

                        Item { height: 1; width: 1 }
                        CheckBox { checked: controller.stereo; text: qsTr("Use right-eye input"); onToggled: controller.stereo = checked }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("Right/input"); enabled: controller.stereo }
                        TextField { text: controller.rightInput; enabled: controller.stereo && !controller.sequenceIndexing; Layout.fillWidth: true; onEditingFinished: controller.rightInput = text }
                        BrowseButton { dialog: rightDialog; enabled: controller.stereo && !controller.sequenceIndexing }

                        FormLabel { text: qsTr("Index") }
                        RowLayout {
                            Button {
                                text: isVideoInput() ? qsTr("Read metadata") : qsTr("Re-index")
                                icon.name: "view-refresh"
                                icon.color: Kirigami.Theme.textColor
                                enabled: !controller.sequenceIndexing && controller.leftInput.length > 0
                                onClicked: controller.forceRefreshImageSequenceStatus()
                            }
                            FormComment { text: isVideoInput() ? qsTr("duration, FPS, size") : (controller.stereo ? qsTr("left and right") : qsTr("left")) }
                        }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("Frame rate") }
                        RowLayout {
                            ComboBox {
                                id: inputFrameRatePresetCombo
                                model: [ "30", "60", "29.97", "59.94", qsTr("Custom") ]
                                currentIndex: inputFrameRatePresetIndex(controller.inputFrameRateNum, controller.inputFrameRateDen)
                                onActivated: setInputFrameRatePreset(currentIndex)
                            }
                            SpinBox {
                                from: 1
                                to: 100000
                                value: inputFrameRateCustomNum
                                enabled: inputFrameRatePresetCombo.currentIndex === 4
                                onValueModified: {
                                    inputFrameRateCustomNum = value
                                    controller.inputFrameRateNum = inputFrameRateCustomNum
                                    controller.inputFrameRateDen = inputFrameRateCustomDen
                                }
                            }
                            Label { text: "/"; enabled: inputFrameRatePresetCombo.currentIndex === 4 }
                            SpinBox {
                                from: 1
                                to: 100000
                                value: inputFrameRateCustomDen
                                enabled: inputFrameRatePresetCombo.currentIndex === 4
                                onValueModified: {
                                    inputFrameRateCustomDen = value
                                    controller.inputFrameRateNum = inputFrameRateCustomNum
                                    controller.inputFrameRateDen = inputFrameRateCustomDen
                                }
                            }
                        }
                        FormComment { text: qsTr("Exact: %1").arg((controller.inputFrameRateNum / controller.inputFrameRateDen).toLocaleString(Qt.locale(), 'f', 3)) }

                        FormLabel { text: qsTr("Start") }
                        RowLayout {
                            SpinBox { from: 0; to: 999999; value: controller.startIndex; onValueModified: controller.startIndex = value }
                            ToolButton {
                                icon.name: "edit-undo"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.hasIndexedRange
                                onClicked: controller.resetStartIndexToIndexedRange()
                                ToolTip.text: qsTr("Reset to last indexed start (%1)").arg(controller.indexedStartIndex)
                                ToolTip.visible: hovered
                            }
                        }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("Stop") }
                        RowLayout {
                            SpinBox { from: 0; to: 999999; value: controller.stopIndex; onValueModified: controller.stopIndex = value }
                            ToolButton {
                                icon.name: "edit-undo"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.hasIndexedRange
                                onClicked: controller.resetStopIndexToIndexedRange()
                                ToolTip.text: qsTr("Reset to last indexed stop (%1)").arg(controller.indexedStopIndex)
                                ToolTip.visible: hovered
                            }
                        }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("Step") }
                        SpinBox { from: 1; to: 10000; value: controller.steps; onValueModified: controller.steps = value }
                        Item { height: 1; width: 1 }

                        Item { height: 1; width: 1 }
                        CheckBox { checked: controller.upsideDown; text: qsTr("Upside down"); onToggled: controller.upsideDown = checked }
                        Item { height: 1; width: 1 }
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: controller.sequenceStatus.indexOf("gaps detected") >= 0 || controller.sequenceStatus.indexOf("right range") >= 0 || controller.sequenceStatus.indexOf("not a matching") >= 0
                            || controller.sequenceStatus.indexOf("metadata differs") >= 0
                            ? Kirigami.MessageType.Warning
                            : Kirigami.MessageType.Positive
                        icon.source: type === Kirigami.MessageType.Warning ? "dialog-warning" : "dialog-ok"
                        visible: controller.sequenceStatus.length > 0 && !controller.sequenceIndexing
                        text: controller.sequenceStatus
                        showCloseButton: false
                    }
                }

                SectionPane {
                    title: qsTr("Output")
                    iconName: "document-save-as"
                    Layout.fillHeight: true
                    Layout.preferredWidth: 350

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        rowSpacing: Kirigami.Units.smallSpacing
                        columnSpacing: Kirigami.Units.smallSpacing

                        FormLabel { text: qsTr("Directory") }
                        TextField { id: outputDirectoryField; text: controller.outputDirectory; Layout.fillWidth: true; onTextEdited: controller.outputDirectory = text }
                        BrowseButton { dialog: outputDialog; icon.name: "folder-open" }

                        FormLabel { text: qsTr("Base name") }
                        TextField { text: controller.outputName; Layout.fillWidth: true; onTextEdited: controller.outputName = text }
                        Item { height: 1; width: 1 }

                        FormLabel { text: qsTr("SGCT config") }
                        TextField { text: controller.configuration; Layout.fillWidth: true; onEditingFinished: controller.configuration = text }
                        BrowseButton { dialog: configDialog }

                        FormLabel { text: qsTr("Config options") }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: 2
                            spacing: Kirigami.Units.smallSpacing

                            CheckBox { checked: controller.warping; text: qsTr("Warping"); onToggled: controller.warping = checked }
                            CheckBox { checked: controller.blendMask; text: qsTr("Blend mask"); onToggled: controller.blendMask = checked }
                        }

                        FormLabel { text: qsTr("Outputs") }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: 2
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    text: qsTr("%1 of %2 selected").arg(controller.selectedOutputCount).arg(controller.outputCount)
                                    opacity: 0.65
                                    Layout.fillWidth: true
                                }
                                Button {
                                    text: qsTr("All")
                                    onClicked: controller.setAllOutputsEnabled(true)
                                }
                                Button {
                                    text: qsTr("None")
                                    onClicked: controller.setAllOutputsEnabled(false)
                                }
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(Kirigami.Units.gridUnit * 8, Math.max(Kirigami.Units.gridUnit * 4, controller.outputCount * Kirigami.Units.gridUnit * 1.45))
                                contentWidth: availableWidth
                                clip: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 0

                                    Repeater {
                                        model: controller.outputs

                                        CheckBox {
                                            Layout.fillWidth: true
                                            checked: modelData.enabled
                                            text: modelData.name
                                            onToggled: controller.setOutputEnabled(modelData.index, checked)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    id: progressQueueRow

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Kirigami.Units.largeSpacing

                    SectionPane {
                        title: qsTr("Progress")
                        iconName: "view-statistics"
                        trailingText: controller.sliceElapsedTime.length > 0 ? qsTr("Elapsed %1 \n Remaining %2").arg(controller.sliceElapsedTime).arg(controller.sliceRemainingTime) : ""
                        enabled: controller.running || controller.sliceTotalFrames > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                Layout.fillWidth: true
                                text: controller.sliceProgressStatus.length > 0
                                    ? controller.sliceProgressStatus
                                    : qsTr("Images are buffered and processed on the virtual dome before capture/encoding.")
                                wrapMode: Text.Wrap
                                opacity: 0.78
                            }

                            ProgressBar {
                                id: renderProgressBar
                                Layout.fillWidth: true
                                Layout.preferredHeight: Kirigami.Units.gridUnit * 0.8
                                implicitHeight: Kirigami.Units.gridUnit * 0.8
                                padding: 1
                                from: 0
                                to: 100
                                value: controller.sliceRenderedProgress
                                background: Rectangle {
                                    implicitHeight: renderProgressBar.implicitHeight
                                    radius: height / 2
                                    color: Kirigami.Theme.alternateBackgroundColor
                                    opacity: 0.9
                                }
                                contentItem: Item {
                                    implicitHeight: renderProgressBar.implicitHeight
                                    clip: true

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: parent.width * Math.max(0, Math.min(1, renderProgressBar.visualPosition))
                                        radius: height / 2
                                        color: "lightgreen"
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                rowSpacing: Kirigami.Units.smallSpacing
                                columnSpacing: Kirigami.Units.smallSpacing

                                FormLabel { text: controller.sliceProgressAction }
                                Label {
                                    text: qsTr("%1/%2 (%3%)")
                                        .arg(controller.sliceRenderedFrames)
                                        .arg(controller.sliceTotalFrames)
                                        .arg(controller.sliceRenderedProgress)
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                }

                                FormLabel { text: controller.sliceProgressAction === qsTr("Verified") ? qsTr("Checked") : qsTr("Loaded") }
                                Label {
                                    text: qsTr("%1/%2 (%3%)")
                                        .arg(controller.sliceLoadedFrames)
                                        .arg(controller.sliceTotalFrames)
                                        .arg(controller.sliceLoadedProgress)
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                }
                            }

                            Kirigami.InlineMessage {
                                Layout.fillWidth: true
                                type: Kirigami.MessageType.Error
                                icon.source: "dialog-error"
                                visible: controller.sliceCriticalErrors.length > 0
                                text: controller.sliceFailedFiles.length > 0
                                    ? qsTr("%1 image file(s) failed. Open the failed files below.").arg(controller.sliceFailedFiles.length)
                                    : qsTr("A critical error occurred. See the Log for details.")
                                showCloseButton: false
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(Kirigami.Units.gridUnit * 5, Math.max(Kirigami.Units.gridUnit * 2, controller.sliceFailedFiles.length * Kirigami.Units.gridUnit * 1.4))
                                visible: controller.sliceFailedFiles.length > 0
                                contentWidth: availableWidth
                                clip: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 0

                                    Repeater {
                                        model: controller.sliceFailedFiles

                                        Label {
                                            Layout.fillWidth: true
                                            textFormat: Text.RichText
                                            text: "<a href=\"#\">" + modelData.name + "</a>"
                                            elide: Text.ElideMiddle
                                            ToolTip.text: modelData.path
                                            ToolTip.visible: hovered
                                            onLinkActivated: controller.openFailedFile(modelData.path)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    SectionPane {
                        title: qsTr("Queue")
                        iconName: "kt-queue-manager"
                        Layout.fillWidth: true
                        trailingText: controller.editingQueuedJob
                                    ? qsTr("Editing queued job: %1").arg(controller.editedQueuedJobName)
                                    : (controller.running && controller.runningJobIndex >= 0
                                    ? qsTr("Processing job %1 of %2").arg(controller.runningJobIndex + 1).arg(controller.internalQueueSize)
                                    : (controller.internalQueueSize === 0 ? qsTr("No jobs in queue") : qsTr("%1 job(s) waiting").arg(controller.internalQueueSize)))

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            // C-Job connection controls
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Button {
                                    text: qsTr("Queue Job")
                                    icon.name: "list-add"
                                    enabled: !controller.running && !controller.editingQueuedJob
                                    onClicked: controller.queueJob()
                                }
                                Button {
                                    text: qsTr("Save")
                                    icon.name: "document-save"
                                    enabled: !controller.running && !controller.internalQueueRunning && !controller.cjobEnabled && !controller.editingQueuedJob
                                    onClicked: saveQueueDialog.open()
                                }
                                Button {
                                    text: qsTr("Load")
                                    icon.name: "document-open"
                                    enabled: !controller.running && !controller.internalQueueRunning && !controller.cjobEnabled && !controller.editingQueuedJob
                                    onClicked: loadQueueDialog.open()
                                }
                                CheckBox {
                                    checked: controller.cjobEnabled
                                    text: qsTr("Submit to C-Job")
                                    enabled: !controller.editingQueuedJob
                                    onToggled: controller.cjobEnabled = checked
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // Queue list with drag-and-drop support
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.maximumHeight: 100
                                Layout.minimumHeight: 20
                                clip: true

                                ListView {
                                    id: queueListView
                                    width: parent ? parent.width : 300
                                    model: internalQueueModel
                                    
                                    delegate: internalQueueItemDelegate
                                    spacing: Kirigami.Units.smallSpacing
                                    
                                    // Catch mouse release outside items to clean up stuck drags
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                        onContainsMouseChanged: {
                                            if (!containsMouse) {
                                                // Mouse left the ListView area - force cleanup of any active drag
                                                for (var i = 0; i < queueListView.count; i++) {
                                                    var item = queueListView.itemAtIndex(i);
                                                    if (item && item.dragging) {
                                                        item.dragging = false;
                                                        item.z = 0;
                                                        item.opacity = 1.0;
                                                        if (item.insertionIndicator) {
                                                            item.insertionIndicator.destroy();
                                                            item.insertionIndicator = null;
                                                        }
                                                        item._dragActive = false;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    
                                    // Catch Escape key to cancel active drags
                                    Keys.onPressed: (event) => {
                                        if (event.key === Qt.Key_Escape) {
                                            for (var i = 0; i < queueListView.count; i++) {
                                                var item = queueListView.itemAtIndex(i);
                                                if (item && item.dragging) {
                                                    event.accepted = true;
                                                    item.dragging = false;
                                                    item.z = 0;
                                                    item.opacity = 1.0;
                                                    if (item.insertionIndicator) {
                                                        item.insertionIndicator.destroy();
                                                        item.insertionIndicator = null;
                                                    }
                                                    item._dragActive = false;
                                                    item.y = item.originalY;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Component {
                                id: internalQueueItemDelegate
                                
                                ItemDelegate {
                                    id: itemRoot
                                    width: parent ? parent.width : 300
                                    height: 30
                                    
                                    property bool dragging: false
                                    property real originalY: 0
                                    property bool _dragActive: false
                                    property real _pressPosX: 0
                                    property real _pressPosY: 0
                                    // transient insertion indicator instance (created on drag start)
                                    property var insertionIndicator: null
                                                                
                                    contentItem: RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: Kirigami.Units.smallSpacing
                                        spacing: Kirigami.Units.smallSpacing
                                                                    
                                         Label {
                                             text: "%1.".arg(index + 1)
                                             Layout.preferredWidth: 30
                                             opacity: 0.7
                                         }
                                         Label {
                                             text: model.jobName || ""
                                             Layout.fillWidth: true
                                             elide: Text.ElideMiddle
                                         }
                                                                    
                                            Label {
                                                text: (controller.running && controller.internalQueueSize > 0 && index === controller.runningJobIndex ? "Running" : "Pending")
                                                color: (controller.running && controller.internalQueueSize > 0 && index === controller.runningJobIndex ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor)
                                                opacity: 0.85
                                            }
                                                                    
                                        ToolButton {
                                            id: removeButton
                                            Layout.preferredWidth: 24
                                            Layout.preferredHeight: 24
                                            icon.name: "list-remove"
                                            visible: !controller.running && !controller.cjobEnabled && !controller.editingQueuedJob
                                            opacity: hovered ? 1.0 : 0.5
                                        }
                                    }
                                                                
                                    background: Rectangle {
                                        anchors.fill: parent
                                        color: {
                                            if (itemRoot.dragging) return Qt.alpha(Kirigami.Theme.highlightColor, 0.4);
                                            if (index === internalQueueModel.selectedIndex) return Qt.alpha(0x00ff00, 0.25);
                                            if (hovered && !itemRoot.dragging) return Qt.alpha(Kirigami.Theme.hoverColor, 0.4);
                                            return Kirigami.Theme.backgroundColor;
                                        }
                                    }
                                                                
                                    // Delete button area - placed at top level to intercept clicks before drag
                                    Rectangle {
                                        id: deleteArea
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 32
                                        color: "transparent"
                                                                    
                                        // Only active when visible (not running, not C-Job)
                                        enabled: !controller.running && !controller.cjobEnabled && !controller.editingQueuedJob
                                                                    
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            hoverEnabled: true
                                            onClicked: {
                                                mouse.accepted = true
                                                controller.removeJobFromInternalQueue(index)
                                            }
                                        }
                                    }
                                                                
                                    // Template for transient insertion indicator
                                    Component {
                                        id: insertionLineComp
                                        Rectangle {
                                            color: Kirigami.Theme.highlightColor
                                            height: 4
                                            width: parent ? parent.width : queueListView.width
                                            radius: 2
                                            opacity: 0.95
                                            z: 10000
                                        }
                                    }
                                                                
                                    // Drag area covering the rest of the item (excluding delete button)
                                        MouseArea {
                                            anchors.right: deleteArea.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton
                                             enabled: !controller.editingQueuedJob
                                            drag.target: itemRoot
                                            drag.axis: Drag.YAxis
                                                                     
                                            onPressed: (mouse) => {
                                                itemRoot._dragActive = false;
                                                itemRoot._pressPosX = mouse.x;
                                                itemRoot._pressPosY = mouse.y;
                                                itemRoot.originalY = itemRoot.y;
                                                queueListView.currentIndex = index;
                                                internalQueueModel.selectedIndex = index;
                                            }
                                                                     
                                            onPositionChanged: (mouse) => {
                                                // Only start drag when mouse moves beyond system drag distance threshold (3 pixels)
                                                if (!itemRoot._dragActive) {
                                                    var dx = Math.abs(mouse.x - itemRoot._pressPosX);
                                                    var dy = Math.abs(mouse.y - itemRoot._pressPosY);
                                                    if (dx <= 3 && dy <= 3) {
                                                        return;
                                                    }
                                                    // Drag has actually started now
                                                    itemRoot._dragActive = true;
                                                    itemRoot.dragging = true;
                                                    itemRoot.z = 9999;
                                                    itemRoot.opacity = 0.85;
                                                    drag.minimumY = 0;
                                                    drag.maximumY = Math.max(0, queueListView.contentHeight);
                                                                             
                                                    // Create insertion indicator only when drag actually starts
                                                    if (queueListView && queueListView.contentItem && !itemRoot.insertionIndicator) {
                                                        itemRoot.insertionIndicator = insertionLineComp.createObject(queueListView.contentItem, { x: 0, y: itemRoot.y + itemRoot.height/2 - 2, width: queueListView.width });
                                                    }
                                                }
                                                                         
                                                if (!itemRoot._dragActive) return;
                                                                         
                                                var edgeThreshold = 20;
                                                if (itemRoot.y < queueListView.contentY + edgeThreshold && queueListView.contentY > 0) {
                                                    queueListView.contentY = Math.max(0, queueListView.contentY - 8);
                                                }
                                                var bottomEdge = queueListView.contentHeight - edgeThreshold - itemRoot.height;
                                                if (itemRoot.y > bottomEdge && (queueListView.contentY < queueListView.contentHeight - queueListView.height)) {
                                                    queueListView.contentY = Math.min(queueListView.contentHeight - queueListView.height, queueListView.contentY + 8);
                                                }

                                                // update insertion indicator position
                                            if (itemRoot.insertionIndicator) {
                                                var centerY = itemRoot.y + itemRoot.height / 2;
                                                var destIndex = Math.floor(centerY / itemRoot.height);
                                                if (destIndex < 0) destIndex = 0;
                                                if (destIndex > queueListView.count - 1) destIndex = queueListView.count - 1;
                                                                            
                                                var halfIndicator = itemRoot.insertionIndicator.height / 2;
                                                var indicatorY;
                                                                            
                                                // When dragging down, show indicator at BOTTOM of target item
                                                // When dragging up, show it at TOP of target item
                                                if (itemRoot.y > itemRoot.originalY) {
                                                    indicatorY = (destIndex + 1) * itemRoot.height - halfIndicator;
                                                } else {
                                                    indicatorY = destIndex * itemRoot.height - halfIndicator;
                                                }
                                                                            
                                                // clamp indicator to content area
                                                if (indicatorY < 0) indicatorY = 0;
                                                var maxIndicatorY = queueListView.contentHeight - itemRoot.insertionIndicator.height;
                                                if (indicatorY > maxIndicatorY) indicatorY = maxIndicatorY;
                                                                            
                                                itemRoot.insertionIndicator.y = indicatorY;
                                                itemRoot.insertionIndicator.width = queueListView.width;
                                            }
                                        }
                                                                    
                                            onCanceled: {
                                                // cleanup transient indicator
                                                if (itemRoot.insertionIndicator) {
                                                    itemRoot.insertionIndicator.destroy();
                                                    itemRoot.insertionIndicator = null;
                                                }
                                                itemRoot._dragActive = false;
                                                itemRoot.dragging = false;
                                                itemRoot.z = 0;
                                                itemRoot.opacity = 1.0;
                                                // restore original position
                                                itemRoot.y = itemRoot.originalY;
                                            }
                                                                    
                                            onReleased: {
                                                if (!itemRoot._dragActive) return;
                                                                         
                                                itemRoot._dragActive = false;
                                                itemRoot.dragging = false;
                                                itemRoot.z = 0;
                                                itemRoot.opacity = 1.0;
                                                                        
                                            // Compute destination index based on delegate center and drag direction
                                            var centerY = itemRoot.y + itemRoot.height / 2;
                                            var destIndex = Math.floor(centerY / itemRoot.height);
                                            if (destIndex < 0) destIndex = 0;
                                            if (destIndex > queueListView.count - 1) destIndex = queueListView.count - 1;
                                                                        
                                            // If dragged down, insert AFTER destIndex; if up, insert BEFORE destIndex
                                            var finalDest = (itemRoot.y > itemRoot.originalY) ? destIndex : destIndex;
                                                                        
                                            // clamp finalDest to valid range
                                            if (finalDest < 0) finalDest = 0;
                                            if (finalDest > queueListView.count - 1) finalDest = queueListView.count - 1;
                                                                        
                                            if (index !== finalDest) {
                                                internalQueueModel.moveRow(index, finalDest);
                                            }
                                                                        
                                            // remove transient indicator after move
                                            if (itemRoot.insertionIndicator) {
                                                itemRoot.insertionIndicator.destroy();
                                                itemRoot.insertionIndicator = null;
                                            }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Button {
                                    text: controller.internalQueueRunning ? qsTr("Pause Queue") : qsTr("Run Queue")
                                    icon.name: controller.internalQueueRunning ? "media-playlist-shuffle" : "media-playback-start"
                                    flat: false
                                    enabled: !controller.cjobEnabled && !controller.editingQueuedJob && (controller.internalQueueSize > 0 || true)
                                    onClicked: {
                                        if (controller.internalQueueRunning) {
                                            controller.internalQueueRunning = false
                                        } else {
                                            controller.internalQueueRunning = true
                                        }
                                    }
                                }

                                Button {
                                    text: qsTr("Start Selected Job")
                                    icon.name: "media-playback-start"
                                    onClicked: controller.launchJobAtQueueIndex(internalQueueModel.selectedIndex)
                                    enabled: internalQueueModel.selectedIndex >= 0 && 
                                            internalQueueModel.selectedIndex < controller.internalQueueSize &&
                                            !controller.running && 
                                            !controller.cjobEnabled &&
                                            !controller.editingQueuedJob
                                }

                                Button {
                                    text: qsTr("Edit Selected Job")
                                    icon.name: "document-edit"
                                    enabled: internalQueueModel.selectedIndex >= 0
                                        && internalQueueModel.selectedIndex < controller.internalQueueSize
                                        && !controller.running && !controller.internalQueueRunning
                                        && !controller.cjobEnabled && !controller.editingQueuedJob
                                    onClicked: controller.beginQueuedJobEdit(internalQueueModel.selectedIndex)
                                }

                                Button {
                                    visible: controller.editingQueuedJob
                                    text: qsTr("Save Changes")
                                    icon.name: "document-save"
                                    onClicked: controller.saveQueuedJobEdit()
                                }

                                Button {
                                    visible: controller.editingQueuedJob
                                    text: qsTr("Cancel Edit")
                                    icon.name: "dialog-cancel"
                                    onClicked: controller.cancelQueuedJobEdit()
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing
                anchors.margins: Kirigami.Units.largeSpacing

                RowLayout {
                    id: parameterView

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.largeSpacing

                    SectionPane {
                        title: qsTr("Input mapping")
                        iconName: "map-globe"
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: Kirigami.Units.smallSpacing
                            columnSpacing: Kirigami.Units.smallSpacing

                            FormLabel { text: qsTr("Input images are") }
                            ComboBox {
                                model: [ "2D (mono)", "3D (side-by-side)", "3D (top-bottom)", "3D (top-bottom+flip)" ]
                                currentIndex: stereoModeIndex(controller.layerStereoMode)
                                onActivated: controller.layerStereoMode = currentText
                            }

                            FormLabel { text: qsTr("Map input onto") }
                            ComboBox { model: [ "Dome", "Sphere EQR", "Sphere EAC", "Plane" ]; currentIndex: model.indexOf(controller.mappingMode); onActivated: controller.mappingMode = currentText }

                            FormLabel { text: qsTr("Radius") }
                            TextField {
                                text: controller.surfaceRadius.toFixed(1)
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                placeholderText: qsTr("cm")
                                onEditingFinished: controller.surfaceRadius = Number(text)
                            }
                            FormLabel { text: qsTr("FOV") }
                            TextField {
                                text: controller.surfaceFov.toFixed(1)
                                enabled: controller.mappingMode === "Dome"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                placeholderText: qsTr("deg")
                                onEditingFinished: controller.surfaceFov = Number(text)
                            }

                            FormLabel { text: qsTr("Rotate"); visible: controller.mappingMode === "Dome" || controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC" }
                            RowLayout {
                                visible: controller.mappingMode === "Dome" || controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC"
                                FormLabel { text: qsTr("Pitch"); visible: controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC" }
                                SpinBox { visible: controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC"; from: -3600; to: 3600; value: Math.round(controller.layerPitch * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.layerPitch = value / 10; ToolTip.text: qsTr("Pitch"); ToolTip.visible: hovered }
                                FormLabel { text: qsTr("Yaw") }
                                SpinBox { from: -3600; to: 3600; value: Math.round(controller.layerYaw * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.layerYaw = value / 10; ToolTip.text: qsTr("Yaw"); ToolTip.visible: hovered }
                                FormLabel { text: qsTr("Roll"); visible: controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC" }
                                SpinBox { visible: controller.mappingMode === "Sphere EQR" || controller.mappingMode === "Sphere EAC"; from: -3600; to: 3600; value: Math.round(controller.layerRoll * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.layerRoll = value / 10; ToolTip.text: qsTr("Roll"); ToolTip.visible: hovered }
                                FormComment { text: qsTr("degrees") }
                            }

                            FormLabel { text: qsTr("Plane orient"); visible: controller.mappingMode === "Plane" }
                            RowLayout {
                                visible: controller.mappingMode === "Plane"
                                SpinBox { from: -3600; to: 3600; value: Math.round(controller.planeAzimuth * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.planeAzimuth = value / 10; ToolTip.text: qsTr("Azimuth"); ToolTip.visible: hovered }
                                SpinBox { from: -1800; to: 1800; value: Math.round(controller.planeElevation * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.planeElevation = value / 10; ToolTip.text: qsTr("Elevation"); ToolTip.visible: hovered }
                                SpinBox { from: -3600; to: 3600; value: Math.round(controller.planeRoll * 10); editable: true; textFromValue: decimalSpinText; valueFromText: decimalSpinValue; onValueModified: controller.planeRoll = value / 10; ToolTip.text: qsTr("Roll"); ToolTip.visible: hovered }
                                FormComment { text: qsTr("degrees") }
                            }

                            FormLabel { text: qsTr("Plane pos"); visible: controller.mappingMode === "Plane" }
                            RowLayout {
                                visible: controller.mappingMode === "Plane"
                                SpinBox { from: 1; to: 100000; value: Math.round(controller.planeDistance); editable: true; onValueModified: controller.planeDistance = value; ToolTip.text: qsTr("Distance cm"); ToolTip.visible: hovered }
                                SpinBox { from: -100000; to: 100000; value: Math.round(controller.planeHorizontal); editable: true; onValueModified: controller.planeHorizontal = value; ToolTip.text: qsTr("Horizontal cm"); ToolTip.visible: hovered }
                                SpinBox { from: -100000; to: 100000; value: Math.round(controller.planeVertical); editable: true; onValueModified: controller.planeVertical = value; ToolTip.text: qsTr("Vertical cm"); ToolTip.visible: hovered }
                                FormComment { text: qsTr("cm") }
                            }

                            FormLabel { text: qsTr("Plane size"); visible: controller.mappingMode === "Plane" }
                            RowLayout {
                                visible: controller.mappingMode === "Plane"
                                SpinBox { from: 0; to: 100000; value: Math.round(controller.planeWidth); editable: true; onValueModified: controller.planeWidth = value; ToolTip.text: qsTr("Width cm; 0 uses default"); ToolTip.visible: hovered }
                                SpinBox { from: 0; to: 100000; value: Math.round(controller.planeHeight); editable: true; onValueModified: controller.planeHeight = value; ToolTip.text: qsTr("Height cm; 0 uses default"); ToolTip.visible: hovered }
                                FormComment { text: qsTr("cm") }
                            }

                            FormLabel { text: qsTr("Plane aspect"); visible: controller.mappingMode === "Plane" }
                            ComboBox {
                                visible: controller.mappingMode === "Plane"
                                model: [ qsTr("Manual"), qsTr("Fit width"), qsTr("Fit height") ]
                                currentIndex: controller.planeAspectRatio
                                onActivated: controller.planeAspectRatio = currentIndex
                            }

                            FormLabel { text: qsTr("ROI") }
                            RowLayout {
                                Layout.fillWidth: true

                                CheckBox { Layout.fillWidth: true; checked: controller.layerRoiEnabled; text: qsTr("Region of interest"); onToggled: controller.layerRoiEnabled = checked }

                                ColumnLayout {
                                    RowLayout {
                                        FormLabel { text: qsTr("ROI Pos"); enabled: controller.layerRoiEnabled }
                                        SpinBox { enabled: controller.layerRoiEnabled; from: 0; to: 1000; value: Math.round(controller.layerRoiX * 1000); onValueModified: controller.layerRoiX = value / 1000 }
                                        SpinBox { enabled: controller.layerRoiEnabled; from: 0; to: 1000; value: Math.round(controller.layerRoiY * 1000); onValueModified: controller.layerRoiY = value / 1000 }
                                    }
                                    RowLayout {
                                        FormLabel { text: qsTr("ROI Size"); enabled: controller.layerRoiEnabled }
                                        SpinBox { enabled: controller.layerRoiEnabled; from: 1; to: 1000; value: Math.round(controller.layerRoiWidth * 1000); onValueModified: controller.layerRoiWidth = value / 1000 }
                                        SpinBox { enabled: controller.layerRoiEnabled; from: 1; to: 1000; value: Math.round(controller.layerRoiHeight * 1000); onValueModified: controller.layerRoiHeight = value / 1000 }
                                    }
                                }
                            }
                        }
                    }

                    SectionPane {
                        title: qsTr("Encoding")
                        iconName: "video-symbolic"
                        trailingText: qsTr("FFmpeg %1").arg(window.externalVersion(app.ffmpegVersion))
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: Kirigami.Units.smallSpacing
                            columnSpacing: Kirigami.Units.smallSpacing

                            FormLabel { text: qsTr("Codec") }
                            RowLayout {
                                ComboBox {
                                    id: codecCombo
                                    model: [ "MPEG-1", "MPEG-2", "MPEG-4", "H264", "H265", "H264 NVENC", "H265 NVENC", "VP8", "VP9", "Hap", "ProRes", "FFV1", "PNG", "JPEG", "TGA" ]
                                    currentIndex: model.indexOf(controller.codec)
                                    onCurrentIndexChanged: if (currentIndex < 0 && model.indexOf(controller.codec) >= 0) currentIndex = model.indexOf(controller.codec)
                                    onActivated: {
                                        controller.codec = currentText
                                        updateEncodingPreset()
                                    }
                                }
                                ComboBox {
                                    id: containerCombo
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                    enabled: controller.outputContainerSuffixes.length > 1
                                    model: controller.outputContainerSuffixes
                                    currentIndex: Math.max(0, model.indexOf(controller.outputContainerSuffix))
                                    onActivated: controller.outputContainerSuffix = currentText
                                }
                            }

                            FormLabel { text: qsTr("Frame rate"); enabled: isMovieCodec(controller.codec) && !controller.runWithoutEncoding }
                            RowLayout {
                                enabled: isMovieCodec(controller.codec) && !controller.runWithoutEncoding
                                ComboBox {
                                    id: frameRatePresetCombo
                                    model: [ "30", "60", "29.97", "59.94", qsTr("Custom") ]
                                    currentIndex: frameRatePresetIndex(controller.frameRateNum, controller.frameRateDen)
                                    onActivated: setFrameRatePreset(currentIndex)
                                }
                                SpinBox { from: 1; to: 100000; value: controller.frameRateNum; enabled: parent.enabled && frameRatePresetCombo.currentIndex === 4; onValueModified: controller.frameRateNum = value }
                                Label { text: "/"; enabled: parent.enabled && frameRatePresetCombo.currentIndex === 4 }
                                SpinBox { from: 1; to: 100000; value: controller.frameRateDen; enabled: parent.enabled && frameRatePresetCombo.currentIndex === 4; onValueModified: controller.frameRateDen = value }
                            }

                            FormLabel { text: qsTr("Preset"); enabled: hasPreset(controller.codec) && !controller.runWithoutEncoding }
                            ComboBox {
                                id: presetCombo
                                enabled: hasPreset(controller.codec) && !controller.runWithoutEncoding
                                model: presetModel(controller.codec)
                                currentIndex: Math.max(0, model.indexOf(controller.preset))
                                onModelChanged: updateEncodingPreset()
                                onCurrentIndexChanged: if (currentIndex < 0 && model.indexOf(controller.preset) >= 0) updateEncodingPreset()
                                onActivated: controller.preset = currentText
                            }

                            FormLabel { text: qsTr("Tune"); enabled: isTuneCodec(controller.codec) && !controller.runWithoutEncoding }
                            ComboBox {
                                id: tuneCombo
                                enabled: isTuneCodec(controller.codec) && !controller.runWithoutEncoding
                                model: tuneModel(controller.codec)
                                currentIndex: Math.max(0, model.indexOf(tuneValue(controller.codec)))
                                onCurrentIndexChanged: if (currentIndex < 0 && model.indexOf(tuneValue(controller.codec)) >= 0) currentIndex = model.indexOf(tuneValue(controller.codec))
                                onActivated: setTuneValue(controller.codec, currentText)
                            }

                            FormLabel { text: qsTr("Bit depth"); enabled: supportsEncodingBitDepth(controller.codec) && !controller.runWithoutEncoding }
                            ComboBox {
                                enabled: supportsEncodingBitDepth(controller.codec) && !controller.runWithoutEncoding
                                model: [ "8-bit", "10-bit" ]
                                currentIndex: controller.encodingBitDepth >= 10 ? 1 : 0
                                onActivated: controller.encodingBitDepth = currentIndex === 1 ? 10 : 8
                            }

                            FormLabel {
                                text: qsTr("Pixel rate")
                                visible: hasBitrate(controller.codec) && !controller.runWithoutEncoding
                                enabled: visible
                            }
                            SpinBox {
                                visible: hasBitrate(controller.codec) && !controller.runWithoutEncoding
                                enabled: visible
                                from: 1
                                to: 500
                                value: controller.pixrate
                                onValueModified: controller.pixrate = value
                            }
                            FormLabel {
                                text: qualityLabel(controller.codec)
                                visible: hasQuality(controller.codec) && !controller.runWithoutEncoding
                                enabled: visible
                            }
                            SpinBox {
                                visible: hasQuality(controller.codec) && !controller.runWithoutEncoding
                                enabled: visible
                                from: 0
                                to: 51
                                value: qualityValue(controller.codec)
                                onValueModified: setQualityValue(controller.codec, value)
                            }

                            FormLabel { text: qsTr("GOP Size"); enabled: isMovieCodec(controller.codec) && !controller.runWithoutEncoding }
                            RowLayout {
                                enabled: isMovieCodec(controller.codec) && !controller.runWithoutEncoding

                                SpinBox {
                                    id: gopSizeSpin
                                    enabled: !controller.useOnlyIframes
                                    from: 1 // Represents 0.1 seconds
                                    to: 100 // Represents 10.0 seconds
                                    value: controller.gopSizeSeconds * 10 // Store as integer internally to allow 0.1 steps
                                    stepSize: 1 // Represents 0.1 steps
                                    editable: true

                                    property int decimals: 1

                                    // Calculates the real floating-point value from the internal integer
                                    property real realValue: value / Math.pow(10, decimals)

                                    textFromValue: function(value, locale) {
                                        return Number(value / Math.pow(10, decimals)).toLocaleString(locale, 'f', gopSizeSpin.decimals)
                                    }

                                    valueFromText: function(text, locale) {
                                        return Number.fromLocaleString(locale, text) * Math.pow(10, decimals)
                                    }

                                    onValueModified: {
                                        controller.gopSizeSeconds = realValue
                                    }
                                    ToolTip.text: qsTr("GOP duration in seconds. High value decreases decoding speed but also lowers file size. Too low value might causes micro stuttering.")
                                    ToolTip.visible: hovered
                                }
                                FormComment { text: qsTr("seconds") }

                                CheckBox {
                                    text: qsTr("Use only I-frames")
                                    checked: controller.useOnlyIframes
                                    onToggled: controller.useOnlyIframes = checked
                                }
                            }

                            FormLabel { text: qsTr("Parameter JSON"); enabled: !controller.runWithoutEncoding }
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: !controller.runWithoutEncoding
                                TextField { text: controller.parameterFile; Layout.fillWidth: true; onEditingFinished: controller.parameterFile = text }
                                BrowseButton { dialog: parameterDialog; icon.name: "configure" }
                            }

                            Item { height: 1; width: 1 }
                            Item { height: 1; width: 1 }
                        }
                    }

                    SectionPane {
                        title: qsTr("Advanced")
                        iconName: "configure"
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: Kirigami.Units.smallSpacing
                            columnSpacing: Kirigami.Units.smallSpacing

                            FormLabel { text: qsTr("Encoder threads") }
                            RowLayout{
                                SpinBox { from: 1; to: 128; value: controller.maxEncoderThreads; onValueModified: controller.maxEncoderThreads = value }
                                FormComment { text: qsTr("per window") }
                            }

                            FormLabel { text: qsTr("Capture GPU Slots") }
                            RowLayout{
                                SpinBox { from: 1; to: 128; value: controller.captureGpuSlots; onValueModified: controller.captureGpuSlots = value }
                                FormComment { text: qsTr("per window") }
                            }

                            FormLabel { text: qsTr("Loading threads") }
                            RowLayout {
                                SpinBox {
                                    from: 1
                                    to: 64
                                    value: controller.imageBufferingThreadCount
                                    onValueModified: controller.imageBufferingThreadCount = value
                                }
                            }
                            FormLabel { 
                                visible: !isVideoInput()
                                text: qsTr("Image errors") 
                            }
                            ComboBox {
                                visible: !isVideoInput()
                                model: [ qsTr("Abort on image error"), qsTr("Pause on image error"), qsTr("Continue on image error") ]
                                currentIndex: controller.imageErrorBehavior === "Pause" ? 1 : (controller.imageErrorBehavior === "Continue" ? 2 : 0)
                                onActivated: controller.imageErrorBehavior = currentIndex === 1 ? "Pause" : (currentIndex === 2 ? "Continue" : "Abort")
                            }

                            FormLabel { 
                                visible: isVideoInput()
                                text: qsTr("Video decoding mode") 
                            }
                            ComboBox {
                                visible: isVideoInput()
                                model: [ qsTr("Software"), qsTr("Hardware"), qsTr("Hybrid") ]
                                currentIndex: {
                                    const modes = [ "Software", "Hardware", "Hybrid" ]
                                    return modes.indexOf(controller.videoDecodingMode) >= 0 
                                        ? modes.indexOf(controller.videoDecodingMode) : 0
                                }
                                onActivated: controller.videoDecodingMode = currentText
                                ToolTip.visible: hovered
                                ToolTip.text: isVideoInput() ? qsTr("Software uses software rendering. Hardware uses GPU decoding. Hybrid uses hardware for left eye and software for right eye.") : ""
                            }

                            FormLabel { text: qsTr("Run without encoding:") }
                            CheckBox { checked: controller.runWithoutEncoding; text: qsTr("Benchmark capture and loading only"); onToggled: controller.runWithoutEncoding = checked }

                            FormLabel { text: qsTr("Run without readback:") }
                            CheckBox {
                                checked: controller.runWithoutReadback
                                enabled: controller.runWithoutEncoding
                                text: qsTr("Benchmark load and render only")
                                onToggled: controller.runWithoutReadback = checked
                            }
                            Item { height: 1; width: 1 }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.largeSpacing

                    SectionPane {
                        title: qsTr("Command preview")
                        iconName: "utilities-terminal"

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                            contentWidth: availableWidth
                            clip: true

                            TextArea {
                                width: parent.availableWidth
                                readOnly: true
                                wrapMode: TextEdit.WrapAnywhere
                                font.family: "Consolas"
                                text: controller.commandLinePreview
                            }
                        }
                    }

                    SectionPane {
                        title: qsTr("Log")
                        iconName: "view-list-details"
                        Layout.preferredHeight: Math.max(window.height - 800, 256)
                        Layout.fillHeight: true

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: -30
                            Item { Layout.fillWidth: true }
                            Button {
                                action: clearLogAction
                                icon.color: Kirigami.Theme.textColor
                                display: AbstractButton.TextBesideIcon
                            }
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            clip: true

                            TextArea {
                                id: logArea
                                width: parent.availableWidth
                                Layout.fillHeight: true
                                readOnly: true
                                font.family: "Consolas"
                                wrapMode: TextEdit.WrapAnywhere
                                text: controller.logText
                            }
                        }
                    }
                }
            }
        }
    }
}
