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
import org.ctoolbox.cjob

Kirigami.ApplicationWindow {
    id: window

    // Server is provided as a context property from cjobapplication.cpp
    readonly property var server: cjobServer

    x: 64
    y: 256
    visible: true
    color: Kirigami.Theme.alternateBackgroundColor
    minimumWidth: 256
    minimumHeight: 256
    width: 512
    height: 700

    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Window

    function formatJobStatus(status) {
        switch (status) {
        case "Pending":
            return qsTr("Pending")
        case "Running":
            return qsTr("Running")
        case "Complete":
            return qsTr("Complete")
        default:
            return status
        }
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                text: qsTr("C-Job")
                level: 1
                Layout.fillWidth: true
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: Math.max(window.width - Kirigami.Units.largeSpacing * 4, 256)
                spacing: Kirigami.Units.largeSpacing

                SectionPane {
                    title: qsTr("Connected Instances")
                    iconName: "computer"
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: qsTr("%1 instance(s) connected").arg(server.instanceIds ? server.instanceIds.length : 0)
                            Layout.fillWidth: true
                            opacity: 0.76
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(200, (server.instanceIds || []).length * 30)
                            model: server.instanceIds || []
                            delegate: RowLayout {
                                width: parent ? parent.width : 0
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: "computer"
                                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                                    color: Kirigami.Theme.textColor
                                }
                                Label {
                                    text: modelData
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }

                SectionPane {
                    title: qsTr("Job Queue")
                    iconName: "kt-queue-manager"
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                         Label {
                             text: qsTr("%1 job(s) in queue").arg(server.jobIds ? server.jobIds.length : 0)
                             Layout.fillWidth: true
                             opacity: 0.76
                         }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.maximumHeight: 160
                                Layout.minimumHeight: 40
                                clip: true

                                ListView {
                                    id: jobQueueListView
                                    width: parent ? parent.width : 300
                                    model: jobQueueModel
                                    
                                    delegate: jobQueueItemDelegate
                                    spacing: Kirigami.Units.smallSpacing
                                    
                                    // Catch mouse release outside items to clean up stuck drags
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                        onContainsMouseChanged: {
                                            if (!containsMouse) {
                                                // Mouse left the ListView area - force cleanup of any active drag
                                                for (var i = 0; i < jobQueueListView.count; i++) {
                                                    var item = jobQueueListView.itemForIndex(i);
                                                    if (item && item.dragging) {
                                                        item.dragging = false;
                                                        item.z = 0;
                                                        item.opacity = 1.0;
                                                        if (item.insertionIndicator) {
                                                            item.insertionIndicator.destroy();
                                                            item.insertionIndicator = null;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    
                                    // Catch Escape key to cancel active drags
                                    Keys.onPressed: {
                                        if (event.key === Qt.Key_Escape) {
                                            for (var i = 0; i < jobQueueListView.count; i++) {
                                                var item = jobQueueListView.itemForIndex(i);
                                                if (item && item.dragging) {
                                                    event.accepted = true;
                                                    item.dragging = false;
                                                    item.z = 0;
                                                    item.opacity = 1.0;
                                                    if (item.insertionIndicator) {
                                                        item.insertionIndicator.destroy();
                                                        item.insertionIndicator = null;
                                                    }
                                                    item.y = item.originalY;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Component {
                                id: jobQueueItemDelegate
                                
                                 ItemDelegate {
                                     id: itemRoot
                                     width: parent ? parent.width : 300
                                     height: 48
                                     
                                     property bool dragging: false
                                     property real originalY: 0
                                     // transient insertion indicator instance (created on drag start)
                                     property var insertionIndicator: null
                                     
                                    contentItem: Column {
                                        anchors.fill: parent
                                        anchors.margins: Kirigami.Units.smallSpacing
                                        spacing: Kirigami.Units.extraSmallSpacing
                                        
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Kirigami.Units.smallSpacing
                                            

                                            Label {
                                                text: (index + 1) + "."
                                                font.pixelSize: Kirigami.Theme.smallFontSize
                                                font.bold: true
                                                Layout.preferredWidth: 24
                                                elide: Text.ElideRight
                                                horizontalAlignment: Text.AlignRight
                                            }
                                            

                                            Label {
                                                text: model.jobName || ""
                                                Layout.fillWidth: true
                                                elide: Text.ElideMiddle
                                                font.pixelSize: Kirigami.Theme.smallFontSize
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Kirigami.Units.smallSpacing

                                            Item {
                                                Layout.preferredWidth: 24
                                            }
                                        
                                            Label {
                                                text: (server.jobOwners && server.jobOwners[index]) || ""
                                                opacity: 0.55
                                                elide: Text.ElideRight
                                                font.pixelSize: Kirigami.Theme.extraSmallFontSize
                                                color: Kirigami.Theme.textColor
                                            }
                                        }
                                    }
                                    
                                    background: Rectangle {
                                        anchors.fill: parent
                                        color: {
                                            if (itemRoot.dragging) return Qt.alpha(Kirigami.Theme.highlightColor, 0.4);
                                            if (highlighted) return Qt.alpha(Kirigami.Theme.highlightColor, 0.6);
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
                                        color: hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.3) : "transparent"
                                        
                                        Kirigami.Icon {
                                            anchors.centerIn: parent
                                            source: "list-remove"
                                            color: Kirigami.Theme.textColor
                                            width: 16
                                            height: 16
                                            opacity: deleteArea.hovered ? 1.0 : 0.5
                                        }
                                        
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            hoverEnabled: true
                                            onClicked: {
                                                mouse.accepted = true
                                                server.removeJobFromQueueAt(index)
                                            }
                                        }
                                    }
                                    
                                    // Template for transient insertion indicator
                                    Component {
                                        id: insertionLineComp
                                        Rectangle {
                                            color: Kirigami.Theme.highlightColor
                                            height: 4
                                            width: parent ? parent.width : jobQueueListView.width
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
                                        drag.target: itemRoot
                                        drag.axis: Drag.YAxis
                                        
                                        onPressed: {
                                            itemRoot.dragging = true;
                                            itemRoot.originalY = itemRoot.y;
                                            itemRoot.z = 9999;
                                            itemRoot.opacity = 0.85;
                                            jobQueueListView.currentIndex = index;
                                            drag.minimumY = 0;
                                            drag.maximumY = Math.max(0, jobQueueListView.contentHeight);
                                            
                                            // create insertion indicator
                                            if (jobQueueListView && jobQueueListView.contentItem && !itemRoot.insertionIndicator) {
                                                itemRoot.insertionIndicator = insertionLineComp.createObject(jobQueueListView.contentItem, { x: 0, y: itemRoot.y + itemRoot.height/2 - 2, width: jobQueueListView.width });
                                            }
                                        }
                                        
                                        onPositionChanged: {
                                            var edgeThreshold = 20;
                                            if (itemRoot.y < jobQueueListView.contentY + edgeThreshold && jobQueueListView.contentY > 0) {
                                                jobQueueListView.contentY = Math.max(0, jobQueueListView.contentY - 8);
                                            }
                                            var bottomEdge = jobQueueListView.contentHeight - edgeThreshold - itemRoot.height;
                                            if (itemRoot.y > bottomEdge && (jobQueueListView.contentY < jobQueueListView.contentHeight - jobQueueListView.height)) {
                                                jobQueueListView.contentY = Math.min(jobQueueListView.contentHeight - jobQueueListView.height, jobQueueListView.contentY + 8);
                                            }
                                            
                                            // update insertion indicator position
                                            if (itemRoot.insertionIndicator) {
                                                var centerY = itemRoot.y + itemRoot.height / 2;
                                                var destIndex = Math.floor(centerY / itemRoot.height);
                                                if (destIndex < 0) destIndex = 0;
                                                if (destIndex > jobQueueListView.count - 1) destIndex = jobQueueListView.count - 1;
                                                
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
                                                var maxIndicatorY = jobQueueListView.contentHeight - itemRoot.insertionIndicator.height;
                                                if (indicatorY > maxIndicatorY) indicatorY = maxIndicatorY;
                                                
                                                itemRoot.insertionIndicator.y = indicatorY;
                                                itemRoot.insertionIndicator.width = jobQueueListView.width;
                                            }
                                        }
                                        
                                        onCanceled: {
                                            // cleanup transient indicator
                                            if (itemRoot.insertionIndicator) {
                                                itemRoot.insertionIndicator.destroy();
                                                itemRoot.insertionIndicator = null;
                                            }
                                            itemRoot.dragging = false;
                                            itemRoot.z = 0;
                                            itemRoot.opacity = 1.0;
                                            // restore original position
                                            itemRoot.y = itemRoot.originalY;
                                        }
                                        
                                        onReleased: {
                                            itemRoot.dragging = false;
                                            itemRoot.z = 0;
                                            itemRoot.opacity = 1.0;
                                            
                                            // Compute destination index based on delegate center and drag direction
                                            var centerY = itemRoot.y + itemRoot.height / 2;
                                            var destIndex = Math.floor(centerY / itemRoot.height);
                                            if (destIndex < 0) destIndex = 0;
                                            if (destIndex > jobQueueListView.count - 1) destIndex = jobQueueListView.count - 1;
                                            
                                            // If dragged down, insert AFTER destIndex; if up, insert BEFORE destIndex
                                            var finalDest = (itemRoot.y > itemRoot.originalY) ? destIndex : destIndex;
                                            
                                            // clamp finalDest to valid range
                                            if (finalDest < 0) finalDest = 0;
                                            if (finalDest > jobQueueListView.count - 1) finalDest = jobQueueListView.count - 1;
                                            
                                            if (index !== finalDest) {
                                                jobQueueModel.moveRow(index, finalDest);
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

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }

                SectionPane {
                    title: qsTr("Queue Status")
                    iconName: "media-playlist-repeat"
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Label { text: qsTr("Processing:"); opacity: 0.76 }
                            Label {
                                text: server.isProcessing ? qsTr("Yes - %1").arg(server.currentJobId) : qsTr("No")
                                opacity: server.isProcessing ? 1.0 : 0.65
                            }
                        }

                        RowLayout {
                            Label { text: qsTr("Queue Length:"); opacity: 0.76 }
                            Label {
                                text: String(server.jobIds ? server.jobIds.length - (server.isProcessing ? 1 : 0) : 0)
                                opacity: 0.65
                            }
                        }

                        RowLayout {
                            Label { text: qsTr("Queue Running:"); opacity: 0.76 }
                            Label {
                                text: server.queueRunning ? qsTr("Yes") : qsTr("No")
                                color: server.queueRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                opacity: 1.0
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: server.queueRunning ? qsTr("Pause Queue") : qsTr("Run Queue")
                                icon.name: server.queueRunning ? "media-playlist-shuffle" : "media-playback-start"
                                flat: false
                                enabled: !server.isProcessing || true  // Can toggle even while processing
                                onClicked: {
                                    if (server.queueRunning) {
                                        server.queueRunning = false
                                    } else {
                                        server.queueRunning = true
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }

                SectionPane {
                    title: qsTr("Server Status")
                    iconName: "network-server"
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Label { text: qsTr("Status:"); opacity: 0.76 }
                            Label {
                                text: server.isListening ? qsTr("Listening on %1").arg(server.serverName) : qsTr("Not listening")
                            }
                        }

                        RowLayout {
                            Label { text: qsTr("Server Name:"); opacity: 0.76 }
                            Label { text: server.serverName || qsTr("Not available") }
                        }

                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    }
                }

                SectionPane {
                    title: qsTr("Log")
                    iconName: "view-pim-notes"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: -30
                        Item { Layout.fillWidth: true }
                        Button {
                            onClicked: console.clear()
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
                            text: server.logText
                            readOnly: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: server
        function onJobAdded(instanceId, jobId) {
            console.log("Job added:", jobId, "from", instanceId)
        }
        function onJobRemoved(jobId) {
            console.log("Job removed:", jobId)
        }
        function onJobProgressUpdated(jobId, completed, total) {
            console.log("Job progress:", jobId, completed, "/", total)
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

            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("C-Job v%1").arg(app.version())
                opacity: 0.76
            }
        }
    }
}