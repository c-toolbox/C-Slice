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

import org.kde.kirigami as Kirigami

// General and Robocopy are applied with Save; Nodes and Accounts are written immediately,
// since they are files and Credential Manager entries of their own.
Dialog {
    id: root

    property var controller

    title: qsTr("C-Opy Settings")
    modal: true
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : 640, Kirigami.Units.gridUnit * 40)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : 760, Kirigami.Units.gridUnit * 46)
    anchors.centerIn: parent
    padding: Kirigami.Units.largeSpacing

    // Edited copy of the robocopy profiles; written to the profile file on Save.
    ListModel { id: profileModel }

    function profileNames() {
        var names = []
        for (var i = 0; i < profileModel.count; ++i)
            names.push(profileModel.get(i).name)
        return names
    }

    function fillProfiles(profiles) {
        profileModel.clear()
        for (var i = 0; i < profiles.length; ++i)
            profileModel.append({ name: profiles[i].name, arguments: profiles[i].arguments, description: profiles[i].description })
        refreshProfileChoices()
    }

    // Keeps the chosen profiles (by name) while the edited names change.
    function refreshProfileChoices(fileName, folderName) {
        var file = fileName !== undefined ? fileName : fileProfileCombo.currentText
        var folder = folderName !== undefined ? folderName : folderProfileCombo.currentText
        var names = profileNames()
        fileProfileCombo.model = names
        folderProfileCombo.model = names
        fileProfileCombo.currentIndex = Math.max(0, names.indexOf(file))
        folderProfileCombo.currentIndex = Math.max(0, names.indexOf(folder))
    }

    function loadFields() {
        fillProfiles(root.controller.robocopyProfiles)
        refreshProfileChoices(root.controller.fileRobocopyProfile, root.controller.folderRobocopyProfile)
        startupClusterCombo.currentIndex = Math.max(0, root.controller.presetNames.indexOf(root.controller.startupCluster) + 1)
        localNodeCombo.currentIndex = Math.max(0, localNodeCombo.model.indexOf(root.controller.localNodeSetting))
        skipLocalCheck.checked = root.controller.skipLocalNode
        autoStartCheck.checked = root.controller.autoStart
        skipHiddenCheck.checked = root.controller.skipHiddenFiles
        parallelSpin.value = root.controller.maxParallel
        presetFolderField.text = root.controller.presetFolder
        nodeFolderField.text = root.controller.nodeFolder
    }

    function storeFields() {
        refreshProfileChoices()
        var profiles = []
        for (var i = 0; i < profileModel.count; ++i) {
            var profile = profileModel.get(i)
            profiles.push({ name: profile.name, arguments: profile.arguments, description: profile.description })
        }
        root.controller.setRobocopyProfiles(profiles)
        root.controller.fileRobocopyProfile = fileProfileCombo.currentText.trim()
        root.controller.folderRobocopyProfile = folderProfileCombo.currentText.trim()
        root.controller.startupCluster = startupClusterCombo.currentIndex > 0 ? startupClusterCombo.currentText : ""
        root.controller.localNodeSetting = localNodeCombo.currentIndex > 0 ? localNodeCombo.currentText : ""
        root.controller.skipLocalNode = skipLocalCheck.checked
        root.controller.autoStart = autoStartCheck.checked
        root.controller.skipHiddenFiles = skipHiddenCheck.checked
        root.controller.maxParallel = parallelSpin.value
        root.controller.presetFolder = presetFolderField.text
        root.controller.nodeFolder = nodeFolderField.text
    }

    // Index 0 is `first` (no account of its own); an assigned but missing account stays selectable.
    function accountChoices(first, assigned) {
        var names = root.controller.storedAccountNames.slice()
        if (assigned.length > 0 && names.indexOf(assigned) < 0)
            names.push(assigned)
        return [ first ].concat(names)
    }

    onOpened: loadFields()

    FolderDialog {
        id: libraryFolderDialog
        property bool forNodes: false
        title: forNodes ? qsTr("Choose the node folder") : qsTr("Choose the cluster folder")
        currentFolder: "file:///" + (forNodes ? nodeFolderField.text : presetFolderField.text).replace(/\\/g, "/")
        onAccepted: {
            if (forNodes)
                nodeFolderField.text = root.controller.urlToLocalPath(selectedFolder)
            else
                presetFolderField.text = root.controller.urlToLocalPath(selectedFolder)
        }
    }

    Kirigami.PromptDialog {
        id: removeAccountDialog
        property string accountName
        parent: root.parent
        title: qsTr("Remove account")
        subtitle: qsTr("Remove the account '%1' and its password from Windows Credential Manager? Clusters that use it can no longer copy until it is stored again.").arg(accountName)
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: root.controller.removeAccount(accountName)
    }

    header: TabBar {
        id: tabs
        TabButton { text: qsTr("General") }
        TabButton { text: qsTr("Robocopy") }
        TabButton { text: qsTr("Nodes") }
        TabButton { text: qsTr("Accounts"); visible: root.controller.accountsSupported; width: visible ? implicitWidth : 0 }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        // ---- General ----------------------------------------------------------------------
        ScrollView {
            id: generalScroll
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                width: generalScroll.availableWidth

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: qsTr("Startup")
                }

                ComboBox {
                    id: startupClusterCombo
                    Kirigami.FormData.label: qsTr("Cluster on startup:")
                    Layout.fillWidth: true
                    model: [ qsTr("Last used (%1)").arg(root.controller.currentPreset.length > 0 ? root.controller.currentPreset : qsTr("none")) ].concat(root.controller.presetNames)
                    ToolTip.text: qsTr("Cluster chosen in step 2 when C-Opy starts. Its destinations, nodes and accounts replace the working mapping.")
                    ToolTip.visible: hovered
                }

                SpinBox {
                    id: parallelSpin
                    Kirigami.FormData.label: qsTr("Parallel copies:")
                    from: 1
                    to: 32
                    ToolTip.text: qsTr("Maximum number of robocopy processes at the same time, e.g. one per node.")
                    ToolTip.visible: hovered
                }

                CheckBox {
                    id: autoStartCheck
                    text: qsTr("Start copying automatically when a file, folder or manifest arrives")
                    ToolTip.text: qsTr("For hands-off copying after each slice with C-Slice's \"Open outputs in C-Opy\".")
                    ToolTip.visible: hovered
                }

                CheckBox {
                    id: skipHiddenCheck
                    text: qsTr("Skip hidden and system files")
                    ToolTip.text: qsTr("Robocopy /XA:SH: files such as Thumbs.db and desktop.ini are not copied, and mirrored folders on the nodes lose the ones they have. Files inside hidden folders are still copied.")
                    ToolTip.visible: hovered
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: qsTr("This computer")
                }

                Label {
                    Kirigami.FormData.label: qsTr("Host name:")
                    text: root.controller.hostName
                }

                ComboBox {
                    id: localNodeCombo
                    Kirigami.FormData.label: qsTr("This computer is node:")
                    Layout.fillWidth: true
                    model: {
                        var names = root.controller.nodeNames.slice()
                        if (root.controller.localNodeSetting.length > 0 && names.indexOf(root.controller.localNodeSetting) < 0)
                            names.push(root.controller.localNodeSetting)
                        var detected = root.controller.detectedLocalNode.length > 0
                            ? qsTr("Detect automatically (%1)").arg(root.controller.detectedLocalNode)
                            : qsTr("Detect automatically (no node matches)")
                        return [ detected ].concat(names)
                    }
                    ToolTip.text: qsTr("Detected when a node's name is this computer's host name, or its address is one of this computer's addresses. Choose the node yourself when the names or addresses differ.")
                    ToolTip.visible: hovered
                }

                CheckBox {
                    id: skipLocalCheck
                    text: qsTr("Do not copy to this computer when it is a node of the cluster")
                    ToolTip.text: qsTr("E.g. on the master machine: files and folders go only to the other nodes, and manifest outputs of this computer's node are not copied.")
                    ToolTip.visible: hovered
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: qsTr("Library folders")
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Clusters:")
                    Layout.fillWidth: true
                    TextField {
                        id: presetFolderField
                        Layout.fillWidth: true
                        placeholderText: root.controller.defaultPresetFolder
                        ToolTip.text: qsTr("Every *.json file in this folder (and its subfolders) is a cluster. Leave empty for %1.").arg(root.controller.defaultPresetFolder)
                        ToolTip.visible: hovered
                    }
                    ToolButton {
                        icon.name: "document-open-folder"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: {
                            libraryFolderDialog.forNodes = false
                            libraryFolderDialog.open()
                        }
                        ToolTip.text: qsTr("Choose folder…")
                        ToolTip.visible: hovered
                    }
                    ToolButton {
                        icon.name: "folder-open"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: root.controller.openPresetFolder()
                        ToolTip.text: qsTr("Open the cluster folder")
                        ToolTip.visible: hovered
                    }
                }

                RowLayout {
                    Kirigami.FormData.label: qsTr("Nodes:")
                    Layout.fillWidth: true
                    TextField {
                        id: nodeFolderField
                        Layout.fillWidth: true
                        placeholderText: root.controller.defaultNodeFolder
                        ToolTip.text: qsTr("Every *.json file in this folder (and its subfolders) is a node. Leave empty for %1. C-Troll's nodes folder works too.").arg(root.controller.defaultNodeFolder)
                        ToolTip.visible: hovered
                    }
                    ToolButton {
                        icon.name: "document-open-folder"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: {
                            libraryFolderDialog.forNodes = true
                            libraryFolderDialog.open()
                        }
                        ToolTip.text: qsTr("Choose folder…")
                        ToolTip.visible: hovered
                    }
                    ToolButton {
                        icon.name: "folder-open"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: root.controller.openNodeFolder()
                        ToolTip.text: qsTr("Open the node folder")
                        ToolTip.visible: hovered
                    }
                }

                Button {
                    text: qsTr("Reload clusters and nodes")
                    icon.name: "view-refresh"
                    icon.color: Kirigami.Theme.textColor
                    onClicked: root.controller.reloadLibrary()
                }
            }
        }

        // ---- Robocopy ---------------------------------------------------------------------
        ScrollView {
            id: robocopyScroll
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: robocopyScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing

                Kirigami.FormLayout {
                    Layout.fillWidth: true

                    ComboBox {
                        id: fileProfileCombo
                        Kirigami.FormData.label: qsTr("Single files use:")
                        Layout.fillWidth: true
                        ToolTip.text: qsTr("Opened files, and movie outputs of C-Slice manifests.")
                        ToolTip.visible: hovered
                    }
                    ComboBox {
                        id: folderProfileCombo
                        Kirigami.FormData.label: qsTr("Folders use:")
                        Layout.fillWidth: true
                        ToolTip.text: qsTr("Opened folders, and image sequence outputs of C-Slice manifests.")
                        ToolTip.visible: hovered
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Every copy runs: robocopy <source> <destination> <file pattern> /MIR <profile arguments> /XJ /BYTES /FP. Folders and manifest outputs are always mirrored, so only new and changed files are copied and files removed from the source are removed from the destination. Single opened files are copied only when changed, without /MIR. Junctions and symbolic links are excluded. /MIR and /PURGE in a profile are ignored. The profiles are saved in %1.").arg(root.controller.robocopyProfilesFilePath)
                    wrapMode: Text.Wrap
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.76
                }

                Repeater {
                    model: profileModel
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            TextField {
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                                text: model.name
                                placeholderText: qsTr("Profile name")
                                font.bold: true
                                onTextEdited: profileModel.setProperty(index, "name", text)
                                onEditingFinished: root.refreshProfileChoices()
                            }
                            TextField {
                                Layout.fillWidth: true
                                text: model.arguments
                                placeholderText: qsTr("/J /R:2 /W:5 /NDL")
                                font.family: "monospace"
                                onTextEdited: profileModel.setProperty(index, "arguments", text)
                            }
                            ToolButton {
                                icon.name: "list-remove"
                                icon.color: Kirigami.Theme.textColor
                                enabled: profileModel.count > 1
                                onClicked: {
                                    profileModel.remove(index)
                                    root.refreshProfileChoices()
                                }
                                ToolTip.text: qsTr("Remove profile")
                                ToolTip.visible: hovered
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: model.description
                            placeholderText: qsTr("Description (optional)")
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            onTextEdited: profileModel.setProperty(index, "description", text)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Button {
                        text: qsTr("Add profile")
                        icon.name: "list-add"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: {
                            profileModel.append({ name: qsTr("New profile"), arguments: "/R:2 /W:5 /NDL", description: "" })
                            root.refreshProfileChoices()
                        }
                    }
                    Button {
                        text: qsTr("Restore built-in profiles")
                        icon.name: "edit-undo"
                        icon.color: Kirigami.Theme.textColor
                        onClicked: root.fillProfiles(root.controller.defaultRobocopyProfiles)
                        ToolTip.text: qsTr("Replace the list with \"Large files\" and \"Many small files\". Nothing is saved until you press Save.")
                        ToolTip.visible: hovered
                    }
                    Item { Layout.fillWidth: true }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Large files: /J (unbuffered I/O) suits video files of several GB. Many small files: /MT:n copies n files at once per robocopy, which suits image sequences; it hides per-file progress. Add /FFT when the destinations are NAS or Samba shares whose time stamps are rounded, so unchanged files are not copied again.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.65
                }
            }
        }

        // ---- Nodes ------------------------------------------------------------------------
        ScrollView {
            id: nodesScroll
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: nodesScroll.availableWidth
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: qsTr("The machines that clusters refer to by name, one JSON file per node with its name and IP address (or host name), like C-Troll nodes. Addresses differ between networks, so every computer keeps its own node folder. Changes here are written to the node files right away.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.65
                }

                Label {
                    visible: root.controller.nodes.length === 0
                    text: qsTr("No nodes in %1.").arg(root.controller.nodeFolder)
                    Layout.fillWidth: true
                    opacity: 0.65
                }

                Repeater {
                    model: root.controller.nodes
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        readonly property bool isLocal: modelData.name.toLowerCase() === root.controller.localNode.toLowerCase()

                        Kirigami.Icon {
                            source: "computer"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            color: Kirigami.Theme.textColor
                        }
                        Label {
                            text: parent.isLocal ? qsTr("%1 (this computer)").arg(modelData.name) : modelData.name
                            font.bold: true
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            elide: Text.ElideRight
                        }
                        TextField {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            text: modelData.ipAddress
                            placeholderText: qsTr("IP address or host")
                            onEditingFinished: {
                                if (!root.controller.setNodeAddress(modelData.name, text))
                                    text = modelData.ipAddress
                            }
                            ToolTip.text: modelData.filePath
                            ToolTip.visible: hovered
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.description
                            elide: Text.ElideRight
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            opacity: 0.65
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: nodeNameField
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                        placeholderText: qsTr("Node name, e.g. nrkp-node1")
                    }
                    TextField {
                        id: nodeAddressField
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                        placeholderText: qsTr("IP address or host")
                        onAccepted: addNodeButton.clicked()
                    }
                    TextField {
                        id: nodeDescriptionField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Description (optional)")
                        onAccepted: addNodeButton.clicked()
                    }
                    Button {
                        id: addNodeButton
                        text: qsTr("Add node")
                        icon.name: "list-add"
                        icon.color: Kirigami.Theme.textColor
                        enabled: nodeNameField.text.trim().length > 0
                        onClicked: {
                            if (!enabled)
                                return
                            if (root.controller.addNode(nodeNameField.text, nodeAddressField.text, nodeDescriptionField.text)) {
                                nodeNameField.clear()
                                nodeAddressField.clear()
                                nodeDescriptionField.clear()
                            }
                        }
                    }
                }

                Label {
                    text: qsTr("To rename or remove a node, edit or delete its file in the node folder (General tab), then reload.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.65
                }
            }
        }

        // ---- Accounts ---------------------------------------------------------------------
        ScrollView {
            id: accountsScroll
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: accountsScroll.availableWidth
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: qsTr("Accounts for password-protected shares. Passwords are kept in Windows Credential Manager as generic credentials (C-Opy/<name>) for your Windows user only, never in clusters. Robocopy uses an account only for its own network connections, like runas /netonly. Changes are stored right away.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.65
                }

                Repeater {
                    model: root.controller.storedAccounts
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "user-identity"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            color: Kirigami.Theme.textColor
                        }
                        Label {
                            text: modelData.name
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.userName
                            elide: Text.ElideRight
                            opacity: 0.76
                        }
                        ToolButton {
                            icon.name: "edit-delete"
                            icon.color: Kirigami.Theme.textColor
                            onClicked: {
                                removeAccountDialog.accountName = modelData.name
                                removeAccountDialog.open()
                            }
                            ToolTip.text: qsTr("Remove account from Windows Credential Manager")
                            ToolTip.visible: hovered
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label { text: qsTr("Account name:"); opacity: 0.76 }
                    TextField {
                        id: accountNameField
                        Layout.fillWidth: true
                        placeholderText: qsTr("e.g. nrkp_2026_cluster")
                    }
                    Label { text: qsTr("User:"); opacity: 0.76 }
                    TextField {
                        id: accountUserField
                        Layout.fillWidth: true
                        placeholderText: qsTr("DOMAIN\\user, MACHINE\\user or user@domain")
                    }
                    Label { text: qsTr("Password:"); opacity: 0.76 }
                    TextField {
                        id: accountPasswordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        onAccepted: storeAccountButton.clicked()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Storing an existing account name replaces its user and password.")
                        wrapMode: Text.Wrap
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        opacity: 0.65
                    }
                    Button {
                        id: storeAccountButton
                        text: qsTr("Store account")
                        icon.name: "document-save"
                        icon.color: Kirigami.Theme.textColor
                        enabled: accountNameField.text.trim().length > 0 && accountUserField.text.trim().length > 0
                        onClicked: {
                            if (!enabled)
                                return
                            if (root.controller.storeAccount(accountNameField.text, accountUserField.text, accountPasswordField.text)) {
                                accountNameField.clear()
                                accountUserField.clear()
                            }
                            accountPasswordField.clear()
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }

                Label {
                    text: qsTr("Accounts to copy with on this computer, when the cluster's default rule names no account. An account set for an output or the default rule in step 3 comes first.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    opacity: 0.65
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        text: qsTr("Default account:")
                        opacity: 0.76
                    }
                    ComboBox {
                        id: defaultAccountCombo
                        Layout.fillWidth: true
                        property var choices: root.accountChoices(qsTr("Current Windows user"), root.controller.defaultAccount)
                        model: choices
                        currentIndex: root.controller.defaultAccount.length > 0 ? choices.indexOf(root.controller.defaultAccount) : 0
                        onActivated: function(index) {
                            root.controller.defaultAccount = index === 0 ? "" : choices[index]
                        }
                        ToolTip.text: qsTr("Used by every cluster without an account of its own below.")
                        ToolTip.visible: hovered
                    }
                }

                Repeater {
                    model: root.controller.presetNames
                    delegate: RowLayout {
                        id: clusterAccountRow
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        property string clusterName: modelData
                        property string account: root.controller.clusterAccounts[clusterName] || ""
                        property var choices: root.accountChoices(root.controller.defaultAccount.length > 0
                            ? qsTr("Default account (%1)").arg(root.controller.defaultAccount)
                            : qsTr("Default account (current Windows user)"), account)

                        Label {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            text: qsTr("Cluster %1:").arg(clusterAccountRow.clusterName)
                            elide: Text.ElideRight
                            opacity: 0.76
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: clusterAccountRow.choices
                            currentIndex: clusterAccountRow.account.length > 0 ? clusterAccountRow.choices.indexOf(clusterAccountRow.account) : 0
                            onActivated: function(index) {
                                root.controller.setClusterAccount(clusterAccountRow.clusterName, index === 0 ? "" : clusterAccountRow.choices[index])
                            }
                        }
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            root.storeFields()
            root.close()
        }
        onRejected: root.close()
    }
}
