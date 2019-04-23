#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSize>
#include <QStringList>
#include <QRegularExpression>
#include <QFile>
#include <QHttpMultiPart>
#include <QtNetwork/QNetworkConfigurationManager>
#include <nemonotifications-qt5/notification.h>

#include "asyncfuture.h"

#include "slackclient.h"
#include "storage.h"
#include "messageformatter.h"

SlackClient::SlackClient(QObject *parent) : QObject(parent), appActive(true), activeWindow("init"), networkAccessible(QNetworkAccessManager::Accessible) {
    networkAccessManager = new QNetworkAccessManager(this);
    config = new SlackConfig(this);
    stream = new SlackStream(this);
    reconnectTimer = new QTimer(this);
    networkAccessible = networkAccessManager->networkAccessible();

    connect(networkAccessManager, SIGNAL(networkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility)), this, SLOT(handleNetworkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility)));
    connect(reconnectTimer, SIGNAL(timeout()), this, SLOT(reconnect()));

    connect(stream, SIGNAL(connected()), this, SLOT(handleStreamStart()));
    connect(stream, SIGNAL(disconnected()), this, SLOT(handleStreamEnd()));
    connect(stream, SIGNAL(messageReceived(QJsonObject)), this, SLOT(handleStreamMessage(QJsonObject)));
}

QString SlackClient::toString(const QJsonObject &data) {
    QJsonDocument doc(data);
    return doc.toJson(QJsonDocument::Compact);
}

void SlackClient::setAppActive(bool active) {
    appActive = active;
    clearNotifications();
}

void SlackClient::setActiveWindow(QString windowId) {
    activeWindow = windowId;
    clearNotifications();
}

void SlackClient::clearNotifications() {
  foreach (QObject* object, Notification::notifications()) {
      Notification* n = qobject_cast<Notification*>(object);
      if (n->hintValue("x-slackfish-channel").toString() == activeWindow) {
          n->close();
      }

      delete n;
  }
}

void SlackClient::handleNetworkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility accessible) {
    qDebug() << "Network accessible changed" << accessible;
    networkAccessible = accessible;

    if (networkAccessible == QNetworkAccessManager::Accessible) {
        emit networkOn();
    }
    else {
        emit networkOff();
    }
}

void SlackClient::reconnect() {
    qDebug() << "Reconnecting";
    emit reconnecting();
    start();
}

void SlackClient::handleStreamStart() {
    qDebug() << "Stream started";
    emit connected();

    QJsonArray userIds;
    foreach (const QVariant &value, Storage::users()) {
        QVariantMap user = value.toMap();
        if (user.value("name") != QVariant("slackbot")) {
            userIds.append(QJsonValue::fromVariant(user.value("id")));
        }
    }

    QJsonObject message;
    message.insert("type", QJsonValue(QString("presence_sub")));
    message.insert("ids", userIds);
    this->stream->send(message);
}

void SlackClient::handleStreamEnd() {
    qDebug() << "Stream ended";

    if (!config->accessToken().isEmpty()) {
        qDebug() << "Stream reconnect scheduled";
        emit reconnecting();
        reconnectTimer->setSingleShot(true);
        reconnectTimer->start(1000);
    }
}

void SlackClient::handleStreamMessage(QJsonObject message) {
    QString type = message.value("type").toString();

    if (type == "message") {
        parseMessageUpdate(message);
    }
    else if (type == "group_marked" || type == "channel_marked" || type == "im_marked" || type == "mpim_marked") {
        parseChannelUpdate(message);
    }
    else if (type == "channel_joined") {
        parseChannelJoin(message);
    }
    else if (type == "group_joined") {
        parseGroupJoin(message);
    }
    else if (type == "im_open") {
        parseChatOpen(message);
    }
    else if (type == "im_close") {
        parseChatClose(message);
    }
    else if (type == "channel_left" || type == "group_left") {
        parseChannelLeft(message);
    }
    else if (type == "presence_change") {
        parsePresenceChange(message);
    }
    else if (type == "desktop_notification") {
        parseNotification(message);
    }
}

void SlackClient::parseChatOpen(QJsonObject message) {
    QString id = message.value("channel").toString();
    QVariantMap channel = Storage::channel(id);
    channel.insert("isOpen", QVariant(true));
    Storage::saveChannel(channel);
    emit channelJoined(channel);
}

void SlackClient::parseChatClose(QJsonObject message) {
    QString id = message.value("channel").toString();
    QVariantMap channel = Storage::channel(id);
    channel.insert("isOpen", QVariant(false));
    Storage::saveChannel(channel);
    emit channelLeft(channel);
}

void SlackClient::parseChannelJoin(QJsonObject message) {
    QVariantMap data = parseChannel(message.value("channel").toObject());
    Storage::saveChannel(data);
    emit channelJoined(data);
}

void SlackClient::parseChannelLeft(QJsonObject message) {
    QString id = message.value("channel").toString();
    QVariantMap channel = Storage::channel(id);
    channel.insert("isOpen", QVariant(false));
    Storage::saveChannel(channel);
    emit channelLeft(channel);
}

void SlackClient::parseGroupJoin(QJsonObject message) {
    QVariantMap data = parseGroup(message.value("channel").toObject());
    Storage::saveChannel(data);
    emit channelJoined(data);
}

void SlackClient::parseChannelUpdate(QJsonObject message) {
    QString id = message.value("channel").toString();
    QVariantMap channel = Storage::channel(id);
    channel.insert("lastRead", message.value("ts").toVariant());
    channel.insert("unreadCount", message.value("unread_count_display").toVariant());
    Storage::saveChannel(channel);
    emit channelUpdated(channel);
}

void SlackClient::parseMessageUpdate(QJsonObject message) {
    QVariantMap data = getMessageData(message);

    QString channelId = message.value("channel").toString();
    if (Storage::channelMessagesExist(channelId)) {
        Storage::appendChannelMessage(channelId, data);
    }

    QVariantMap channel = Storage::channel(channelId);

    QString messageTime = data.value("timestamp").toString();
    QString latestRead = channel.value("lastRead").toString();

    if (messageTime > latestRead) {
        int unreadCount = channel.value("unreadCount").toInt() + 1;
        channel.insert("unreadCount", unreadCount);
        Storage::saveChannel(channel);
        emit channelUpdated(channel);
    }

    if (channel.value("isOpen").toBool() == false) {
        if (channel.value("type").toString() == "im") {
            openChat(channelId);
        }
    }

    emit messageReceived(data);
}

void SlackClient::parsePresenceChange(QJsonObject message) {
    QVariant presence = message.value("presence").toVariant();
    QVariantList userIds;
    if (message.contains("user")) {
        userIds << message.value("user").toVariant();
    }
    else {
        userIds = message.value("users").toArray().toVariantList();
    }

    foreach (QVariant userId, userIds) {
        QVariantMap user = Storage::user(userId);
        if (!user.isEmpty()) {
            user.insert("presence", presence);
            Storage::saveUser(user);
            emit userUpdated(user);
        }

        foreach (QVariant item, Storage::channels()) {
            QVariantMap channel = item.toMap();

            if (channel.value("type") == QVariant("im") && channel.value("userId") == userId) {
                channel.insert("presence", presence);
                Storage::saveChannel(channel);
                emit channelUpdated(channel);
            }
        }
    }
}

void SlackClient::parseNotification(QJsonObject message) {
  QString channel = message.value("subtitle").toString();
  QString content = message.value("content").toString();

  QString channelId = message.value("channel").toString();
  QString title;

  if (channelId.startsWith("C") || channelId.startsWith("G")) {
      title = QString(tr("New message in %1")).arg(channel);
  }
  else if (channelId.startsWith("D")) {
      title = QString(tr("New message from %1")).arg(channel);
  }
  else {
      title = QString(tr("New message"));
  }

  qDebug() << "App state" << appActive << activeWindow;

  if (!appActive || activeWindow != channelId) {
      sendNotification(channelId, title, content);
  }
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

QNetworkReply* SlackClient::executeGet(QString method, QMap<QString, QString> params) {
    QUrlQuery query;

    QString token = config->accessToken();
    if (!token.isEmpty()) {
        query.addQueryItem("token", token);
    }

    foreach (const QString key, params.keys()) {
        query.addQueryItem(key, params.value(key));
    }

    QUrl url("https://slack.com/api/" + method);
    url.setQuery(query);
    QNetworkRequest request(url);

    qDebug() << "GET" << url.toString();
    return networkAccessManager->get(request);
}

QNetworkReply* SlackClient::executePost(QString method, const QMap<QString, QString>& data) {
    QUrlQuery query;

    QString token = config->accessToken();
    if (!token.isEmpty()) {
        query.addQueryItem("token", token);
    }

    foreach (const QString key, data.keys()) {
        query.addQueryItem(key, data.value(key));
    }

    QUrl params;
    params.setQuery(query);
    QByteArray body = params.toEncoded(QUrl::EncodeUnicode | QUrl::EncodeReserved);
    body.remove(0,1);

    QUrl url("https://slack.com/api/" + method);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, body.length());

    qDebug() << "POST" << url.toString() << body;
    return networkAccessManager->post(request, body);
}

QNetworkReply* SlackClient::executePostWithFile(QString method, const QMap<QString, QString>& formdata, QFile* file) {
    QHttpMultiPart* dataParts = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart tokenPart;
    tokenPart.setHeader(QNetworkRequest::ContentDispositionHeader, "from-data; name=\"token\"");
    tokenPart.setBody(config->accessToken().toUtf8());
    dataParts->append(tokenPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, "from-data; name=\"file\"; filename=\"" + file->fileName() + "\"");
    filePart.setBodyDevice(file);
    dataParts->append(filePart);

    foreach (const QString key, formdata.keys()) {
        QHttpPart data;
        data.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"" + key.toUtf8() + "\"");
        data.setBody(formdata.value(key).toUtf8());
        dataParts->append(data);
    }

    QUrl url("https://slack.com/api/" + method);
    QNetworkRequest request(url);

    qDebug() << "POST" << url << dataParts;

    QNetworkReply* reply = networkAccessManager->post(request, dataParts);
    connect(reply, SIGNAL(finished()), dataParts, SLOT(deleteLater()));

    return reply;
}

void SlackClient::fetchAccessToken(QUrl resultUrl) {
    QUrlQuery resultQuery(resultUrl);
    QString code = resultQuery.queryItemValue("code");

    if (code.isEmpty()) {
        emit accessTokenFail();
        return;
    }

    QMap<QString,QString> params;
    params.insert("client_id", SLACK_CLIENT_ID);
    params.insert("client_secret", SLACK_CLIENT_SECRET);
    params.insert("code", code);

    QNetworkReply* reply = executeGet("oauth.access", params);
    connect(reply, SIGNAL(finished()), this, SLOT(handleAccessTokenReply()));
}

void SlackClient::handleAccessTokenReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    QJsonObject data = getResult(reply);

    if (isError(data)) {
        reply->deleteLater();
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

    reply->deleteLater();
}

void SlackClient::logout() {
    config->clearAccessToken();
    stream->disconnectFromHost();
    Storage::clear();
}

void SlackClient::testLogin() {
    if (networkAccessible != QNetworkAccessManager::Accessible) {
        qDebug() << "Login failed no network" << networkAccessible;
        emit testConnectionFail();
        return;
    }

    QString token = config->accessToken();
    if (token.isEmpty()) {
        emit testLoginFail();
        return;
    }

    QNetworkReply *reply = executeGet("auth.test");
    connect(reply, SIGNAL(finished()), this, SLOT(handleTestLoginReply()));
}

void SlackClient::handleTestLoginReply() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QJsonObject data = getResult(reply);

    if (isError(data)) {
        config->clearAccessToken();
        reply->deleteLater();
        emit testLoginFail();
        return;
    }

    QString teamId = data.value("team_id").toString();
    QString userId = data.value("user_id").toString();
    QString teamName = data.value("team").toString();
    qDebug() << "Login success" << userId << teamId << teamName;

    config->setUserId(userId);

    emit testLoginSuccess(userId, teamId, teamName);
    reply->deleteLater();
}

void SlackClient::init() {
  qDebug() << "Start init";
  loadUsers();
}

void SlackClient::loadUsers() {
  qDebug() << "Start load users";
  QNetworkReply* reply = executeGet("users.list");

  connect(reply, &QNetworkReply::finished, [reply,this]() {
    QJsonObject data = getResult(reply);
    if (isError(data)) {
      qDebug() << "User load failed";
      emit loadUsersFail();
    }
    else {
      qDebug() << "Load users completed";
      parseUsers(data);
      emit loadUsersSuccess();
      loadConversations();
    }

    reply->deleteLater();
  });
}

void SlackClient::start() {
    qDebug() << "Connect start";

    QMap<QString,QString> params;
    params.insert("batch_presence_aware", "1");
    QNetworkReply *reply = executeGet("rtm.connect", params);

    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Connect result error";
            emit disconnected();
            emit initFail();
        }
        else {
            QUrl url(data.value("url").toString());
            stream->listen(url);
            qDebug() << "Connect completed";

            Storage::clearChannelMessages();
            emit initSuccess();
        }

        reply->deleteLater();
    });
}

QVariantMap SlackClient::parseChannel(QJsonObject channel) {
    QVariantMap data;
    data.insert("id", channel.value("id").toVariant());
    data.insert("type", QVariant("channel"));
    data.insert("category", QVariant("channel"));
    data.insert("name", channel.value("name").toVariant());
    data.insert("presence", QVariant("none"));
    data.insert("isOpen", channel.value("is_member").toVariant());
    data.insert("lastRead", channel.value("last_read").toVariant());
    data.insert("unreadCount", channel.value("unread_count_display").toVariant());
    data.insert("userId", QVariant());
    return data;
}

QVariantMap SlackClient::parseGroup(QJsonObject group) {
    QVariantMap data;

    if (group.value("is_mpim").toBool()) {
        data.insert("type", QVariant("mpim"));
        data.insert("category", QVariant("chat"));

        QStringList members;
        QJsonArray memberList = group.value("members").toArray();
        foreach (const QJsonValue &member, memberList) {
            QVariant memberId = member.toVariant();

            if (memberId != config->userId()) {
                members << Storage::user(memberId).value("name").toString();
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
    data.insert("presence", QVariant("none"));
    data.insert("isOpen", group.value("is_open").toVariant());
    data.insert("lastRead", group.value("last_read").toVariant());
    data.insert("unreadCount", group.value("unread_count_display").toVariant());
    data.insert("userId", QVariant());
    return data;
}

QVariantMap SlackClient::parseChat(QJsonObject chat) {
  QVariantMap data;

  QVariant userId = chat.value("user").toVariant();
  QVariantMap user = Storage::user(userId);

  QString name;
  if (userId.toString() == config->userId()) {
    name = user.value("name").toString() + " (you)";
  }
  else {
    name = user.value("name").toString();
  }

  data.insert("type", QVariant("im"));
  data.insert("category", QVariant("chat"));
  data.insert("id", chat.value("id").toVariant());
  data.insert("userId", userId);
  data.insert("name", QVariant(name));
  data.insert("presence", user.value("presence"));
  data.insert("isOpen", chat.value("is_open").toVariant());
  data.insert("lastRead", chat.value("last_read").toVariant());
  data.insert("unreadCount", chat.value("unread_count_display").toVariant());

  return data;
}

void SlackClient::parseUsers(QJsonObject data) {
    foreach (const QJsonValue &value, data.value("members").toArray()) {
        QJsonObject user = value.toObject();
        QJsonObject profile = user.value("profile").toObject();
        QVariant presence;
        if (profile.value("always_active").toBool()) {
            presence = QVariant("active");
        }
        else {
            presence = QVariant("away");
        }

        QVariantMap data;
        data.insert("id", user.value("id").toVariant());
        if (profile.contains("display_name")) {
            data.insert("name", profile.value("display_name").toVariant());
        } else {
            data.insert("name", user.value("name").toVariant());
        }
        data.insert("presence", presence);
        Storage::saveUser(data);
    }
}

QVariantList SlackClient::getChannels() {
    return Storage::channels();
}

QVariant SlackClient::getChannel(QString channelId) {
    return Storage::channel(QVariant(channelId));
}

QString SlackClient::historyMethod(QString type) {
    if (type == "channel") {
        return "channels.history";
    }
    else if (type == "group") {
        return "groups.history";
    }
    else if (type == "mpim") {
        return "mpim.history";
    }
    else if (type == "im") {
        return "im.history";
    }
    else {
        return "";
    }
}

void SlackClient::loadConversations(QString cursor) {
  qDebug() << "Conversation load start" << cursor;

  QMap<QString,QString> params;
  params.insert("types", "public_channel,private_channel,mpim,im");
  params.insert("limit", "100");

  if (!cursor.isEmpty()) {
      params.insert("cursor", cursor);
  }

  QNetworkReply* reply = executeGet("conversations.list", params);
  connect(reply, &QNetworkReply::finished, [reply,this]() {
    QJsonObject data = getResult(reply);

    if (isError(data)) {
      qDebug() << "Conversation load failed";
    }
    else {
      auto combinator = AsyncFuture::combine();
      QString nextCursor = data.value("response_metadata").toObject().value("next_cursor").toString();

      foreach (const QJsonValue &value, data.value("channels").toArray()) {
        QJsonObject channel = value.toObject();

        QString infoMethod;
        if (channel.value("is_channel").toBool()) {
          infoMethod = "channels.info";
        }
        else if (channel.value("is_group").toBool()) {
          infoMethod = "groups.info";
        }
        else {
          infoMethod = "conversations.info";
        }

        QMap<QString,QString> params;
        params.insert("channel", channel.value("id").toString());
        QNetworkReply* infoReply = executeGet(infoMethod, params);

        combinator << AsyncFuture::observe(infoReply, &QNetworkReply::finished).future();
        connect(infoReply, &QNetworkReply::finished, [infoReply,infoMethod,this]() {
            QJsonObject infoData = getResult(infoReply).value(infoMethod == "groups.info" ? "group" : "channel").toObject();
            QVariantMap channel;

            if (infoData.value("is_im").toBool()) {
              channel = parseChat(infoData);
            }
            else if (infoData.value("is_channel").toBool()) {
              channel = parseChannel(infoData);
            }
            else {
              channel = parseGroup(infoData);
            }

            Storage::saveChannel(channel);
            infoReply->deleteLater();
        });
      }

      AsyncFuture::observe(combinator.future()).subscribe([nextCursor,this]() {
          if (nextCursor.isEmpty()) {
              start();
          }
          else {
              loadConversations(nextCursor);
          }
      });
    }

    reply->deleteLater();
  });
}

void SlackClient::joinChannel(QString channelId) {
    QVariantMap channel = Storage::channel(QVariant(channelId));

    QMap<QString,QString> params;
    params.insert("name", channel.value("name").toString());

    QNetworkReply* reply = executeGet("channels.join", params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Channel join failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::leaveChannel(QString channelId) {
    QMap<QString,QString> params;
    params.insert("channel", channelId);

    QNetworkReply* reply = executeGet("channels.leave", params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Channel leave failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::leaveGroup(QString groupId) {
    QMap<QString,QString> params;
    params.insert("channel", groupId);

    QNetworkReply* reply = executeGet("groups.leave", params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Group leave failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::openChat(QString chatId) {
    QVariantMap channel = Storage::channel(QVariant(chatId));

    QMap<QString,QString> params;
    params.insert("user", channel.value("userId").toString());

    QNetworkReply* reply = executeGet("im.open", params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Chat open failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::closeChat(QString chatId) {
    QMap<QString,QString> params;
    params.insert("channel", chatId);

    QNetworkReply* reply = executeGet("im.close", params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Chat close failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::loadHistory(QString type, QString channelId, QString latest) {
  QMap<QString,QString> params;
  params.insert("channel", channelId);
  params.insert("count", "20");
  params.insert("latest", latest);
  params.insert("inclusive", "0");

  QNetworkReply* reply = executeGet(historyMethod(type), params);
  connect(reply, &QNetworkReply::finished, [reply,channelId,this]() {
      QJsonObject data = getResult(reply);

      if (isError(data)) {
          reply->deleteLater();
          emit loadHistoryFail();
          return;
      }

      QVariantList messages = parseMessages(data);
      bool hasMore = data.value("has_more").toBool();
      Storage::prependChannelMessages(channelId, messages);

      emit loadHistorySuccess(channelId, messages, hasMore);
      reply->deleteLater();
  });
}

void SlackClient::loadMessages(QString type, QString channelId) {
    if (Storage::channelMessagesExist(channelId)) {
        QVariantList messages = Storage::channelMessages(channelId);
        emit loadMessagesSuccess(channelId, messages, true);
        return;
    }

    QMap<QString,QString> params;
    params.insert("channel", channelId);
    params.insert("count", "20");

    QNetworkReply* reply = executeGet(historyMethod(type), params);
    reply->setProperty("channelId", channelId);
    connect(reply, SIGNAL(finished()), this, SLOT(handleLoadMessagesReply()));
}

void SlackClient::handleLoadMessagesReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    QJsonObject data = getResult(reply);

    if (isError(data)) {
        reply->deleteLater();
        emit loadMessagesFail();
        return;
    }

    QVariantList messages = parseMessages(data);
    bool hasMore = data.value("has_more").toBool();
    QString channelId = reply->property("channelId").toString();
    Storage::setChannelMessages(channelId, messages);

    emit loadMessagesSuccess(channelId, messages, hasMore);
    reply->deleteLater();
}

QVariantList SlackClient::parseMessages(const QJsonObject data) {
    QJsonArray messageList = data.value("messages").toArray();
    QVariantList messages;

    foreach (const QJsonValue &value, messageList) {
        QJsonObject message = value.toObject();
        messages << getMessageData(message);
    }
    std::sort(messages.begin(), messages.end(), [](const QVariant &a, const QVariant &b) -> bool {
        return a.toMap().value("time").toDateTime() < b.toMap().value("time").toDateTime();
    });

    return messages;
}

QString SlackClient::markMethod(QString type) {
    if (type == "channel") {
        return "channels.mark";
    }
    else if (type == "group") {
        return "groups.mark";
    }
    else if (type == "mpim") {
        return "mpim.mark";
    }
    else if (type == "im") {
        return "im.mark";
    }
    else {
        return "";
    }
}

void SlackClient::markChannel(QString type, QString channelId, QString time) {
    QMap<QString,QString> params;
    params.insert("channel", channelId);
    params.insert("ts", time);

    QNetworkReply* reply = executeGet(markMethod(type), params);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Mark conversation failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::postMessage(QString channelId, QString content) {
    content.replace(QRegularExpression("&"), "&amp;");
    content.replace(QRegularExpression(">"), "&gt;");
    content.replace(QRegularExpression("<"), "&lt;");

    QMap<QString,QString> data;
    data.insert("channel", channelId);
    data.insert("text", content);
    data.insert("as_user", "true");
    data.insert("parse", "full");

    QNetworkReply* reply = executePost("chat.postMessage", data);
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);

        if (isError(data)) {
            qDebug() << "Post message failed";
        }

        reply->deleteLater();
    });
}

void SlackClient::postImage(QString channelId, QString imagePath, QString title, QString comment) {
    QMap<QString,QString> data;
    data.insert("channels", channelId);

    if (!title.isEmpty()) {
        data.insert("title", title);
    }

    if (!comment.isEmpty()) {
        data.insert("initial_comment", comment);
    }

    QFile* imageFile = new QFile(imagePath);
    if (!imageFile->open(QFile::ReadOnly)) {
        qWarning() << "image file not readable" << imagePath;
        emit postImageFail();
        return;
    }

    qDebug() << "sending image" << imagePath;
    QNetworkReply* reply = executePostWithFile("files.upload", data, imageFile);

    connect(reply, SIGNAL(finished()), imageFile, SLOT(deleteLater()));
    connect(reply, &QNetworkReply::finished, [reply,this]() {
        QJsonObject data = getResult(reply);
        qDebug() << "Post image result" << data;

        if (isError(data)) {
            emit postImageFail();
        }
        else {
            emit postImageSuccess();
        }

        reply->deleteLater();
    });
}

QVariantMap SlackClient::getMessageData(const QJsonObject message) {
    qlonglong multiplier = 1000;
    QStringList timeParts = message.value("ts").toString().split(".");
    QString timePart = timeParts.value(0);
    QString indexPart = timeParts.value(1);

    qlonglong timestamp = timePart.toLongLong() * multiplier + indexPart.toLongLong();
    QDateTime time = QDateTime::fromMSecsSinceEpoch(timestamp);

    QVariantMap data;
    data.insert("type", message.value("type").toVariant());
    data.insert("time", QVariant::fromValue(time));
    data.insert("timegroup", QVariant::fromValue(time.toString("MMMM d, yyyy")));
    data.insert("timestamp", message.value("ts").toVariant());
    data.insert("channel", message.value("channel").toVariant());
    data.insert("user", user(message));
    data.insert("attachments", getAttachments(message));
    data.insert("images", getImages(message));
    data.insert("content", QVariant(getContent(message)));

    return data;
}

QVariantMap SlackClient::user(const QJsonObject &data) {
    QString type = data.value("subtype").toString("default");
    QVariant userId;

    if (type == "bot_message") {
        userId = data.value("bot_id").toVariant();
    }
    else if (type == "file_comment") {
        userId = data.value("comment").toObject().value("user").toVariant();
    }
    else {
        userId = data.value("user").toVariant();
    }

    if (!userId.isValid()) {
        qDebug() << "User not found for message";
        qDebug() << data;
    }

    QVariantMap userData = Storage::user(userId);

    if (userData.isEmpty()) {
        userData.insert("id", data.value("user").toVariant());
        userData.insert("name", QVariant("Unknown"));
        userData.insert("presence", QVariant("away"));
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
    MessageFormatter::replaceUserInfo(content);
    MessageFormatter::replaceTargetInfo(content);
    MessageFormatter::replaceChannelInfo(content);
    MessageFormatter::replaceLinks(content);
    MessageFormatter::replaceSpecialCharacters(content);
    MessageFormatter::replaceMarkdown(content);
    MessageFormatter::replaceEmoji(content);

    return content;
}

QVariantList SlackClient::getImages(QJsonObject message) {
    QVariantList images;

    if (message.value("subtype").toString() == "file_share") {
        QStringList imageTypes;
        imageTypes << "jpg";
        imageTypes << "png";
        imageTypes << "gif";

        QJsonObject file = message.value("file").toObject();
        QString fileType = file.value("filetype").toString();

        if (imageTypes.contains(fileType)) {
            QString thumbItem = file.contains("thumb_480") ? "480" : "360";

            QVariantMap thumbSize;
            thumbSize.insert("width", file.value("thumb_" + thumbItem + "_w").toVariant());
            thumbSize.insert("height", file.value("thumb_" + thumbItem + "_h").toVariant());

            QVariantMap imageSize;
            imageSize.insert("width", file.value("original_w").toVariant());
            imageSize.insert("height", file.value("original_h").toVariant());

            QVariantMap fileData;
            fileData.insert("name", file.value("name").toVariant());
            fileData.insert("url", file.value("url_private").toVariant());
            fileData.insert("size", imageSize);
            fileData.insert("thumbSize", thumbSize);
            fileData.insert("thumbUrl", file.value("thumb_" + thumbItem).toVariant());

            images.append(fileData);
        }
    }

    return images;
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
        QString color = getAttachmentColor(attachment);
        QVariantList fields = getAttachmentFields(attachment);
        QVariantList images = getAttachmentImages(attachment);

        MessageFormatter::replaceLinks(pretext);
        MessageFormatter::replaceLinks(text);
        MessageFormatter::replaceLinks(fallback);
        MessageFormatter::replaceEmoji(text);

        int index = text.indexOf(' ', 250);
        if (index > 0) {
            text = text.left(index) + "...";
        }

        if (!title.isEmpty() && !titleLink.isEmpty()) {
            title = "<a href=\""+ titleLink + "\">" + title +"</a>";
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

QString SlackClient::getAttachmentColor(QJsonObject attachment) {
    QString color = attachment.value("color").toString();

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

    return color;
}

QVariantList SlackClient::getAttachmentFields(QJsonObject attachment) {
    QVariantList fields;
    if (attachment.contains("fields")) {
        QJsonArray fieldList = attachment.value("fields").toArray();

        foreach (const QJsonValue &fieldValue, fieldList) {
            QJsonObject field = fieldValue.toObject();
            QString title = field.value("title").toString();
            QString value = field.value("value").toString();
            bool isShort = field.value("short").toBool();

            if (!title.isEmpty()) {
                MessageFormatter::replaceLinks(title);
                MessageFormatter::replaceMarkdown(title);

                QVariantMap titleData;
                titleData.insert("isTitle", QVariant(true));
                titleData.insert("isShort", QVariant(isShort));
                titleData.insert("content", QVariant(title));
                fields.append(titleData);
            }

            if (!value.isEmpty()) {
                MessageFormatter::replaceLinks(value);
                MessageFormatter::replaceMarkdown(value);

                QVariantMap valueData;
                valueData.insert("isTitle", QVariant(false));
                valueData.insert("isShort", QVariant(isShort));
                valueData.insert("content", QVariant(value));
                fields.append(valueData);
            }
        }
    }

    return fields;
}

QVariantList SlackClient::getAttachmentImages(QJsonObject attachment) {
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

    return images;
}

void SlackClient::findNewUsers(const QString &message) {
    QRegularExpression newUserPattern("<@([A-Z0-9]+)\\|([^>]+)>");

    QRegularExpressionMatchIterator i = newUserPattern.globalMatch(message);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString id = match.captured(1);

        if (Storage::user(id).isEmpty()) {
            QString name = match.captured(2);
            QVariantMap data;
            data.insert("id", QVariant(id));
            data.insert("name", QVariant(name));
            data.insert("presence", QVariant("active"));
            Storage::saveUser(data);
        }
    }
}

void SlackClient::sendNotification(QString channelId, QString title, QString text) {
    QString body = text.length() > 100 ? text.left(97) + "..." : text;
    QString preview = text.length() > 40 ? text.left(37) + "..." : text;

    QVariantList arguments;
    arguments.append(channelId);

    Notification notification;
    notification.setAppName("Slackfish");
    notification.setAppIcon("harbour-slackfish");
    notification.setBody(body);
    notification.setPreviewSummary(title);
    notification.setPreviewBody(preview);
    notification.setCategory("chat");
    notification.setHintValue("x-slackfish-channel", channelId);
    notification.setHintValue("x-nemo-feedback", "chat_exists");
    notification.setHintValue("x-nemo-priority", 100);
    notification.setHintValue("x-nemo-display-on", true);
    notification.setRemoteAction(Notification::remoteAction("default", "", "harbour.slackfish", "/", "harbour.slackfish", "activate", arguments));
    notification.publish();
}
