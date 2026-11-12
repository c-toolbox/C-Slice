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

import org.kde.kirigami as Kirigami
import org.ctoolbox.copy

Kirigami.ApplicationWindow {
    id: window

    // Controller is provided as a context property from copyapplication.cpp
    readonly property var controller: copyController
    // Step 3 shows every output rule for manifests, and only the default rule for files and folders.
    readonly property bool manifestMode: controller.sourceMode === "manifest"
    // plannedCopies rebuilds the plan on every read, so bindings share this copy.
    readonly property var plannedCopies: controller.plannedCopies
    readonly property int readyCopyCount: window.plannedCopies.filter(function(copy) { return copy.skipped.length === 0 }).length
    // What the last check found on the nodes and, for files and folders, on this computer.
    readonly property var targets: controller.targets
    readonly property var presentTargets: window.targets.filter(function(target) { return target.state === "present" })

    x: 96
    y: 128
    visible: true
    color: Kirigami.Theme.alternateBackgroundColor
    minimumWidth: 480
    minimumHeight: 400
    width: 760
    height: 1024

    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Window

    function statusColor(status) {
        switch (status) {
        case "Done":
        case "Copied":
            return Kirigami.Theme.positiveTextColor
        case "Failed":
        case "Errors":
        case "Stopped":
            return Kirigami.Theme.negativeTextColor
        case "Running":
        case "Copying":
            return Kirigami.Theme.neutralTextColor
        default:
            return Kirigami.Theme.textColor
        }
    }

    // "45 MB/s · about 1:20 left"; the estimate follows the measured speed during the copy.
    // Copies with /MT show only the speed.
    function copyTimeText(bytesPerSecond, remainingSeconds, estimatesTime) {
        const parts = [bytesPerSecond > 0 ? qsTr("%1/s").arg(controller.formatDataSize(Math.round(bytesPerSecond)))
                                          : qsTr("measuring speed…")]
        if (estimatesTime) {
            parts.push(remainingSeconds >= 0 ? qsTr("about %1 left").arg(controller.formatDuration(remainingSeconds))
                                             : qsTr("estimating time left…"))
        }
        return parts.join(" · ")
    }

    // "1.2 GB in 0:25 · 48 MB/s average"
    function copyAverageText(bytes, seconds, bytesPerSecond) {
        const text = qsTr("%1 in %2").arg(controller.formatDataSize(bytes)).arg(controller.formatDuration(seconds))
        return bytesPerSecond > 0 ? qsTr("%1 · %2/s average").arg(text).arg(controller.formatDataSize(Math.round(bytesPerSecond))) : text
    }

    // "12 files in 3 folders · 1.2 GB", "not there", "cannot be reached: …"
    function targetStateText(target) {
        switch (target.state) {
        case "checking":
            return qsTr("checking…")
        case "deleting":
            return qsTr("deleting…")
        case "unreachable":
            return qsTr("cannot be reached: %1").arg(target.message)
        case "missing":
            return target.message.length > 0 ? target.message : qsTr("not there")
        }
        const size = controller.formatDataSize(target.totalBytes)
        const text = target.wholeFolder && target.folderCount > 0
            ? qsTr("%1 file(s) in %2 folder(s) · %3").arg(target.fileCount).arg(target.folderCount).arg(size)
            : qsTr("%1 file(s) · %2").arg(target.fileCount).arg(size)
        return target.message.length > 0 ? qsTr("%1 · %2").arg(text).arg(target.message) : text
    }

    // all: "Delete all…", where the sources on this computer are an option of their own.
    function confirmDelete(targets, all) {
        const nodeTargets = targets.filter(function(target) { return !target.source })
        const sourceTargets = targets.filter(function(target) { return target.source })
        const optionalSources = all && nodeTargets.length > 0 ? sourceTargets : []
        const chosen = optionalSources.length > 0 ? nodeTargets : targets
        var files = 0
        var bytes = 0
        chosen.forEach(function(target) { files += target.fileCount; bytes += target.totalBytes })
        if (chosen.length === 1) {
            const where = chosen[0].source ? qsTr("this computer") : (chosen[0].node || qsTr("the destination"))
            deleteTargetsDialog.summary = qsTr("Delete %1 on %2 (%3 file(s), %4)?").arg(chosen[0].path).arg(where).arg(files).arg(controller.formatDataSize(bytes))
        } else {
            const nodes = []
            nodeTargets.forEach(function(target) { if (target.node.length > 0 && nodes.indexOf(target.node) < 0) nodes.push(target.node) })
            deleteTargetsDialog.summary = qsTr("Delete %1 destination(s) on %2 node(s) (%3 file(s), %4)?")
                .arg(chosen.length).arg(nodes.length).arg(files).arg(controller.formatDataSize(bytes))
        }
        deleteTargetsDialog.targetIds = chosen.map(function(target) { return target.id })
        deleteTargetsDialog.sources = optionalSources
        deleteTargetsDialog.open()
    }

    function chooseSource(mode) {
        controller.sourceMode = mode
        if (mode === "file")
            sourceFileDialog.open()
        else if (mode === "folder")
            sourceFolderDialog.open()
        else
            manifestDialog.open()
    }

    // Dropped manifests are loaded; any other file or folder is copied to every node.
    function openDropped(drop) {
        controller.openPaths(Array.from(drop.urls, function(url) { return url.toString() }))
        drop.acceptProposedAction()
    }

    component SectionPane: Pane {
        id: section

        property string title
        property string iconName
        property string trailingText
        default property alias content: sectionContent.data

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
            }
            GridLayout {
                id: sectionContent
                Layout.fillWidth: true
                columns: 1
                rowSpacing: Kirigami.Units.smallSpacing
            }
        }
    }

    // Destinations, node and account of one output rule (or the default rule). The manifest
    // destination has destinations only.
    component RuleEditor: ColumnLayout {
        id: ruleEditor

        readonly property var controller: copyController
        property var rule
        property bool showNode: true
        property bool showDestinations: true
        property bool manifestMode: false

        signal browseRequested(string identifier, int destinationIndex)

        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: ruleEditor.rule.isDefault ? "globe" : (ruleEditor.rule.isManifestDefault ? "folder-network" : "computer")
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                color: Kirigami.Theme.textColor
            }
            Label {
                text: ruleEditor.rule.isDefault ? (ruleEditor.manifestMode ? qsTr("Default rule (all other outputs)")
                                                                           : (ruleEditor.showDestinations ? qsTr("Destination on every node") : qsTr("Every node")))
                                                : (ruleEditor.rule.isManifestDefault ? qsTr("Manifest destination (outputs without their own)") : ruleEditor.rule.identifier)
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: ruleEditor.rule.isDefault
                    ? (ruleEditor.manifestMode && ruleEditor.rule.manifestDefaultSet ? qsTr("node and account; destinations only without a manifest destination") : "")
                    : ruleEditor.rule.isManifestDefault ? qsTr("ahead of the default rule")
                    : (ruleEditor.rule.inManifest ? ruleEditor.rule.source : qsTr("not in loaded manifests"))
                elide: Text.ElideMiddle
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                opacity: 0.55
            }
            ToolButton {
                visible: ruleEditor.showDestinations
                text: qsTr("Add destination")
                icon.name: "list-add"
                icon.color: Kirigami.Theme.textColor
                display: AbstractButton.IconOnly
                onClicked: ruleEditor.controller.addDestination(ruleEditor.rule.identifier, "")
                ToolTip.text: text
                ToolTip.visible: hovered
            }
        }

        RowLayout {
            id: nodeRow
            visible: ruleEditor.showNode && !ruleEditor.rule.isManifestDefault
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            // Index 0 means no own node; assigned but missing nodes stay selectable.
            property var nodeChoices: {
                var names = ruleEditor.controller.nodeNames.slice()
                if (ruleEditor.rule.node.length > 0 && names.indexOf(ruleEditor.rule.node) < 0)
                    names.push(ruleEditor.rule.node)
                var first = ruleEditor.rule.isDefault ? qsTr("No node")
                    : (ruleEditor.rule.effectiveNode.length > 0 ? qsTr("Default rule (%1)").arg(ruleEditor.rule.effectiveNode)
                                                               : qsTr("Default rule (no node)"))
                return [ first ].concat(names)
            }

            Label {
                text: qsTr("Node:")
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                opacity: 0.76
            }
            ComboBox {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                model: nodeRow.nodeChoices
                currentIndex: ruleEditor.rule.node.length > 0 ? nodeRow.nodeChoices.indexOf(ruleEditor.rule.node) : 0
                onActivated: function(index) {
                    ruleEditor.controller.setOutputNode(ruleEditor.rule.identifier, index === 0 ? "" : nodeRow.nodeChoices[index])
                }
                ToolTip.text: qsTr("Machine that {ip} and {node} refer to. Its address comes from the node folder (Settings → Nodes).")
                ToolTip.visible: hovered
            }
            Label {
                Layout.fillWidth: true
                text: ruleEditor.rule.nodeMissing ? qsTr("Node '%1' is not in the node folder.").arg(ruleEditor.rule.effectiveNode)
                    : (ruleEditor.rule.effectiveNode.length > 0
                       ? (ruleEditor.rule.nodeAddress.length > 0 ? ruleEditor.rule.nodeAddress : qsTr("No IP address set."))
                       : "")
                color: ruleEditor.rule.nodeMissing || (ruleEditor.rule.effectiveNode.length > 0 && ruleEditor.rule.nodeAddress.length === 0)
                    ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                opacity: ruleEditor.rule.nodeMissing ? 1 : 0.76
                elide: Text.ElideRight
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            }
        }

        RowLayout {
            id: accountRow
            visible: ruleEditor.controller.accountsSupported && !ruleEditor.rule.isManifestDefault
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            // Index 0 means no own account; assigned but missing accounts stay selectable.
            property var accountChoices: {
                var names = ruleEditor.controller.storedAccountNames.slice()
                if (ruleEditor.rule.account.length > 0 && names.indexOf(ruleEditor.rule.account) < 0)
                    names.push(ruleEditor.rule.account)
                var inherited = ruleEditor.rule.inheritedAccount
                var first = ruleEditor.rule.isDefault
                    ? (inherited.length > 0 ? qsTr("From settings (%1)").arg(inherited) : qsTr("Current Windows user"))
                    : (inherited.length > 0 ? qsTr("Default rule (%1)").arg(inherited) : qsTr("Default rule (current Windows user)"))
                return [ first ].concat(names)
            }

            Label {
                text: qsTr("Account:")
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                opacity: 0.76
            }
            ComboBox {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                model: accountRow.accountChoices
                currentIndex: ruleEditor.rule.account.length > 0 ? accountRow.accountChoices.indexOf(ruleEditor.rule.account) : 0
                onActivated: function(index) {
                    ruleEditor.controller.setOutputAccount(ruleEditor.rule.identifier, index === 0 ? "" : accountRow.accountChoices[index])
                }
                ToolTip.text: qsTr("Account robocopy uses to reach the destination shares. Stored in Windows Credential Manager (Settings → Accounts).")
                ToolTip.visible: hovered
            }
            Label {
                Layout.fillWidth: true
                visible: ruleEditor.rule.accountMissing
                text: qsTr("Account '%1' is not stored on this computer.").arg(ruleEditor.rule.effectiveAccount)
                color: Kirigami.Theme.negativeTextColor
                elide: Text.ElideRight
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            }
            Item { Layout.fillWidth: !ruleEditor.rule.accountMissing }
        }

        Label {
            visible: ruleEditor.showDestinations && ruleEditor.rule.destinations.length === 0
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing
            text: ruleEditor.rule.usesManifestDefault ? qsTr("Uses the manifest destination.")
                : ruleEditor.rule.isManifestDefault ? (ruleEditor.rule.usesDefault ? qsTr("None; outputs use the default rule.") : qsTr("No manifest destination."))
                : ruleEditor.rule.usesDefault ? qsTr("Uses the default rule.")
                : (ruleEditor.rule.isDefault ? qsTr("No default destination.") : qsTr("No destination; this output is skipped."))
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            color: ruleEditor.rule.usesDefault || ruleEditor.rule.usesManifestDefault || ruleEditor.rule.isManifestDefault || !ruleEditor.rule.inManifest
                ? Kirigami.Theme.textColor : Kirigami.Theme.neutralTextColor
            opacity: 0.76
        }

        Repeater {
            model: ruleEditor.showDestinations ? ruleEditor.rule.destinations : []
            delegate: RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                TextField {
                    Layout.fillWidth: true
                    text: modelData
                    placeholderText: qsTr("\\\\{ip}\\media\\{folder} or D:\\folder")
                    onEditingFinished: ruleEditor.controller.setDestination(ruleEditor.rule.identifier, index, text)
                }
                ToolButton {
                    icon.name: "folder-open"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: ruleEditor.browseRequested(ruleEditor.rule.identifier, index)
                    ToolTip.text: qsTr("Browse…")
                    ToolTip.visible: hovered
                }
                ToolButton {
                    icon.name: "list-remove"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: ruleEditor.controller.removeDestination(ruleEditor.rule.identifier, index)
                    ToolTip.text: qsTr("Remove destination")
                    ToolTip.visible: hovered
                }
            }
        }
    }

    function browseDestination(identifier, destinationIndex) {
        destinationFolderDialog.identifier = identifier
        destinationFolderDialog.destinationIndex = destinationIndex
        destinationFolderDialog.open()
    }

    FileDialog {
        id: manifestDialog
        title: qsTr("Open C-Opy manifest")
        nameFilters: [ qsTr("C-Opy manifests (*.c-opy.json)"), qsTr("JSON files (*.json)") ]
        fileMode: FileDialog.OpenFiles
        currentFolder: controller.manifestDialogFolder
        onAccepted: {
            controller.rememberManifestDialogFolder(selectedFile)
            controller.openPaths(Array.from(selectedFiles, function(url) { return url.toString() }), "manifest")
        }
    }

    FileDialog {
        id: sourceFileDialog
        title: qsTr("Open files to copy to the cluster")
        nameFilters: [ qsTr("All files (*)") ]
        fileMode: FileDialog.OpenFiles
        currentFolder: controller.sourceDialogFolder
        onAccepted: {
            controller.rememberSourceDialogFolder(selectedFile, true)
            controller.openPaths(Array.from(selectedFiles, function(url) { return url.toString() }), "file")
        }
    }

    FolderDialog {
        id: sourceFolderDialog
        title: qsTr("Open a folder to mirror to the cluster")
        currentFolder: controller.sourceDialogFolder
        onAccepted: {
            controller.rememberSourceDialogFolder(selectedFolder, false)
            controller.openPath(selectedFolder.toString())
        }
    }

    COpySettingsDialog {
        id: settingsDialog
        controller: window.controller
        parent: Overlay.overlay
    }

    FileDialog {
        id: importPresetDialog
        title: qsTr("Import clusters")
        nameFilters: [ qsTr("JSON files (*.json)") ]
        currentFolder: controller.presetDialogFolder
        onAccepted: {
            controller.rememberPresetDialogFolder(selectedFile)
            controller.importPresets(selectedFile.toString())
        }
    }

    FolderDialog {
        id: destinationFolderDialog
        property string identifier
        property int destinationIndex: -1
        title: qsTr("Choose destination folder for %1").arg(identifier)
        currentFolder: controller.destinationDialogFolder
        onAccepted: {
            controller.rememberDestinationDialogFolder(selectedFolder)
            controller.setDestination(identifier, destinationIndex, controller.urlToLocalPath(selectedFolder))
        }
    }

    Dialog {
        id: savePresetDialog
        title: qsTr("Save cluster as")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 22)
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: {
            presetNameField.text = controller.currentPreset
            presetNameField.selectAll()
            presetNameField.forceActiveFocus()
        }
        onAccepted: controller.savePreset(presetNameField.text)

        ColumnLayout {
            anchors.fill: parent
            Label {
                text: qsTr("The destinations, nodes and accounts are stored under this name.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                opacity: 0.76
            }
            TextField {
                id: presetNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Cluster name, e.g. Dome cluster")
                onAccepted: savePresetDialog.accept()
            }
        }
    }

    Kirigami.PromptDialog {
        id: deletePresetDialog
        title: qsTr("Delete cluster")
        subtitle: qsTr("Delete the saved cluster '%1'?").arg(controller.currentPreset)
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: controller.deletePreset(controller.currentPreset)
    }

    Dialog {
        id: deleteTargetsDialog

        property string summary
        property var targetIds: []
        // Offered with "Delete all…": the sources on this computer, deleted only when checked.
        property var sources: []

        title: qsTr("Delete")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 26)
        standardButtons: Dialog.Yes | Dialog.Cancel
        onOpened: includeSourcesCheck.checked = false
        onAccepted: controller.deleteTargets(includeSourcesCheck.checked
            ? targetIds.concat(sources.map(function(target) { return target.id })) : targetIds)

        ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: deleteTargetsDialog.summary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            CheckBox {
                id: includeSourcesCheck
                visible: deleteTargetsDialog.sources.length > 0
                Layout.fillWidth: true
                text: deleteTargetsDialog.sources.length === 1
                    ? qsTr("Also delete the source on this computer: %1").arg(deleteTargetsDialog.sources[0].path)
                    : qsTr("Also delete the %1 sources on this computer").arg(deleteTargetsDialog.sources.length)
            }
            Label {
                text: qsTr("Files are deleted for good, not moved to the Recycle Bin. Junctions and links are removed without touching what they point to.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                color: Kirigami.Theme.negativeTextColor
            }
        }
    }

    DropArea {
        id: windowDropArea
        anchors.fill: parent
        onEntered: function(drag) { drag.accepted = drag.hasUrls }
        onDropped: function(drop) { window.openDropped(drop) }
    }

    // Shown while something is dragged over the window, on top of the content.
    Rectangle {
        anchors.fill: parent
        z: 1
        visible: windowDropArea.containsDrag
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.08)
        border.color: Kirigami.Theme.highlightColor
        border.width: 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: qsTr("C-Opy")
                level: 1
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Settings")
                icon.name: "configure"
                icon.color: Kirigami.Theme.textColor
                onClicked: settingsDialog.open()
                ToolTip.text: qsTr("Startup options, this computer, robocopy profiles, nodes and accounts")
                ToolTip.visible: hovered
            }
        }

        ScrollView {
            id: mainScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: mainScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing

                // ---- Step 1 ---------------------------------------------------------------
                SectionPane {
                    title: qsTr("1. What to copy")
                    iconName: "document-multiple"
                    trailingText: controller.readyManifestCount > 0 ? qsTr("%1 ready").arg(controller.readyManifestCount) : ""

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: [
                                    { mode: "file", text: qsTr("Open file…"), icon: "document-open" },
                                    { mode: "folder", text: qsTr("Open folder…"), icon: "folder-open" },
                                    { mode: "manifest", text: qsTr("Open C-Slice manifest…"), icon: "document-import" }
                                ]
                                delegate: Button {
                                    text: modelData.text
                                    icon.name: modelData.icon
                                    icon.color: Kirigami.Theme.textColor
                                    checkable: true
                                    checked: controller.sourceMode === modelData.mode
                                    // The binding decides the checked state; a click only opens the dialog.
                                    onClicked: {
                                        checked = Qt.binding(function() { return controller.sourceMode === modelData.mode })
                                        window.chooseSource(modelData.mode)
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Label {
                            visible: controller.sourceMode.length > 0
                            text: controller.sourceMode === "file"
                                ? qsTr("Files are copied to every node of the cluster with the %1 profile.").arg(controller.fileRobocopyProfile)
                                : (controller.sourceMode === "folder"
                                   ? qsTr("Folders are mirrored to every node of the cluster with the %1 profile.").arg(controller.folderRobocopyProfile)
                                   : qsTr("Each output of the manifest is copied to its own node: movies with the %1 profile, image sequences with %2.")
                                         .arg(controller.fileRobocopyProfile).arg(controller.folderRobocopyProfile))
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            opacity: 0.76
                        }

                        // Drop zone for files, folders and manifests; dropping anywhere on the window works too.
                        DropArea {
                            id: sourceDropArea
                            Layout.fillWidth: true
                            implicitHeight: dropZoneContent.implicitHeight + 2 * Kirigami.Units.largeSpacing
                            onEntered: function(drag) { drag.accepted = drag.hasUrls }
                            onDropped: function(drop) { window.openDropped(drop) }

                            Rectangle {
                                anchors.fill: parent
                                radius: Kirigami.Units.smallSpacing
                                color: sourceDropArea.containsDrag
                                    ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                                    : "transparent"
                                border.width: sourceDropArea.containsDrag ? 2 : 1
                                border.color: sourceDropArea.containsDrag
                                    ? Kirigami.Theme.highlightColor
                                    : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                            }

                            RowLayout {
                                id: dropZoneContent
                                anchors.fill: parent
                                anchors.margins: Kirigami.Units.largeSpacing
                                spacing: Kirigami.Units.largeSpacing

                                Kirigami.Icon {
                                    source: "document-import"
                                    implicitWidth: Kirigami.Units.iconSizes.medium
                                    implicitHeight: Kirigami.Units.iconSizes.medium
                                    color: sourceDropArea.containsDrag ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                                    opacity: sourceDropArea.containsDrag ? 1 : 0.65
                                }
                                Label {
                                    text: sourceDropArea.containsDrag
                                        ? qsTr("Drop to add")
                                        : (controller.manifests.length === 0
                                           ? qsTr("Drop files, folders or C-Slice copy manifests (*.c-opy.json) here, or use the buttons above. C-Slice writes a manifest next to its outputs when a slice finishes.")
                                           : qsTr("Drop more files, folders or manifests here"))
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                    opacity: sourceDropArea.containsDrag ? 1 : 0.65
                                }
                            }
                        }

                        Repeater {
                            model: controller.manifests
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: modelData.kind === "folder" ? "folder" : (modelData.kind === "file" ? "document-new" : "text-x-generic")
                                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                    color: Kirigami.Theme.textColor
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        text: modelData.jobName
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.kind === "manifest" ? qsTr("Outputs: %1").arg(modelData.identifiers) : modelData.path
                                        elide: modelData.kind === "manifest" ? Text.ElideRight : Text.ElideMiddle
                                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                        opacity: 0.65
                                        Layout.fillWidth: true
                                    }
                                }
                                Label {
                                    text: modelData.status
                                    color: window.statusColor(modelData.status)
                                }
                                ToolButton {
                                    icon.name: "list-remove"
                                    icon.color: Kirigami.Theme.textColor
                                    enabled: !modelData.busy
                                    onClicked: controller.removeManifest(index)
                                    ToolTip.text: qsTr("Remove")
                                    ToolTip.visible: hovered
                                }
                            }
                        }
                    }
                }

                // ---- Step 2 ---------------------------------------------------------------
                SectionPane {
                    title: qsTr("2. Cluster to copy to")
                    iconName: "network-server"
                    trailingText: controller.currentPreset.length > 0
                        ? (controller.presetModified ? qsTr("%1 (modified)").arg(controller.currentPreset) : controller.currentPreset)
                        : qsTr("None chosen")

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            visible: controller.presets.length === 0
                            text: qsTr("No clusters yet. Put cluster files (*.json) in the cluster folder (Settings → General), or import one.")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            opacity: 0.65
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            visible: controller.presets.length > 0

                            Label { text: qsTr("Cluster:"); opacity: 0.76 }
                            ComboBox {
                                id: clusterCombo
                                Layout.fillWidth: true
                                model: controller.presets
                                textRole: "name"
                                currentIndex: controller.presetNames.indexOf(controller.currentPreset)
                                displayText: currentIndex >= 0 ? currentText : qsTr("Choose a cluster…")
                                onActivated: function(index) { controller.applyPreset(controller.presetNames[index]) }
                                delegate: ItemDelegate {
                                    width: clusterCombo.width
                                    highlighted: clusterCombo.highlightedIndex === index
                                    text: qsTr("%1 · %2 node(s)").arg(modelData.name).arg(modelData.nodes.length)
                                    ToolTip.text: modelData.library ? modelData.filePath : qsTr("Saved in the settings file")
                                    ToolTip.visible: hovered
                                }
                            }
                        }

                        ColumnLayout {
                            id: clusterDetails
                            Layout.fillWidth: true
                            spacing: 0
                            visible: clusterCombo.currentIndex >= 0

                            readonly property var cluster: clusterCombo.currentIndex >= 0 ? controller.presets[clusterCombo.currentIndex] : null
                            readonly property var missing: cluster ? cluster.nodes.filter(function(name) {
                                return controller.nodeNames.map(function(n) { return n.toLowerCase() }).indexOf(name.toLowerCase()) < 0
                            }) : []

                            Label {
                                Layout.fillWidth: true
                                text: clusterDetails.cluster
                                    ? (clusterDetails.cluster.nodes.length > 0 ? qsTr("Nodes: %1").arg(clusterDetails.cluster.nodes.join(", ")) : qsTr("No nodes"))
                                    : ""
                                wrapMode: Text.Wrap
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                opacity: 0.76
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: clusterDetails.missing.length > 0
                                text: qsTr("Not in the node folder: %1 (Settings → Nodes)").arg(clusterDetails.missing.join(", "))
                                wrapMode: Text.Wrap
                                color: Kirigami.Theme.negativeTextColor
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: clusterDetails.cluster ? clusterDetails.cluster.description : ""
                                wrapMode: Text.Wrap
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                opacity: 0.65
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: controller.currentPreset.length > 0
                            wrapMode: Text.Wrap
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            readonly property var receiving: controller.clusterNodes.filter(function(name) {
                                return !(controller.skipLocalNode && name.toLowerCase() === controller.localNode.toLowerCase())
                            })
                            text: controller.localNode.length === 0
                                ? qsTr("This computer (%1) is not in the cluster: all %2 node(s) receive copies.").arg(controller.hostName).arg(receiving.length)
                                : (receiving.length < controller.clusterNodes.length
                                   ? qsTr("This computer is %1 and already has the files: only the other %2 node(s) receive copies.").arg(controller.localNode).arg(receiving.length)
                                   : qsTr("This computer is node %1.").arg(controller.localNode))
                            color: Kirigami.Theme.positiveTextColor
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Save")
                                icon.name: "document-save"
                                icon.color: Kirigami.Theme.textColor
                                visible: controller.presetModified
                                enabled: !controller.currentPresetIsLibrary
                                onClicked: controller.savePreset(controller.currentPreset)
                                ToolTip.text: controller.currentPresetIsLibrary
                                    ? qsTr("Library clusters are read-only here. Use Save as… for your own copy, or edit the file in the cluster folder.")
                                    : qsTr("Save the changed destinations to the cluster")
                                ToolTip.visible: hovered
                            }
                            Button {
                                text: qsTr("Save as…")
                                icon.name: "document-save-as"
                                icon.color: Kirigami.Theme.textColor
                                onClicked: savePresetDialog.open()
                            }
                            Button {
                                text: qsTr("Import…")
                                icon.name: "document-import"
                                icon.color: Kirigami.Theme.textColor
                                onClicked: importPresetDialog.open()
                                ToolTip.text: qsTr("Import clusters from a JSON file into the settings file. Files in the cluster folder are listed without importing.")
                                ToolTip.visible: hovered
                            }
                            ToolButton {
                                icon.name: "edit-delete"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.currentPreset.length > 0 && !controller.currentPresetIsLibrary
                                onClicked: deletePresetDialog.open()
                                ToolTip.text: qsTr("Delete the saved cluster")
                                ToolTip.visible: hovered
                            }
                        }
                    }
                }

                // ---- Step 3 ---------------------------------------------------------------
                SectionPane {
                    title: qsTr("3. Destinations")
                    iconName: "folder-network"
                    trailingText: window.plannedCopies.length > 0 ? qsTr("%1 copy(ies) planned").arg(window.readyCopyCount) : ""

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            visible: controller.sourceMode.length === 0
                            text: qsTr("Choose what to copy in step 1 first.")
                            Layout.fillWidth: true
                            opacity: 0.65
                        }

                        Label {
                            visible: window.manifestMode
                            text: qsTr("Each output is copied to its own destinations, else to the cluster's manifest destination, else to the default rule's. {ip} and {node} are the output's node, {folder} is the output directory name; also {identifier}, {job}, {date} and {ip:name}.")
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            opacity: 0.65
                        }

                        // Files and folders: the same path on every node, or the default rule.
                        ColumnLayout {
                            visible: controller.sourceMode.length > 0 && !window.manifestMode
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            RadioButton {
                                text: qsTr("Same path on every node, through the cluster's drive shares")
                                checked: controller.samePath
                                onClicked: controller.samePath = true
                            }
                            RadioButton {
                                text: qsTr("A fixed destination on every node")
                                checked: !controller.samePath
                                onClicked: controller.samePath = false
                            }

                            // Drive shares of the current cluster.
                            ColumnLayout {
                                visible: controller.samePath
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.gridUnit
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Each drive of this computer maps to a share on every node, e.g. D: → \\\\{ip}\\d$, so D:\\Media\\Show is mirrored to D:\\Media\\Show on each node.")
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                    opacity: 0.65
                                }

                                Repeater {
                                    model: controller.driveShares
                                    delegate: RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        Label {
                                            text: modelData.drive
                                            font.bold: true
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                                        }
                                        Label { text: "→"; opacity: 0.65 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: modelData.share
                                            placeholderText: "\\\\{ip}\\d$"
                                            onEditingFinished: controller.setDriveShare(modelData.drive, text)
                                        }
                                        ToolButton {
                                            icon.name: "list-remove"
                                            icon.color: Kirigami.Theme.textColor
                                            onClicked: controller.setDriveShare(modelData.drive, "")
                                            ToolTip.text: qsTr("Remove the share of %1 from the cluster").arg(modelData.drive)
                                            ToolTip.visible: hovered
                                        }
                                    }
                                }

                                Repeater {
                                    model: controller.unsharedDrives
                                    delegate: Kirigami.InlineMessage {
                                        Layout.fillWidth: true
                                        visible: true
                                        type: Kirigami.MessageType.Warning
                                        text: qsTr("Drive %1 has no share in this cluster, so nothing on %1 can be copied to the same path.").arg(modelData)
                                        actions: [
                                            Kirigami.Action {
                                                text: qsTr("Use %1").arg("\\\\{ip}\\" + modelData.charAt(0).toLowerCase() + "$")
                                                icon.name: "list-add"
                                                onTriggered: controller.setDriveShare(modelData, "\\\\{ip}\\" + modelData.charAt(0).toLowerCase() + "$")
                                            }
                                        ]
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    TextField {
                                        id: newDriveField
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                                        placeholderText: "F:"
                                        maximumLength: 2
                                    }
                                    TextField {
                                        id: newShareField
                                        Layout.fillWidth: true
                                        placeholderText: "\\\\{ip}\\f$"
                                        onAccepted: addShareButton.clicked()
                                    }
                                    Button {
                                        id: addShareButton
                                        text: qsTr("Add drive")
                                        icon.name: "list-add"
                                        icon.color: Kirigami.Theme.textColor
                                        enabled: /^[A-Za-z]:?$/.test(newDriveField.text.trim()) && newShareField.text.trim().length > 0
                                        onClicked: {
                                            if (!enabled)
                                                return
                                            controller.setDriveShare(newDriveField.text, newShareField.text)
                                            newDriveField.clear()
                                            newShareField.clear()
                                        }
                                    }
                                }
                            }

                            // The account (and, for a fixed destination, the destinations) of the default rule.
                            RuleEditor {
                                visible: controller.destinations.length > 0
                                rule: controller.destinations[0]
                                showNode: false
                                showDestinations: !controller.samePath
                                onBrowseRequested: function(identifier, destinationIndex) { window.browseDestination(identifier, destinationIndex) }
                            }

                            Label {
                                visible: !controller.samePath
                                Layout.fillWidth: true
                                text: qsTr("{ip} and {node} are the node, {folder} is the %1.")
                                    .arg(controller.sourceMode === "file" ? qsTr("name of the folder the file is in") : qsTr("folder name"))
                                wrapMode: Text.Wrap
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                opacity: 0.65
                            }
                        }

                        // Manifests: the default rule and every output.
                        Repeater {
                            model: window.manifestMode ? controller.destinations : []
                            delegate: RuleEditor {
                                rule: modelData
                                manifestMode: true
                                onBrowseRequested: function(identifier, destinationIndex) { window.browseDestination(identifier, destinationIndex) }
                            }
                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                            visible: window.plannedCopies.length > 0
                        }

                        Label {
                            visible: window.plannedCopies.length > 0
                            text: qsTr("Planned copies")
                            font.bold: true
                        }

                        Repeater {
                            model: window.plannedCopies
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                opacity: modelData.skipped.length > 0 ? 0.6 : 1

                                // Copies skipped for another reason cannot be turned on.
                                CheckBox {
                                    checked: !modelData.disabled && modelData.skipped.length === 0
                                    enabled: modelData.disabled || modelData.skipped.length === 0
                                    onToggled: controller.setPlannedCopyEnabled(modelData.key, checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: checked ? qsTr("Copy to this destination; uncheck to skip it") : qsTr("Skipped; check to copy to this destination")
                                }
                                Kirigami.Icon {
                                    source: modelData.skipped.length > 0 ? "dialog-cancel" : (modelData.mirror ? "folder-sync" : "edit-copy")
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.textColor
                                }
                                Label {
                                    text: modelData.node.length > 0
                                        ? qsTr("%1 → %2").arg(window.manifestMode ? modelData.identifier : modelData.jobName).arg(modelData.node)
                                        : (window.manifestMode ? modelData.identifier : modelData.jobName)
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.skipped.length > 0 ? qsTr("skipped: %1").arg(modelData.skipped) : modelData.destination
                                    color: modelData.skipped.length > 0 && !modelData.local && !modelData.disabled
                                        ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                }
                                Label {
                                    visible: modelData.skipped.length === 0
                                    text: (modelData.mirror ? qsTr("mirror · ") : "") + modelData.profileName
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                    opacity: 0.65
                                }
                            }
                        }
                    }
                }

                // ---- Step 4 ---------------------------------------------------------------
                SectionPane {
                    title: qsTr("4. Check the nodes")
                    iconName: "network-connect"
                    trailingText: {
                        if (window.targets.length === 0)
                            return ""
                        if (controller.targetsBusy)
                            return qsTr("Checking…")
                        const nodes = controller.checkedNodes
                        const reachable = nodes.filter(function(node) { return node.state === "reachable" }).length
                        return qsTr("%1 of %2 node(s) reachable · %3 found").arg(reachable).arg(nodes.length).arg(window.presentTargets.length)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            text: window.manifestMode
                                ? qsTr("Checks that every node can be reached and what is already at each planned destination. Anything found can be deleted here.")
                                : qsTr("Checks that every node can be reached and what is already at each planned destination. Anything found can be deleted here, on the nodes and on this computer at the same time.")
                            wrapMode: Text.Wrap
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            opacity: 0.65
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: qsTr("Check nodes")
                                icon.name: "view-refresh"
                                icon.color: Kirigami.Theme.textColor
                                enabled: window.readyCopyCount > 0 && !controller.targetsBusy
                                onClicked: controller.checkTargets()
                                ToolTip.text: qsTr("Connect to every node of the planned copies and list what is already there")
                                ToolTip.visible: hovered
                            }
                            Button {
                                text: qsTr("Delete all…")
                                icon.name: "edit-delete"
                                icon.color: Kirigami.Theme.textColor
                                enabled: window.presentTargets.length > 0 && !controller.targetsBusy && !controller.running
                                onClicked: window.confirmDelete(window.presentTargets, true)
                                ToolTip.text: controller.running ? qsTr("Not while copying") : qsTr("Delete everything that was found")
                                ToolTip.visible: hovered
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            visible: controller.targetsStale && !controller.targetsBusy
                            type: Kirigami.MessageType.Information
                            text: qsTr("The planned copies changed since the last check.")
                            actions: [
                                Kirigami.Action {
                                    text: qsTr("Check again")
                                    icon.name: "view-refresh"
                                    onTriggered: controller.checkTargets()
                                }
                            ]
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: controller.checkedNodes.length > 0
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: controller.checkedNodes
                                delegate: Kirigami.Chip {
                                    text: modelData.node
                                    icon.name: modelData.state === "reachable" ? "network-connect"
                                        : (modelData.state === "checking" ? "view-refresh" : "network-disconnect")
                                    icon.color: modelData.state === "reachable" ? Kirigami.Theme.positiveTextColor
                                        : (modelData.state === "checking" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.negativeTextColor)
                                    closable: false
                                    checkable: false
                                    ToolTip.text: modelData.state === "reachable" ? qsTr("%1 can be reached").arg(modelData.address || modelData.node)
                                        : (modelData.state === "checking" ? qsTr("Checking %1…").arg(modelData.address || modelData.node)
                                                                          : qsTr("%1 cannot be reached: %2").arg(modelData.address || modelData.node).arg(modelData.message))
                                    ToolTip.visible: hovered
                                }
                            }
                        }

                        Repeater {
                            model: window.targets
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                opacity: modelData.state === "missing" ? 0.6 : 1

                                Kirigami.Icon {
                                    source: modelData.source ? "computer" : (modelData.wholeFolder ? "folder" : "document-new")
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.textColor
                                }
                                Label {
                                    text: modelData.source ? qsTr("This computer")
                                        : (modelData.node.length > 0 ? modelData.node : (window.manifestMode ? modelData.identifier : modelData.jobName))
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                }
                                Label {
                                    Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                                    text: window.targetStateText(modelData)
                                    elide: Text.ElideRight
                                    color: modelData.state === "unreachable" || (modelData.state === "present" && modelData.message.length > 0)
                                        ? Kirigami.Theme.negativeTextColor
                                        : (modelData.state === "present" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.textColor)
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                }
                                ToolButton {
                                    icon.name: "edit-delete"
                                    icon.color: Kirigami.Theme.textColor
                                    opacity: modelData.state === "present" ? 1 : 0
                                    enabled: modelData.state === "present" && !controller.running
                                    onClicked: window.confirmDelete([ modelData ], false)
                                    ToolTip.text: modelData.source ? qsTr("Delete the source on this computer") : qsTr("Delete on %1").arg(modelData.node || qsTr("the destination"))
                                    ToolTip.visible: hovered
                                }
                            }
                        }
                    }
                }

                // ---- Step 5 ---------------------------------------------------------------
                SectionPane {
                    title: qsTr("5. Copy")
                    iconName: "edit-copy"
                    trailingText: controller.running
                        ? qsTr("Copying… %1 · %2").arg(controller.formatDuration(controller.elapsedSeconds))
                              .arg(window.copyTimeText(controller.copyRate, controller.remainingSeconds, controller.remainingSeconds >= 0))
                        : controller.tasks.length > 0 && controller.elapsedSeconds > 0
                              ? qsTr("%1 task(s) · last copy: %2").arg(controller.tasks.length)
                                    .arg(window.copyAverageText(controller.copiedBytes, controller.elapsedSeconds, controller.copyRate))
                              : qsTr("%1 task(s)").arg(controller.tasks.length)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: window.readyCopyCount > 0 ? qsTr("Start %1 copy(ies)").arg(window.readyCopyCount) : qsTr("Start copy")
                                icon.name: "media-playback-start"
                                icon.color: Kirigami.Theme.textColor
                                highlighted: window.readyCopyCount > 0
                                // Never while something on the destinations is being deleted.
                                enabled: window.readyCopyCount > 0 && !window.targets.some(function(target) { return target.state === "deleting" })
                                onClicked: controller.startCopy()
                            }
                            Button {
                                text: qsTr("Stop")
                                icon.name: "media-playback-stop"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.running
                                onClicked: controller.stopCopy()
                            }
                            Button {
                                text: qsTr("Retry failed")
                                icon.name: "view-refresh"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.failedTaskCount > 0
                                onClicked: controller.retryFailed()
                            }
                            Button {
                                text: qsTr("Clear finished")
                                icon.name: "edit-clear-list"
                                icon.color: Kirigami.Theme.textColor
                                enabled: controller.finishedTaskCount > 0
                                onClicked: controller.clearFinished()
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Repeater {
                            model: controller.tasks
                            delegate: ColumnLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing / 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing
                                    Label {
                                        text: qsTr("%1 · %2").arg(modelData.jobName).arg(modelData.identifier)
                                        font.bold: true
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: "\u2192 " + modelData.destination
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        text: modelData.accountName.length > 0 ? qsTr("%1 · as %2").arg(modelData.profileName).arg(modelData.accountName) : modelData.profileName
                                        opacity: 0.65
                                    }
                                    Label {
                                        text: modelData.totalFiles > 1 && modelData.status === "Running"
                                            ? qsTr("%1 / %2 files").arg(Math.min(modelData.filesStarted, modelData.totalFiles)).arg(modelData.totalFiles)
                                            : ""
                                        opacity: 0.65
                                    }
                                    Label {
                                        text: modelData.status === "Running"
                                            ? qsTr("%1 · %2").arg(controller.formatDuration(modelData.elapsedSeconds))
                                                  .arg(window.copyTimeText(modelData.bytesPerSecond, modelData.remainingSeconds, modelData.estimatesTime))
                                            : modelData.status !== "Pending" && modelData.elapsedSeconds > 0
                                                  ? window.copyAverageText(modelData.copiedBytes, modelData.elapsedSeconds, modelData.bytesPerSecond)
                                                  : ""
                                        opacity: 0.65
                                    }
                                    Label {
                                        text: modelData.status
                                        color: window.statusColor(modelData.status)
                                    }
                                }
                                // Drawn here like C-Slice's render progress: the desktop style's bar is not
                                // visible on Windows.
                                ProgressBar {
                                    id: taskProgressBar
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Kirigami.Units.gridUnit * 0.8
                                    implicitHeight: Kirigami.Units.gridUnit * 0.8
                                    padding: 1
                                    from: 0
                                    to: 1
                                    value: modelData.progress
                                    indeterminate: modelData.status === "Running" && modelData.filesStarted === 0
                                    background: Rectangle {
                                        implicitHeight: taskProgressBar.implicitHeight
                                        radius: height / 2
                                        color: Kirigami.Theme.alternateBackgroundColor
                                        opacity: 0.9
                                    }
                                    contentItem: Item {
                                        implicitHeight: taskProgressBar.implicitHeight
                                        clip: true

                                        Rectangle {
                                            visible: !taskProgressBar.indeterminate
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: parent.width * Math.max(0, Math.min(1, taskProgressBar.visualPosition))
                                            radius: height / 2
                                            color: modelData.status === "Failed" || modelData.status === "Stopped"
                                                ? Kirigami.Theme.negativeTextColor : "lightgreen"
                                        }
                                        // Robocopy has not reported a file yet: a block slides back and forth.
                                        Rectangle {
                                            id: busyBlock
                                            visible: taskProgressBar.indeterminate
                                            width: parent.width / 5
                                            height: parent.height
                                            radius: height / 2
                                            color: "lightgreen"
                                            SequentialAnimation on x {
                                                running: taskProgressBar.indeterminate && taskProgressBar.visible
                                                loops: Animation.Infinite
                                                NumberAnimation { from: 0; to: Math.max(0, taskProgressBar.availableWidth - busyBlock.width); duration: 900; easing.type: Easing.InOutQuad }
                                                NumberAnimation { from: Math.max(0, taskProgressBar.availableWidth - busyBlock.width); to: 0; duration: 900; easing.type: Easing.InOutQuad }
                                            }
                                        }
                                    }
                                }
                                Label {
                                    visible: modelData.message.length > 0
                                    text: modelData.message
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                    opacity: 0.65
                                }
                            }
                        }
                    }
                }

                SectionPane {
                    title: qsTr("Log")
                    iconName: "view-pim-notes"
                    Layout.preferredHeight: 220

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: -30
                        Item { Layout.fillWidth: true }
                        Button {
                            onClicked: controller.clearLog()
                            text: qsTr("Clear Log")
                            icon.color: Kirigami.Theme.textColor
                            icon.name: "edit-clear-history"
                            display: AbstractButton.TextBesideIcon
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true

                        TextArea {
                            text: controller.logText
                            readOnly: true
                            wrapMode: Text.Wrap
                            onTextChanged: cursorPosition = length
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: app
        function onActivationRequested() {
            if (window.visibility === Window.Minimized) {
                window.showNormal()
            }
            window.raise()
            window.requestActivate()
        }
    }

    footer: Pane {
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        implicitHeight: footerLayout.implicitHeight + topPadding + bottomPadding

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            ToolButton {
                icon.name: "folder-open"
                icon.color: Kirigami.Theme.textColor
                onClicked: controller.openSettingsFolder()
                ToolTip.text: qsTr("Open the folder with the settings file")
                ToolTip.visible: hovered
            }
            Label {
                text: controller.settingsFilePath
                elide: Text.ElideMiddle
                opacity: 0.55
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("C-Opy v%1").arg(app.version())
                opacity: 0.76
            }
        }
    }
}
