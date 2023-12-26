// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import HelperWidgets as HelperWidgets
import StudioControls as StudioControls
import StudioTheme as StudioTheme
import EffectMakerBackend

Item {
    id: root

    property var draggedSec: null
    property var secsY: []
    property int moveFromIdx: 0
    property int moveToIdx: 0
    property bool previewAnimationRunning: false

    // Invoked after save changes is done
    property var onSaveChangesCallback: () => {}

    // Invoked from C++ side when open composition is requested and there are unsaved changes
    function promptToSaveBeforeOpen() {
        root.onSaveChangesCallback = () => { EffectMakerBackend.rootView.doOpenComposition() }

        saveChangesDialog.open()
    }

    Connections {
        target: EffectMakerBackend.effectMakerModel
        function onIsEmptyChanged() {
            if (EffectMakerBackend.effectMakerModel.isEmpty)
                saveAsDialog.close()
        }
    }

    SaveAsDialog {
        id: saveAsDialog
        anchors.centerIn: parent
    }

    SaveChangesDialog {
        id: saveChangesDialog
        anchors.centerIn: parent

        onSave: {
            if (EffectMakerBackend.effectMakerModel.currentComposition === "") {
                // if current composition is unsaved, show save as dialog and clear afterwards
                saveAsDialog.clearOnClose = true
                saveAsDialog.open()
            } else {
                root.onSaveChangesCallback()
            }
        }

        onDiscard: {
            root.onSaveChangesCallback()
        }
    }

    Column {
        id: col
        anchors.fill: parent
        spacing: 1

        EffectMakerTopBar {
            onAddClicked: {
                root.onSaveChangesCallback = () => { EffectMakerBackend.effectMakerModel.clear() }

                if (EffectMakerBackend.effectMakerModel.hasUnsavedChanges)
                    saveChangesDialog.open()
                else
                    EffectMakerBackend.effectMakerModel.clear()
            }

            onSaveClicked: {
                let name = EffectMakerBackend.effectMakerModel.currentComposition

                if (name === "")
                    saveAsDialog.open()
                else
                    EffectMakerBackend.effectMakerModel.saveComposition(name)
            }

            onSaveAsClicked: saveAsDialog.open()

            onAssignToSelectedClicked: {
                EffectMakerBackend.effectMakerModel.assignToSelected()
            }
        }

        EffectMakerPreview {
            mainRoot: root

            FrameAnimation {
                id: previewFrameTimer
                running: true
                paused: !previewAnimationRunning
            }
        }

        Rectangle {
            width: parent.width
            height: StudioTheme.Values.toolbarHeight
            color: StudioTheme.Values.themeToolbarBackground

            EffectNodesComboBox {
                mainRoot: root

                anchors.verticalCenter: parent.verticalCenter
                x: 5
                width: parent.width - 50
            }

            HelperWidgets.AbstractButton {
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter

                style: StudioTheme.Values.viewBarButtonStyle
                buttonIcon: StudioTheme.Constants.clearList_medium
                tooltip: qsTr("Remove all effect nodes.")
                enabled: !EffectMakerBackend.effectMakerModel.isEmpty

                onClicked: EffectMakerBackend.effectMakerModel.clear()
            }

            HelperWidgets.AbstractButton {
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter

                style: StudioTheme.Values.viewBarButtonStyle
                buttonIcon: StudioTheme.Constants.code
                tooltip: qsTr("Open Shader in Code Editor.")
                visible: false // TODO: to be implemented

                onClicked: {} // TODO
            }
        }

        Component.onCompleted: HelperWidgets.Controller.mainScrollView = scrollView

        HelperWidgets.ScrollView {
            id: scrollView

            width: parent.width
            height: parent.height - y
            clip: true

            Column {
                width: scrollView.width
                spacing: 1

                Repeater {
                    id: repeater

                    width: root.width
                    model: EffectMakerBackend.effectMakerModel

                    onCountChanged: {
                        HelperWidgets.Controller.setCount("EffectMaker", repeater.count)
                    }

                    delegate: EffectCompositionNode {
                        width: root.width
                        modelIndex: index

                        Behavior on y {
                            PropertyAnimation {
                                duration: 300
                                easing.type: Easing.InOutQuad
                            }
                        }

                        onStartDrag: (section) => {
                            root.draggedSec = section
                            root.moveFromIdx = index

                            highlightBorder = true

                            root.secsY = []
                            for (let i = 0; i < repeater.count; ++i)
                                root.secsY[i] = repeater.itemAt(i).y
                        }

                        onStopDrag: {
                            if (root.moveFromIdx === root.moveToIdx)
                                root.draggedSec.y = root.secsY[root.moveFromIdx]
                            else
                                EffectMakerBackend.effectMakerModel.moveNode(root.moveFromIdx, root.moveToIdx)

                            highlightBorder = false
                            root.draggedSec = null
                        }
                    }
                } // Repeater

                Timer {
                    running: root.draggedSec
                    interval: 50
                    repeat: true

                    onTriggered: {
                        root.moveToIdx = root.moveFromIdx
                        for (let i = 0; i < repeater.count; ++i) {
                            let currItem = repeater.itemAt(i)
                            if (i > root.moveFromIdx) {
                                if (root.draggedSec.y > currItem.y + (currItem.height - root.draggedSec.height) * .5) {
                                    currItem.y = root.secsY[i] - root.draggedSec.height
                                    root.moveToIdx = i
                                } else {
                                    currItem.y = root.secsY[i]
                                }
                            } else if (i < root.moveFromIdx) {
                                if (!repeater.model.isDependencyNode(i)
                                        && root.draggedSec.y < currItem.y + (currItem.height - root.draggedSec.height) * .5) {
                                    currItem.y = root.secsY[i] + root.draggedSec.height
                                    root.moveToIdx = Math.min(root.moveToIdx, i)
                                } else {
                                    currItem.y = root.secsY[i]
                                }
                            }
                        }
                    }
                } // Timer
            } // Column
        } // ScrollView
    }

    Text {
        id: emptyText

        text: qsTr("Add an effect node to start")
        color: StudioTheme.Values.themeTextColor
        font.pixelSize: StudioTheme.Values.baseFontSize

        x: scrollView.x + (scrollView.width - emptyText.width) * .5
        y: scrollView.y + scrollView.height * .5

        visible: EffectMakerBackend.effectMakerModel.isEmpty
    }
}
