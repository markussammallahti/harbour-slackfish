#ifndef SLACKCLIENT_H
#define SLACKCLIENT_H

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include "slackconfig.h"
#include "QtWebsocket/QWsSocket.h"

class SlackClient : public QObject
{
    Q_OBJECT
public:
    explicit SlackClient(QObject *parent = 0);
    ~SlackClient();

    Q_INVOKABLE QVariantList getChannels();

signals:
    void testConnectionFail();
    void testLoginSuccess(QString userId, QString teamId, QString team);
    void testLoginFail();
    void accessTokenSuccess(QString userId, QString teamId, QString team);
    void accessTokenFail();
    void loadMessagesSuccess(QString channelId, QVariantList messages);
    void loadMessagesFail();
    void initFail();
    void initSuccess();
    void reconnectFail();
    void reconnectAccessTokenFail();
    void messageReceived(QVariantMap message);
    void channelUpdated(QVariantMap channel);
    void reconnecting();
    void disconnected();
    void connected();

public slots:
    void start();
    void fetchAccessToken(QUrl url);
    void testLogin();
    void loadMessages(QString type, QString channelId);
    void postMessage(QString channelId, QString content);
    void markChannel(QString type, QString channelId, QString time);

    void handleNetworkAccessibleChanged(QNetworkAccessManager::NetworkAccessibility accessible);
    void handleStartReply();
    void handleAccessTokenReply();
    void handleTestLoginReply();
    void handleMarkChannelReply();
    void handleLoadMessagesReply();
    void handlePostMessageReply();
    void handleListerStart();
    void handleListerEnd();
    void handleMessage(QString message);
    void checkConnection();
    void reconnect();

private:
    bool isOk(const QNetworkReply *reply);
    bool isError(const QJsonObject &data);
    QJsonObject getResult(QNetworkReply *reply);

    void parseMessageUpdate(QJsonObject message);
    void parseChannelUpdate(QJsonObject message);

    QVariantMap getMessageData(const QJsonObject message);

    QString getContent(QJsonObject message);
    QVariantList getAttachments(QJsonObject message);
    void findNewUsers(const QString &message);
    void replaceUserInfo(QString &message);
    void replaceTargetInfo(QString &message);
    void replaceChannelInfo(QString &message);
    void replaceSpecialCharacters(QString &message);
    void replaceLinks(QString &message);
    void replaceMarkdown(QString &message);
    void replaceEmoji(QString &message);
    QVariantMap user(const QJsonObject &data);

    QUrl historyUrl(QString type);
    QUrl markUrl(QString type);

    QPointer<QNetworkAccessManager> networkAccessManager;
    QPointer<QNetworkReply> accessTokenReply;
    QPointer<QNetworkReply> testLoginReply;
    QPointer<QNetworkReply> startReply;
    QPointer<QNetworkReply> loadMessagesReply;
    QPointer<QNetworkReply> postMessageReply;
    QPointer<QNetworkReply> markChannelReply;
    QPointer<SlackConfig> config;
    QPointer<QtWebsocket::QWsSocket> webSocket;
    QPointer<QTimer> checkTimer;
    QPointer<QTimer> reconnectTimer;

    int lastMessageId;
    bool isConnected;

    QNetworkAccessManager::NetworkAccessibility networkAccessible;

    QVariantMap channels;
    QVariantMap users;
    QVariantMap channelMessages;
};

#endif // SLACKCLIENT_H
