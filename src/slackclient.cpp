#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QThread>
#include <QSize>
#include <QStringList>
#include <QRegularExpression>
#include <QtNetwork/QNetworkConfigurationManager>

#include "slackclient.h"

SlackClient::SlackClient(QObject *parent) : QObject(parent), lastMessageId(0), isConnected(false), networkAccessible(QNetworkAccessManager::Accessible) {
    networkAccessManager = new QNetworkAccessManager(this);
    config = new SlackConfig(this);
    checkTimer = new QTimer(this);
    reconnectTimer = new QTimer(this);
    webSocket = new QtWebsocket::QWsSocket(this);
    networkAccessible = networkAccessManager->networkAccessible();

    connect(networkAccessManager, SIGNAL(networkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility)), this, SLOT(handleNetworkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility)));
    connect(checkTimer, SIGNAL(timeout()), this, SLOT(checkConnection()));
    connect(reconnectTimer, SIGNAL(timeout()), this, SLOT(reconnect()));
    connect(webSocket, SIGNAL(connected()), this, SLOT(handleListerStart()));
    connect(webSocket, SIGNAL(disconnected()), this, SLOT(handleListerEnd()));
    connect(webSocket, SIGNAL(frameReceived(QString)), this, SLOT(handleMessage(QString)));
}

SlackClient::~SlackClient() {
    disconnect(webSocket, SIGNAL(disconnected()), this, SLOT(handleListerEnd()));

    if (!webSocket.isNull()) {
        webSocket->disconnectFromHost();
    }
}

void SlackClient::handleNetworkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility accessible) {
    qDebug() << "Network accessible changed" << accessible;
    networkAccessible = accessible;
}

void SlackClient::checkConnection() {
    if (isConnected) {
        QString type("ping");
        QJsonObject values;
        values.insert("id", QJsonValue(++lastMessageId));
        values.insert("type", QJsonValue(type));

        QJsonDocument document(values);
        QByteArray data = document.toJson(QJsonDocument::Compact);
        QString payload(data);

        qDebug() << "Check connection" << lastMessageId;
        webSocket->write(payload);
    }
}

void SlackClient::reconnect() {
    qDebug() << "Reconnecting";
    emit reconnecting();
    start();
}

void SlackClient::handleListerStart() {
    qDebug() << "Connected";
    emit connected();
    isConnected = true;
    checkTimer->start(20000);
}

void SlackClient::handleListerEnd() {
    qDebug() << "Disconnected";
    emit reconnecting();
    checkTimer->stop();
    isConnected = false;
    lastMessageId = 0;
    reconnectTimer->setSingleShot(true);
    reconnectTimer->start(1000);
}

void SlackClient::handleMessage(QString message) {
    qDebug() << "Got message" << message;

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qDebug() << "Failed to parse message" << message;
        return;
    }

    QJsonObject messageObject = document.object();
    QString type = messageObject.value("type").toString();

    if (type == "message") {
        parseMessageUpdate(messageObject);
    }
    else if (type == "group_marked" || type == "channel_marked" || type == "im_marked" || type == "mpim_marked") {
        parseChannelUpdate(messageObject);
    }
}

void SlackClient::parseChannelUpdate(QJsonObject message) {
    qDebug() << "Channel unread counts" << message.value("unread_count").toInt() << message.value("unread_count_display").toInt();

    QString id = message.value("channel").toString();
    QVariantMap channel = channels.value(id).toMap();
    channel.insert("unreadCount", message.value("unread_count_display").toVariant());
    channels.insert(id, channel);

    emit channelUpdated(channel);
}

void SlackClient::parseMessageUpdate(QJsonObject message) {
    QVariantMap data = getMessageData(message);

    QString channelId = message.value("channel").toString();
    if (channelMessages.contains(channelId)) {
        QVariantList messages = channelMessages.value(channelId).toList();
        messages.append(data);
        channelMessages.insert(channelId, messages);
    }

    QVariantMap channel = channels.value(channelId).toMap();
    int unreadCount = channel.value("unreadCount").toInt() + 1;
    channel.insert("unreadCount", unreadCount);
    channels.insert(channelId, channel);

    emit channelUpdated(channel);
    emit messageReceived(data);
}

bool SlackClient::isOk(const QNetworkReply *reply) {
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (statusCode / 100 == 2) {
        return true;
    }
    else {
        return false;
    }
}

bool SlackClient::isError(const QJsonObject &data) {
    if (data.isEmpty()) {
        return true;
    }
    else {
        return !data.value("ok").toBool(false);
    }
}

QJsonObject SlackClient::getResult(QNetworkReply *reply) {
    if (isOk(reply)) {
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);

        if (error.error == QJsonParseError::NoError) {
            return document.object();
        }
        else {
            return QJsonObject();
        }
    }
    else {
        return QJsonObject();
    }
}


/**** Access token ****/
void SlackClient::fetchAccessToken(QUrl resultUrl) {
    QUrlQuery resultQuery(resultUrl);
    QString code = resultQuery.queryItemValue("code");

    if (code.isEmpty()) {
        emit accessTokenFail();
        return;
    }

    QUrlQuery query;
    query.addQueryItem("client_id", "19437480772.45165537666");
    query.addQueryItem("client_secret", "d5d28d904bd38bdb662a18638fe54a7e");
    query.addQueryItem("code", code);

    QUrl url("https://slack.com/api/oauth.access");
    url.setQuery(query);
    QNetworkRequest request(url);

    accessTokenReply = networkAccessManager->get(request);
    connect(accessTokenReply, SIGNAL(finished()), this, SLOT(handleAccessTokenReply()));
}

void SlackClient::handleAccessTokenReply() {
    if (accessTokenReply.isNull()) {
        return;
    }

    QJsonObject data = getResult(accessTokenReply);
    qDebug() << "Access token result" << data;

    if (isError(data)) {
        accessTokenReply->deleteLater();
        emit accessTokenFail();
        return;
    }

    QString accessToken = data.value("access_token").toString();
    QString teamId = data.value("team_id").toString();
    QString userId = data.value("user_id").toString();
    QString teamName = data.value("team_name").toString();
    qDebug() << "Access token success" << accessToken << userId << teamId << teamName;

    config->setAccessToken(accessToken);
    config->setUserId(userId);

    emit accessTokenSuccess(userId, teamId, teamName);

    accessTokenReply->deleteLater();
}


/**** Test login ****/
void SlackClient::testLogin() {
    qDebug() << "Network status" << networkAccessible;

    if (networkAccessible != QNetworkAccessManager::Accessible) {
        emit testConnectionFail();
        return;
    }

    QString token = config->accessToken();
    if (token.isEmpty()) {
        emit testLoginFail();
        return;
    }

    QUrlQuery query;
    query.addQueryItem("token", token);
    qDebug() << "Token" << token;

    QUrl url("https://slack.com/api/auth.test");
    url.setQuery(query);
    QNetworkRequest request(url);

    QThread::msleep(200);
    testLoginReply = networkAccessManager->get(request);
    connect(testLoginReply, SIGNAL(finished()), this, SLOT(handleTestLoginReply()));
}

void SlackClient::handleTestLoginReply() {
    if (testLoginReply.isNull()) {
        return;
    }

    QJsonObject data = getResult(testLoginReply);
    qDebug() << "Test login result" << data;

    if (isError(data)) {
        testLoginReply->deleteLater();
        emit testLoginFail();
        return;
    }

    QString teamId = data.value("team_id").toString();
    QString userId = data.value("user_id").toString();
    QString teamName = data.value("team").toString();
    qDebug() << "Login success" << userId << teamId << teamName;

    config->setUserId(userId);

    emit testLoginSuccess(userId, teamId, teamName);
    testLoginReply->deleteLater();
}


/**** Start ****/
void SlackClient::start() {
    qDebug() << "Start init";
    QString token = config->accessToken();
    if (token.isEmpty()) {
        qDebug() << "No access token" << "init fail";
        emit disconnected();
        emit initFail();
        return;
    }

    QUrlQuery query;
    query.addQueryItem("token", token);

    QUrl url("https://slack.com/api/rtm.start");
    url.setQuery(query);
    QNetworkRequest request(url);
    qDebug() << "Start" << url;

    QThread::msleep(200);
    startReply = networkAccessManager->get(request);
    connect(startReply, SIGNAL(finished()), this, SLOT(handleStartReply()));
}

void SlackClient::handleStartReply() {
    if (startReply.isNull()) {
        qDebug() << "reply null" << "start fail";
        emit disconnected();
        emit initFail();
        return;
    }

    QJsonObject data = getResult(startReply);

    if (isError(data)) {
        qDebug() << "Start result error";
        startReply->deleteLater();
        emit disconnected();
        emit initFail();
        return;
    }

    QJsonArray userList = data.value("users").toArray();
    foreach (const QJsonValue &value, userList) {
        QJsonObject user = value.toObject();

        QVariantMap data;
        data.insert("id", user.value("id").toVariant());
        data.insert("name", user.value("name").toVariant());

        users.insert(data.value("id").toString(), data);
    }

    QJsonArray botList = data.value("bots").toArray();
    foreach (const QJsonValue &value, botList) {
        QJsonObject bot = value.toObject();
        QVariantMap data;
        data.insert("id", bot.value("id"));
        data.insert("name", bot.value("name"));
        users.insert(data.value("id").toString(), data);
    }

    QJsonArray channelList = data.value("channels").toArray();
    foreach (const QJsonValue &value, channelList) {
        QJsonObject channel = value.toObject();

        QVariantMap data;
        data.insert("type", QVariant("channel"));
        data.insert("category", QVariant("channel"));
        data.insert("id", channel.value("id").toVariant());
        data.insert("name", channel.value("name").toVariant());
        data.insert("isMember", channel.value("is_member").toVariant());
        data.insert("isOpen", channel.value("is_member").toVariant());
        data.insert("unreadCount", channel.value("unread_count_display").toVariant());

        channels.insert(data.value("id").toString(), data);
    }

    QJsonArray groupList = data.value("groups").toArray();
    foreach (const QJsonValue &value, groupList) {
        QJsonObject group = value.toObject();

        QVariantMap data;

        if (group.value("is_mpim").toBool()) {
            data.insert("type", QVariant("mpim"));
            data.insert("category", QVariant("chat"));

            QStringList members;
            QJsonArray memberList = group.value("members").toArray();
            foreach (const QJsonValue &member, memberList) {
                QString memberId = member.toString();

                if (memberId != config->userId()) {
                    members << users.value(memberId).toMap().value("name").toString();
                }
            }
            data.insert("name", QVariant(members.join(", ")));
        }
        else {
            data.insert("type", QVariant("group"));
            data.insert("category", QVariant("channel"));
            data.insert("name", group.value("name").toVariant());
        }

        data.insert("id", group.value("id").toVariant());
        data.insert("isOpen", group.value("is_open").toVariant());
        data.insert("unreadCount", group.value("unread_count_display").toVariant());
        channels.insert(data.value("id").toString(), data);
    }

    QJsonArray chatList = data.value("ims").toArray();
    foreach (const QJsonValue &value, chatList) {
        QJsonObject chat = value.toObject();
        QVariantMap data;

        QString userId = chat.value("user").toString();
        QVariantMap user = users.value(userId).toMap();

        data.insert("type", QVariant("im"));
        data.insert("category", QVariant("chat"));
        data.insert("id", chat.value("id").toVariant());
        data.insert("name", user.value("name"));
        data.insert("isOpen", chat.value("is_open").toVariant());
        data.insert("unreadCount", chat.value("unread_count_display").toVariant());
        channels.insert(data.value("id").toString(), data);
    }

    QThread::msleep(200);
    QUrl url(data.value("url").toString());

    QString socketUrl = url.scheme() + "://" + url.host();
    webSocket->setResourceName(url.path());
    webSocket->connectToHost(socketUrl);

    channelMessages.clear();
    emit initSuccess();

    startReply->deleteLater();
}

QVariantList SlackClient::getChannels() {
    return channels.values();
}


/**** Load messages ****/
QUrl SlackClient::historyUrl(QString type) {
    if (type == "channel") {
        return QUrl("https://slack.com/api/channels.history");
    }
    else if (type == "group") {
        return QUrl("https://slack.com/api/groups.history");
    }
    else if (type == "mpim") {
        return QUrl("https://slack.com/api/mpim.history");
    }
    else if (type == "im") {
        return QUrl("https://slack.com/api/im.history");
    }
    else {
        return QUrl();
    }
}

void SlackClient::loadMessages(QString type, QString channelId) {
    qDebug() << "Start load messages" << channelId;

    if (channelMessages.contains(channelId)) {
        QVariantList messages = channelMessages.value(channelId).toList();
        emit loadMessagesSuccess(channelId, messages);
        return;
    }

    QString token = config->accessToken();
    if (token.isEmpty()) {
        qDebug() << "No access token" << "load messages fail";
        emit loadMessagesFail();
        return;
    }

    QUrlQuery query;
    query.addQueryItem("token", token);
    query.addQueryItem("channel", channelId);

    QUrl url = historyUrl(type);
    url.setQuery(query);
    QNetworkRequest request(url);
    qDebug() << "Load messages" << url;

    QThread::msleep(200);
    loadMessagesReply = networkAccessManager->get(request);
    loadMessagesReply->setProperty("channelId", channelId);
    connect(loadMessagesReply, SIGNAL(finished()), this, SLOT(handleLoadMessagesReply()));
}

void SlackClient::handleLoadMessagesReply() {
    if (loadMessagesReply.isNull()) {
        qDebug() << "reply null" << "load messages fail";
        return;
    }

    QJsonObject data = getResult(loadMessagesReply);

    if (isError(data)) {
        loadMessagesReply->deleteLater();
        emit loadMessagesFail();
        return;
    }

    QJsonArray messageList = data.value("messages").toArray();
    QVariantList messages;

    foreach (const QJsonValue &value, messageList) {
        QJsonObject message = value.toObject();
        messages << getMessageData(message);
    }

    QString channelId = loadMessagesReply->property("channelId").toString();
    channelMessages.insert(channelId, messages);

    emit loadMessagesSuccess(channelId, messages);

    loadMessagesReply->deleteLater();
}

/* Mark channel */
QUrl SlackClient::markUrl(QString type) {
    if (type == "channel") {
        return QUrl("https://slack.com/api/channels.mark");
    }
    else if (type == "group") {
        return QUrl("https://slack.com/api/groups.mark");
    }
    else if (type == "mpim") {
        return QUrl("https://slack.com/api/mpim.mark");
    }
    else if (type == "im") {
        return QUrl("https://slack.com/api/im.mark");
    }
    else {
        return QUrl();
    }
}

void SlackClient::markChannel(QString type, QString channelId, QString time) {
    QString token = config->accessToken();
    if (token.isEmpty()) {
        qDebug() << "No access token" << "mark channel fail";
        return;
    }

    QUrlQuery query;
    query.addQueryItem("token", token);
    query.addQueryItem("channel", channelId);
    query.addQueryItem("ts", time);

    QUrl url = markUrl(type);
    url.setQuery(query);

    QNetworkRequest request(url);
    qDebug() << "Mark channel" << url;

    markChannelReply = networkAccessManager->get(request);
    connect(markChannelReply, SIGNAL(finished()), this, SLOT(handleMarkChannelReply()));
}

void SlackClient::handleMarkChannelReply() {
    if (markChannelReply.isNull()) {
        qDebug() << "reply null" << "mark messages fail";
        return;
    }

    QJsonObject data = getResult(markChannelReply);
    qDebug() << "Mark message result" << data;

    markChannelReply->deleteLater();
}

void SlackClient::postMessage(QString channelId, QString content) {
    QString token = config->accessToken();
    if (token.isEmpty()) {
        qDebug() << "No access token" << "post message fail";
        return;
    }

    content.replace(QRegularExpression("&"), "&amp;");
    content.replace(QRegularExpression(">"), "&gt;");
    content.replace(QRegularExpression("<"), "&lt;");

    QUrlQuery query;
    query.addQueryItem("token", token);
    query.addQueryItem("channel", channelId);
    query.addQueryItem("text", content);
    query.addQueryItem("as_user", "true");
    query.addQueryItem("parse", "full");

    QUrl params;
    params.setQuery(query);
    QByteArray data = params.toEncoded(QUrl::EncodeUnicode | QUrl::EncodeReserved); // | QUrl::EncodeDelimiters | QUrl::EncodeSpaces
    data.remove(0,1);

    qDebug() << "Message:" << data;
    QUrl url("https://slack.com/api/chat.postMessage");
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());

    postMessageReply = networkAccessManager->post(request, data);
    connect(postMessageReply, SIGNAL(finished()), this, SLOT(handlePostMessageReply()));
}

void SlackClient::handlePostMessageReply() {
    if (postMessageReply.isNull()) {
        qDebug() << "reply null" << "post messages fail";
        return;
    }

    QJsonObject data = getResult(postMessageReply);
    qDebug() << "Post message result" << data;

    postMessageReply->deleteLater();
}

QVariantMap SlackClient::getMessageData(const QJsonObject message) {
    QVariantMap data;
    data.insert("type", message.value("type").toVariant());
    data.insert("time", message.value("ts").toVariant());
    data.insert("channel", message.value("channel").toVariant());
    data.insert("user", user(message));
    data.insert("attachments", getAttachments(message));

    QString content = getContent(message);
    data.insert("content", QVariant(content));

    QVariantList images;
    QString subtype = message.value("subtype").toString();
    if (subtype == "file_share") {
        QStringList imageTypes;
        imageTypes << "jpg";
        imageTypes << "png";
        imageTypes << "gif";

        QJsonObject file = message.value("file").toObject();
        QString fileType = file.value("filetype").toString();

        if (imageTypes.contains(fileType)) {
            QString thumbItem = "360";

            if (file.contains("thumb_480")) {
                thumbItem = "480";
            }

            QVariant thumbUrl = file.value("thumb_" + thumbItem).toVariant();
            QVariantMap thumbSize;
            thumbSize.insert("width", file.value("thumb_" + thumbItem + "_w").toVariant());
            thumbSize.insert("height", file.value("thumb_" + thumbItem + "_h").toVariant());

            QVariant name = file.value("name").toVariant();
            QVariant url = file.value("url_private").toVariant();
            QVariantMap imageSize;
            imageSize.insert("width", file.value("original_w").toVariant());
            imageSize.insert("height", file.value("original_h").toVariant());

            QVariantMap fileData;
            fileData.insert("name", name);
            fileData.insert("url", url);
            fileData.insert("size", imageSize);
            fileData.insert("thumbSize", thumbSize);
            fileData.insert("thumbUrl", thumbUrl);

            images.append(fileData);
        }
    }
    data.insert("images", images);

    return data;
}

QVariantMap SlackClient::user(const QJsonObject &data) {
    QString type = data.value("subtype").toString("default");
    QString userId;

    if (type == "bot_message") {
        userId = data.value("bot_id").toString();
    }
    else {
        userId = data.value("user").toString();
    }

    QVariantMap userData;

    if (users.contains(userId)) {
        userData = users.value(userId).toMap();

    }
    else {
        userData.insert("id", data.value("user").toVariant());
        userData.insert("name", QVariant("Unknown"));
    }

    QString username = data.value("username").toString();
    if (!username.isEmpty()) {
        QRegularExpression newUserPattern("<@([A-Z0-9]+)\\|([^>]+)>");
        username.replace(newUserPattern, "\\2");
        userData.insert("name", username);
    }

    return userData;
}

QString SlackClient::getContent(QJsonObject message) {
    QString content = message.value("text").toString();

    findNewUsers(content);
    replaceUserInfo(content);
    replaceTargetInfo(content);
    replaceChannelInfo(content);
    replaceLinks(content);
    replaceSpecialCharacters(content);
    replaceMarkdown(content);
    replaceEmoji(content);

    return content;
}

QVariantList SlackClient::getAttachments(QJsonObject message) {
    QJsonArray attachementList = message.value("attachments").toArray();
    QVariantList attachments;

    foreach (const QJsonValue &value, attachementList) {
        QJsonObject attachment = value.toObject();
        QVariantMap data;

        QString titleLink = attachment.value("title_link").toString();
        QString title = attachment.value("title").toString();
        QString pretext = attachment.value("pretext").toString();
        QString text = attachment.value("text").toString();
        QString fallback = attachment.value("fallback").toString();
        QString color = attachment.value("color").toString();

        QVariantList fields;
        if (attachment.contains("fields")) {
            QJsonArray fieldList = attachment.value("fields").toArray();
            foreach (const QJsonValue &fieldValue, fieldList) {
                QJsonObject field = fieldValue.toObject();
                QString title = field.value("title").toString();
                QString value = field.value("value").toString();
                bool isShort = field.value("short").toBool();

                if (!title.isEmpty()) {
                    replaceLinks(title);
                    replaceMarkdown(title);

                    QVariantMap titleData;
                    titleData.insert("isTitle", QVariant(true));
                    titleData.insert("isShort", QVariant(isShort));
                    titleData.insert("content", QVariant(title));
                    fields.append(titleData);
                }

                if (!value.isEmpty()) {
                    replaceLinks(value);
                    replaceMarkdown(value);

                    QVariantMap valueData;
                    valueData.insert("isTitle", QVariant(false));
                    valueData.insert("isShort", QVariant(isShort));
                    valueData.insert("content", QVariant(value));
                    fields.append(valueData);
                }
            }
        }

        QVariantList images;
        if (attachment.contains("image_url")) {
            QVariantMap size;
            size.insert("width", attachment.value("image_width"));
            size.insert("height", attachment.value("image_height"));

            QVariantMap image;
            image.insert("url", attachment.value("image_url"));
            image.insert("size", size);

            images.append(image);
        }

        replaceLinks(pretext);
        replaceLinks(text);
        replaceLinks(fallback);
        replaceEmoji(text);

        int index = text.indexOf(' ', 250);
        if (index > 0) {
            text = text.left(index) + "...";
        }

        if (!title.isEmpty() && !titleLink.isEmpty()) {
            title = "<a href=\""+ titleLink + "\">" + title +"</a>";
        }

        if (color.isEmpty()) {
            color = "theme";
        }
        else if (color == "good") {
            color = "#6CC644";
        }
        else if (color == "warning") {
            color = "#E67E22";
        }
        else if (color == "danger") {
            color = "#D00000";
        }
        else if (!color.startsWith('#')) {
            color = "#" + color;
        }

        data.insert("title", QVariant(title));
        data.insert("pretext", QVariant(pretext));
        data.insert("content", QVariant(text));
        data.insert("fallback", QVariant(fallback));
        data.insert("indicatorColor", QVariant(color));
        data.insert("fields", QVariant(fields));
        data.insert("images", QVariant(images));

        attachments.append(data);
    }

    return attachments;
}

void SlackClient::findNewUsers(const QString &message) {
    QRegularExpression newUserPattern("<@([A-Z0-9]+)\\|([^>]+)>");

    QRegularExpressionMatchIterator i = newUserPattern.globalMatch(message);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString id = match.captured(1);

        if (!users.contains(id)) {
            QString name = match.captured(2);
            //qDebug() << "Found new user" << id << name;

            QVariantMap data;
            data.insert("id", QVariant(id));
            data.insert("name", QVariant(name));
            users.insert(id, data);
        }
    }
}

void SlackClient::replaceUserInfo(QString &message) {
    foreach (const QVariant &value, users) {
        QVariantMap user = value.toMap();
        QString id = user.value("id").toString();
        QString name = user.value("name").toString();

        QRegularExpression userIdPattern("<@" + id + "(\\|[^>]+)?>");
        QString displayName = "<a href=\"slackfish://user/"+ id +"\">@" + name + "</a>";

        message.replace(userIdPattern, displayName);
    }
}

void SlackClient::replaceChannelInfo(QString &message) {
    foreach (const QVariant &value, channels.values()) {
        QVariantMap channel = value.toMap();
        QString id = channel.value("id").toString();
        QString name = channel.value("name").toString();

        QRegularExpression channelIdPattern("<#" + id + "(\\|[^>]+)?>");
        QString displayName = "<a href=\"slackfish://channel/"+ id +"\">#" + name + "</a>";

        message.replace(channelIdPattern, displayName);
    }
}

void SlackClient::replaceLinks(QString &message) {
    QRegularExpression labelPattern("<(http[^\\|>]+)\\|([^>]+)>");
    message.replace(labelPattern, "<a href=\"\\1\">\\2</a>");

    QRegularExpression plainPattern("<(http[^>]+)>");
    message.replace(plainPattern, "<a href=\"\\1\">\\1</a>");

    QRegularExpression mailtoPattern("<(mailto:[^\\|>]+)\\|([^>]+)>");
    message.replace(mailtoPattern, "<a href=\"\\1\">\\2</a>");
}

void SlackClient::replaceMarkdown(QString &message) {
    QRegularExpression italicPattern("(^|\\s)_([^_]+)_(\\s|\\.|\\?|!|,|$)");
    message.replace(italicPattern, "\\1<i>\\2</i>\\3");

    QRegularExpression boldPattern("(^|\\s)\\*([^\\*]+)\\*(\\s|\\.|\\?|!|,|$)");
    message.replace(boldPattern, "\\1<b>\\2</b>\\3");

    QRegularExpression strikePattern("(^|\\s)~([^~]+)~(\\s|\\.|\\?|!|,|$)");
    message.replace(strikePattern, "\\1<s>\\2</s>\\3");

    QRegularExpression codePattern("(^|\\s)`([^`]+)`(\\s|\\.|\\?|!|,|$)");
    message.replace(codePattern, "\\1<code>\\2</code>\\3");

    QRegularExpression codeBlockPattern("```([^`]+)```");
    message.replace(codeBlockPattern, "<br/><code>\\1</code><br/>");

    QRegularExpression newLinePattern("\n");
    message.replace(newLinePattern, "<br/>");
}

void SlackClient::replaceEmoji(QString &message) {
    QMap<QString,QString> alternatives;
    alternatives.insert(":slightly_smiling_face:", ":grinning:");
    alternatives.insert(":slightly_frowning_face:", ":confused:");

    foreach (QString from, alternatives.keys()) {
        message.replace(from, alternatives.value(from));
    }

    QRegularExpression emojiPattern(":([\\w\\+\\-]+):(:[\\w\\+\\-]+:)?[\\?\\.!]?");
    message.replace(emojiPattern, "<img src=\"http://www.tortue.me/emoji/\\1.png\" alt=\"\\1\" align=\"bottom\" width=\"32\" height=\"32\" />");
}

void SlackClient::replaceTargetInfo(QString &message) {
    QRegularExpression variableLabelPattern("<!(here|channel|group|everyone)\\|([^>]+)>");
    message.replace(variableLabelPattern, "<a href=\"slackfish://target/\\1\">\\2</a>");

    QRegularExpression variablePattern("<!(here|channel|group|everyone)>");
    message.replace(variablePattern, "<a href=\"slackfish://target/\\1\">@\\1</a>");
}

void SlackClient::replaceSpecialCharacters(QString &message) {
    message.replace(QRegularExpression("&gt;"), ">");
    message.replace(QRegularExpression("&lt;"), "<");
    message.replace(QRegularExpression("&amp;"), "&");
}
