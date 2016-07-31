import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.slackfish 1.0 as Slack

DockedPanel {
    id: connectionPanel
    width: parent.width
    height: content.height + Theme.paddingLarge

    dock: Dock.Bottom

    Column {
        id: content
        width: parent.width - Theme.paddingLarge * 2
        anchors.centerIn: parent
        spacing: Theme.paddingMedium

        Row {
            id: reconnectingMessage
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingSmall

            BusyIndicator {
                running: reconnectingMessage.visible
                anchors.verticalCenter: parent.verticalCenter
                size: BusyIndicatorSize.ExtraSmall
            }

            Label {
                text: qsTr("Reconnecting")
            }
        }

        Label {
            id: disconnectedMessage
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Disconnected")
        }

        Button {
            id: reconnectButton
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Reconnect")
            onClicked: {
                Slack.Client.reconnect()
            }
        }
    }

    Component.onCompleted: {
        Slack.Client.onConnected.connect(hideConnectionPanel)
        Slack.Client.onReconnecting.connect(showReconnectingMessage)
        Slack.Client.onDisconnected.connect(showDisconnectedMessage)
    }

    Component.onDestruction: {
        Slack.Client.onConnected.disconnect(hideConnectionPanel)
        Slack.Client.onReconnecting.disconnect(showReconnectingMessage)
        Slack.Client.onDisconnected.disconnect(showDisconnectedMessage)
    }

    function hideConnectionPanel() {
        connectionPanel.hide()
    }

    function showReconnectingMessage() {
        disconnectedMessage.visible = false
        reconnectButton.visible = false
        reconnectingMessage.visible = true
        connectionPanel.show()
    }

    function showDisconnectedMessage() {
        disconnectedMessage.visible = true
        reconnectButton.visible = true
        reconnectingMessage.visible = false
        connectionPanel.show()
    }
}
