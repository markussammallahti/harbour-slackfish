import QtQuick 2.1
import QtQuick.Layouts 1.1
import Sailfish.Silica 1.0

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

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.RichText
            font.pixelSize: Theme.fontSizeSmall
            text: richText(content)
            color: textColor
            visible: text.length > 0
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

                Label {
                    id: pretextField
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    textFormat: Text.RichText
                    text: richText(pretext)
                    visible: text.length > 0
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

                        /*
                        Label {
                            width: parent.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeSmall
                            textFormat: Text.RichText
                            text: richText(fallback)
                            visible: !(pretextField.visible || titleField.visible || contentField.visible)
                            onLinkActivated: handleLink(link)
                        }
                        */

                        Label {
                            id: titleField
                            width: parent.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.Bold
                            textFormat: Text.RichText
                            text: richText(title)
                            visible: text.length > 0
                            onLinkActivated: handleLink(link)
                        }

                        Label {
                            id: contentField
                            width: parent.width
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeSmall
                            textFormat: Text.RichText
                            text: richText(content)
                            visible: text.length > 0
                            onLinkActivated: handleLink(link)
                        }

                        GridLayout {
                            id: grid
                            columns: 2
                            width: parent.width

                            Repeater {
                                model: fields

                                Label {
                                    Layout.columnSpan: isShort ? 1 : 2
                                    Layout.preferredWidth: isShort ? grid.width / 2 : grid.width
                                    Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    font.weight: isTitle ? Font.Bold : Font.Normal
                                    wrapMode: Text.Wrap
                                    textFormat: Text.RichText
                                    text: richText(content)
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: images

                    Image {
                        width: parent.width
                        //height: size.height
                        fillMode: Image.PreserveAspectFit
                        source: url
                        sourceSize.width: size.width
                        sourceSize.height: size.height
                    }
                }
            }
        }
    }

    function richText(content) {
        return content.length > 0 ? "<style>a:link { color: " + Theme.highlightColor + "; }</style>" + content : ""
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
