import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.slackfish 1.0 as Slack
import "../pages/Settings.js" as Settings

CoverBackground {
    property int unreadMessageCount: 0
    property color unreadColor: unreadMessageCount === 0 ? Theme.secondaryColor : Theme.primaryColor

    Label {
        id: title
        color: Theme.highlightColor
        width: parent.width - Theme.paddingLarge * 2
        text: "Slackfish"
        truncationMode: TruncationMode.Fade
        anchors {
            top: parent.top
            left: parent.left
            topMargin: Theme.paddingLarge
            leftMargin: Theme.paddingLarge
            rightMargin: Theme.paddingLarge
        }
    }

    Label {
        id: messageCount
        text: unreadMessageCount
        color: unreadColor
        font.pixelSize: Theme.fontSizeHuge
        anchors {
            top: title.bottom
            left: parent.left
            topMargin: Theme.paddingMedium
            leftMargin: Theme.paddingLarge
        }
    }

    Label {
        text: "Unread\nmessage" + (unreadMessageCount === 1 ? "" : "s")
        font.pixelSize: Theme.fontSizeTiny
        color: unreadColor
        anchors {
            left: messageCount.right
            leftMargin: Theme.paddingSmall
            verticalCenter: messageCount.verticalCenter
        }
    }

    Label {
        id: connectionMessage
        color: Theme.highlightColor
        text: ""
        visible: text.length > 0
        width: parent.width - Theme.paddingLarge * 2
        truncationMode: TruncationMode.Fade
        anchors {
            top: messageCount.bottom
            left: parent.left
            topMargin: Theme.paddingLarge
            leftMargin: Theme.paddingLarge
            rightMargin: Theme.paddingLarge
        }
    }

    CoverActionList {
        id: disconnectedActions
        enabled: false

        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: Slack.Client.reconnect()
        }
    }

    Component.onCompleted: {
        Slack.Client.onInitSuccess.connect(reloadChannelList)
        Slack.Client.onChannelUpdated.connect(reloadChannelList)
        Slack.Client.onConnected.connect(hideConnectionMessage)
        Slack.Client.onReconnecting.connect(showReconnectingMessage)
        Slack.Client.onDisconnected.connect(showDisconnectedMessage)
    }

    function hideConnectionMessage() {
        connectionMessage.text = ""
        disconnectedActions.enabled = false
    }

    function showReconnectingMessage() {
        connectionMessage.text = qsTr("Reconnecting")
        disconnectedActions.enabled = false
    }

    function showDisconnectedMessage() {
        connectionMessage.text = qsTr("Disconnected")
        disconnectedActions.enabled = true
    }

    function reloadChannelList() {
        title.text = Settings.getUserInfo().teamName
        unreadMessageCount = 0

        Slack.Client.getChannels().forEach(function(channel) {
            if (channel.isOpen && channel.unreadCount > 0) {
                unreadMessageCount += channel.unreadCount
            }
        })
    }
}
