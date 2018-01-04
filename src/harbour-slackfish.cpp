#include <QtQuick>
#include <sailfishapp.h>

#include "slackclient.h"
#include "networkaccessmanagerfactory.h"
#include "notificationlistener.h"
#include "dbusadaptor.h"
#include "storage.h"
#include "filemodel.h"

static QObject *slack_client_provider(QQmlEngine *engine, QJSEngine *scriptEngine) {
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    SlackClient *client = new SlackClient();
    return client;
}

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    QSettings settings;
    QString lastVersion = settings.value("app/lastVersion").toString();
    if (lastVersion.isEmpty()) {
        qDebug() << "No last version set, removing previous access token";
        settings.remove("user/accessToken");
    }

    qDebug() << "Setting last version" << APP_VERSION;
    settings.setValue("app/lastVersion", QVariant(APP_VERSION));

    SlackConfig::clearWebViewCache();

    qmlRegisterSingletonType<SlackClient>("harbour.slackfish", 1, 0, "Client", slack_client_provider);

    view->rootContext()->setContextProperty("applicationVersion", APP_VERSION);
    view->rootContext()->setContextProperty("fileModel", new FileModel());

    view->setSource(SailfishApp::pathTo("qml/harbour-slackfish.qml"));
    view->engine()->setNetworkAccessManagerFactory(new NetworkAccessManagerFactory());
    view->showFullScreen();

    NotificationListener* listener = new NotificationListener(view.data());
    new DBusAdaptor(listener);
    QDBusConnection connection = QDBusConnection::sessionBus();
    connection.registerService("harbour.slackfish");
    connection.registerObject("/", listener);

    int result = app->exec();

    qDebug() << "Application terminating";

    delete listener;

    return result;
}
