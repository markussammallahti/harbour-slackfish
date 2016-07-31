#include "networkaccessmanager.h"

NetworkAccessManager::NetworkAccessManager(QObject *parent): QNetworkAccessManager(parent) {
    qDebug() << "Creating NetworkAccessManager";
    config = new SlackConfig(this);
}

QNetworkReply* NetworkAccessManager::createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) {
    qDebug() << "Creating request" << request.url().host();

    if (request.url().host() == "files.slack.com") {
        QNetworkRequest copy(request);

        QString token = config->accessToken();
        if (!token.isEmpty()) {
            qDebug() << "Set token" << token;
            copy.setRawHeader(QString("Authorization").toUtf8(), QString("Bearer " + token).toUtf8());
        }
        else {
            qDebug() << "No token";
        }

        return QNetworkAccessManager::createRequest(op, copy, outgoingData);
    }
    else {
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    }
}
