import QtQuick 2.1
import QtQuick.Layouts 1.1
import Sailfish.Silica 1.0
import "Content.js" as Content

ListItem {
    id: item
    enabled: false
    contentHeight: column.height + Theme.paddingMedium
    property color infoColor: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
    property color textColor: item.highlighted ? Theme.highlightColor : Theme.primaryColor

    Column {
        id: column
        width: parent.width - Theme.paddingLarge * (Screen.sizeCategory >= Screen.Large ? 4 : 2)
        anchors.verticalCenter: parent.verticalCenter
        x: Theme.paddingLarge * (Screen.sizeCategory >= Screen.Large ? 2 : 1)
        spacing: Theme.paddingSmall

        Item {
            width: parent.width
            height: childrenRect.height

            Label {
                text: user.name
                anchors.left: parent.left
                font.pixelSize: Theme.fontSizeTiny
                color: infoColor
            }

            Label {
                anchors.right: parent.right
                text: new Date(parseInt(time, 10) * 1000).toLocaleString(Qt.locale(), "H:mm")
                font.pixelSize: Theme.fontSizeTiny
                color: infoColor
            }
        }

        RichTextLabel {
            width: parent.width
            font.pixelSize: Theme.fontSizeSmall
            color: textColor
            visible: text.length > 0
            value: content
            onLinkActivated: handleLink(link)
        }

        Repeater {
            model: images

            Image {
                width: parent.width
                height: thumbSize.height
                fillMode: Image.PreserveAspectFit
                source: thumbUrl
                sourceSize.width: thumbSize.width
                sourceSize.height: thumbSize.height

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        pageStack.push(Qt.resolvedUrl("Image.qml"), {"model": model})
                    }
                }
            }
        }

        Repeater {
            model: attachments

            Column {
                width: column.width
                spacing: Theme.paddingMedium

                RichTextLabel {
                    width: parent.width
                    font.pixelSize: Theme.fontSizeSmall
                    visible: text.length > 0
                    value: pretext
                    onLinkActivated: handleLink(link)
                }

                Row {
                    width: parent.width
                    spacing: Theme.paddingMedium

                    Rectangle {
                        id: color
                        width: Theme.paddingSmall
                        height: parent.height
                        color: indicatorColor == "theme" ? Theme.highlightColor : indicatorColor
                    }

                    Column {
                        width: parent.width - color.width - Theme.paddingMedium
                        spacing: Theme.paddingMedium

                        RichTextLabel {
                            width: parent.width
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.Bold
                            value: title
                            visible: text.length > 0
                            onLinkActivated: handleLink(link)
                        }

                        RichTextLabel {
                            width: parent.width
                            font.pixelSize: Theme.fontSizeSmall
                            value: content
                            visible: text.length > 0
                            onLinkActivated: handleLink(link)
                        }

                        GridLayout {
                            id: grid
                            columns: 2
                            width: parent.width

                            Repeater {
                                model: fields

                                RichTextLabel {
                                    Layout.columnSpan: isShort ? 1 : 2
                                    Layout.preferredWidth: isShort ? grid.width / 2 : grid.width
                                    Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    font.weight: isTitle ? Font.Bold : Font.Normal
                                    value: content
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: images

                    Image {
                        width: parent.width
                        fillMode: Image.PreserveAspectFit
                        source: url
                        sourceSize.width: size.width
                        sourceSize.height: size.height
                    }
                }
            }
        }
    }

    function handleLink(link) {
        if (link.indexOf("slackfish://") === 0) {
            console.log("local link", link)
        } else {
            console.log("external link", link)
            Qt.openUrlExternally(link)
        }
    }
}
