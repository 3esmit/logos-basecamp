import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Logos.Theme
import Logos.Controls

Item {
    id: root
    objectName: "coreModuleInterface"

    property string pluginName: ""
    property var methods: []
    property var events: []
    property string resultText: ""

    signal backClicked()

    function argumentCount(signature) {
        const firstParenthesis = signature.indexOf("(")
        const lastParenthesis = signature.lastIndexOf(")")
        if (firstParenthesis < 0 || lastParenthesis <= firstParenthesis) {
            return -1
        }

        const parameters = signature.slice(firstParenthesis + 1, lastParenthesis).trim()
        return parameters.length === 0 ? 0 : parameters.split(",").length
    }

    function validateMethodArguments(argumentsJson, expectedArgumentCount) {
        let argumentsValue
        try {
            argumentsValue = JSON.parse(argumentsJson)
        } catch (error) {
            return {
                valid: false,
                error: qsTr("Arguments must be valid JSON.")
            }
        }

        if (!Array.isArray(argumentsValue)) {
            return {
                valid: false,
                error: qsTr("Arguments must be a JSON array.")
            }
        }

        if (expectedArgumentCount >= 0 && argumentsValue.length !== expectedArgumentCount) {
            return {
                valid: false,
                error: qsTr("Expected %1 argument(s).").arg(expectedArgumentCount)
            }
        }

        return {
            valid: true,
            argsJson: JSON.stringify(argumentsValue),
            error: ""
        }
    }

    Component.onCompleted: { loadMethods(); loadEvents() }
    onPluginNameChanged: { loadMethods(); loadEvents() }

    function loadMethods() {
        if (pluginName.length > 0) {
            let methodsJson = backend.getCoreModuleMethods(pluginName)
            try {
                methods = JSON.parse(methodsJson)
            } catch (e) {
                methods = []
            }
        } else {
            // Cleared/reset: don't leave a previous plugin's data on screen.
            methods = []
            resultText = ""
        }
    }

    function loadEvents() {
        if (pluginName.length > 0) {
            let eventsJson = backend.getCoreModuleEvents(pluginName)
            try {
                events = JSON.parse(eventsJson)
            } catch (e) {
                events = []
            }
        } else {
            events = []
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        // Header with back button
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Button {
                text: "← Back"
                
                contentItem: LogosText {
                    text: parent.text
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    implicitWidth: 80
                    implicitHeight: 32
                    color: parent.pressed ? "#3d3d3d" : "#4b4b4b"
                    radius: 4
                }

                onClicked: root.backClicked()
            }

            LogosText {
                text: "Interface: " + root.pluginName
                font.pixelSize: 20
                font.weight: Font.Bold
                color: "#ffffff"
            }

            Item { Layout.fillWidth: true }
        }

        // Methods list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2d2d2d"
            radius: 8
            border.color: "#3d3d3d"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                LogosScrollView {
                    id: interfaceScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ColumnLayout {
                        // Bind to the ScrollView's `availableWidth`, not
                        // `parent.width`: inside a Qt 6 ScrollView the latter
                        // resolves against the internal Flickable's contentItem,
                        // which can transiently collapse to 0 while a Repeater
                        // recreates delegates — snapping every `fillWidth` child
                        // to width 0 without always recovering. `availableWidth`
                        // is the stable public property (scrollbars included).
                        width: interfaceScroll.availableWidth
                        spacing: 8

                        // ── Methods ──
                        LogosText {
                            text: "Methods"
                            font.pixelSize: Theme.typography.secondaryText
                            font.weight: Font.Bold
                            color: "#a0a0a0"
                            visible: root.methods.length > 0
                        }

                        Repeater {
                            model: root.methods

                            Rectangle {
                                id: methodCard
                                Layout.fillWidth: true
                                Layout.preferredHeight: methodColumn.implicitHeight + 24
                                color: "#363636"
                                radius: 6

                                readonly property string methodName: modelData.name || modelData
                                readonly property int expectedArgumentCount:
                                    root.argumentCount(modelData.signature || "")
                                property string argumentError: ""

                                ColumnLayout {
                                    id: methodColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12

                                        LogosText {
                                            text: methodCard.methodName
                                            font.weight: Font.Bold
                                            color: "#4A90E2"
                                        }

                                        LogosText {
                                            text: modelData.signature || ""
                                            font.pixelSize: Theme.typography.secondaryText
                                            color: "#808080"
                                            visible: modelData.signature !== undefined
                                        }

                                        Item { Layout.fillWidth: true }
                                    }

                                    LogosText {
                                        text: modelData.description || ""
                                        font.pixelSize: Theme.typography.secondaryText
                                        color: "#a0a0a0"
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                        visible: modelData.description !== undefined && modelData.description.length > 0
                                    }

                                    LogosText {
                                        text: qsTr("Arguments (JSON array)")
                                        font.pixelSize: Theme.typography.secondaryText
                                        font.weight: Font.Bold
                                        color: "#a0a0a0"
                                        visible: methodCard.expectedArgumentCount !== 0
                                    }

                                    TextArea {
                                        id: argumentsInput
                                        objectName: "coreModuleArgumentInput." + methodCard.methodName
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 64
                                        visible: methodCard.expectedArgumentCount !== 0
                                        text: "[]"
                                        placeholderText: methodCard.expectedArgumentCount > 0
                                            ? qsTr("Enter a JSON array with %1 item(s).").arg(methodCard.expectedArgumentCount)
                                            : qsTr("[]")
                                        selectByMouse: true
                                        wrapMode: Text.Wrap
                                        color: "#ffffff"
                                        font.family: Theme.typography.publicSans
                                        font.pixelSize: Theme.typography.secondaryText

                                        background: Rectangle {
                                            color: "#1e1e1e"
                                            radius: 4
                                            border.color: "#4d4d4d"
                                            border.width: 1
                                        }

                                        onTextChanged: methodCard.argumentError = ""
                                    }

                                    LogosText {
                                        objectName: "coreModuleArgumentError." + methodCard.methodName
                                        Layout.fillWidth: true
                                        text: methodCard.argumentError
                                        font.pixelSize: Theme.typography.secondaryText
                                        color: "#e57373"
                                        wrapMode: Text.Wrap
                                        visible: methodCard.argumentError.length > 0
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Item { Layout.fillWidth: true }

                                        Button {
                                            objectName: "coreModuleCallButton." + methodCard.methodName
                                            text: qsTr("Call")

                                            contentItem: LogosText {
                                                text: parent.text
                                                font.pixelSize: Theme.typography.secondaryText
                                                color: "#ffffff"
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            background: Rectangle {
                                                implicitWidth: 60
                                                implicitHeight: 26
                                                color: parent.pressed ? "#45a049" : "#4CAF50"
                                                radius: 4
                                            }

                                            onClicked: {
                                                const validation = root.validateMethodArguments(
                                                    argumentsInput.text,
                                                    methodCard.expectedArgumentCount)
                                                if (!validation.valid) {
                                                    methodCard.argumentError = validation.error
                                                    return
                                                }

                                                methodCard.argumentError = ""
                                                root.resultText = backend.callCoreModuleMethod(
                                                    root.pluginName,
                                                    methodCard.methodName,
                                                    validation.argsJson)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Empty state
                        LogosText {
                            text: "No methods available for this plugin."
                            color: "#606060"
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 40
                            visible: root.methods.length === 0
                        }

                        // ── Events ──
                        LogosText {
                            text: "Events"
                            font.pixelSize: Theme.typography.secondaryText
                            font.weight: Font.Bold
                            color: "#a0a0a0"
                            Layout.topMargin: 12
                            visible: root.events.length > 0
                        }

                        Repeater {
                            model: root.events

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: eventColumn.implicitHeight + 24
                                color: "#363636"
                                radius: 6

                                ColumnLayout {
                                    id: eventColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12

                                        LogosText {
                                            text: modelData.name || modelData
                                            font.weight: Font.Bold
                                            // Amber to distinguish events from the blue methods.
                                            color: "#E2A04A"
                                        }

                                        LogosText {
                                            text: modelData.signature || ""
                                            font.pixelSize: Theme.typography.secondaryText
                                            color: "#808080"
                                            visible: modelData.signature !== undefined
                                        }

                                        Item { Layout.fillWidth: true }
                                    }

                                    LogosText {
                                        text: modelData.description || ""
                                        font.pixelSize: Theme.typography.secondaryText
                                        color: "#a0a0a0"
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                        visible: modelData.description !== undefined && modelData.description.length > 0
                                    }
                                }
                            }
                        }
                    }
                }

                // Result area
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    color: "#1e1e1e"
                    radius: 4
                    border.color: "#4d4d4d"
                    border.width: 1
                    visible: root.resultText.length > 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        LogosText {
                            text: "Result:"
                            font.pixelSize: Theme.typography.secondaryText
                            font.weight: Font.Bold
                            color: "#a0a0a0"
                        }

                        LogosScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            TextArea {
                                text: root.resultText
                                font.pixelSize: 12
                                font.family: Theme.typography.publicSans
                                color: "#4CAF50"
                                readOnly: true
                                wrapMode: Text.Wrap
                                background: Rectangle {
                                    color: "transparent"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

