#include "networkaccessmanagerfactory.h"

#include <QDebug>

#include "networkaccessmanager.h"

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    qDebug() << "Creating NetworkAccessManager";
    NetworkAccessManager *manager = new NetworkAccessManager(parent);
    return manager;
}
