import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.slackfish 1.0 as Slack

SilicaListView {
    property alias atBottom: listView.atYEnd
    property variant channel

    property bool appActive: Qt.application.state === Qt.ApplicationActive
    property bool inputEnabled: false
    property bool hasMoreMessages: false
    property bool loading: false
    property bool canLoadMore: hasMoreMessages && !loading
    property string latestRead: ""

    signal loadCompleted()
    signal loadStarted()

    id: listView
    anchors.fill: parent
    spacing: Theme.paddingLarge
    currentIndex: -1

    BusyIndicator {
        visible: loading && hasMoreMessages
        running: visible
        size: BusyIndicatorSize.Medium
        anchors.topMargin: Theme.paddingLarge
        anchors.top: listView.top
        anchors.horizontalCenter: listView.horizontalCenter
    }

    VerticalScrollDecorator {}

    Timer {
        id: readTimer
        interval: 5000
        triggeredOnStart: false
        running: false
        repeat: false
        onTriggered: {
            markLatest()
        }
    }

    WorkerScript {
        id: loader
        source: "MessageLoader.js"

        onMessage: {
            if (messageObject.op === 'replace') {
                listView.positionViewAtEnd()
                inputEnabled = true
                loading = false
                loadCompleted()

                if (messageListModel.count) {
                    latestRead = messageListModel.get(messageListModel.count - 1).timestamp
                    readTimer.restart()
                }
            }
            else if (messageObject.op === 'prepend') {
                loading = false
            }
        }
    }

    header: PageHeader {
        title: channel.name
    }

    model: ListModel {
        id: messageListModel
    }

    delegate: MessageListItem {}

    section {
        property: "timegroup"
        criteria: ViewSection.FullString
        delegate: SectionHeader {
            text: section
        }
    }

    footer: MessageInput {
        visible: inputEnabled
        placeholder: qsTr("Message %1%2").arg("#").arg(channel.name)
        onSendMessage: {
            Slack.Client.postMessage(channel.id, content)
        }
    }

    onAppActiveChanged: {
        if (appActive && atBottom && messageListModel.count) {
            latestRead = messageListModel.get(messageListModel.count - 1).timestamp
            readTimer.restart()
        }
    }

    onContentYChanged: {
        var y = (contentY - originY) * (height / contentHeight)

        if (canLoadMore && y < Screen.height / 3) {
            loadHistory()
        }
    }

    onMovementEnded: {
        if (atBottom && messageListModel.count) {
            latestRead = messageListModel.get(messageListModel.count - 1).timestamp
            readTimer.restart()
        }
    }

    Component.onCompleted: {
        Slack.Client.onInitSuccess.connect(handleReload)
        Slack.Client.onLoadMessagesSuccess.connect(handleLoadSuccess)
        Slack.Client.onLoadHistorySuccess.connect(handleHistorySuccess)
        Slack.Client.onMessageReceived.connect(handleMessageReceived)
    }

    Component.onDestruction: {
        Slack.Client.onInitSuccess.disconnect(handleReload)
        Slack.Client.onLoadMessagesSuccess.disconnect(handleLoadSuccess)
        Slack.Client.onLoadHistorySuccess.disconnect(handleHistorySuccess)
        Slack.Client.onMessageReceived.disconnect(handleMessageReceived)
    }

    function markLatest() {
        if (latestRead != "") {
            Slack.Client.markChannel(channel.type, channel.id, latestRead)
            latestRead = ""
        }
    }

    function handleReload() {
        inputEnabled = false
        loadStarted()
        loadMessages()
    }

    function loadMessages() {
        loading = true
        Slack.Client.loadMessages(channel.type, channel.id)
    }

    function loadHistory() {
        if (messageListModel.count) {
            loading = true
            Slack.Client.loadHistory(channel.type, channel.id, messageListModel.get(0).timestamp)
        }
    }

    function handleLoadSuccess(channelId, messages, hasMore) {
        if (channelId === channel.id) {
            hasMoreMessages = hasMore
            loader.sendMessage({
                op: 'replace',
                model: messageListModel,
                messages: messages
            })
        }
    }

    function handleHistorySuccess(channelId, messages, hasMore) {
        if (channelId === channel.id) {
            hasMoreMessages = hasMore
            loader.sendMessage({
                op: 'prepend',
                model: messageListModel,
                messages: messages
            })
        }
    }

    function handleMessageReceived(message) {
        if (message.type === "message" && message.channel === channel.id) {
            var isAtBottom = atBottom
            messageListModel.append(message)

            if (isAtBottom) {
                listView.positionViewAtEnd()

                if (appActive) {
                    latestRead = message.timestamp
                    readTimer.restart()
                }
            }
        }
    }
}
