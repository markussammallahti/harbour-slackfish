#include <QtQuick>
#include <sailfishapp.h>

#include "slackclient.h"
#include "networkaccessmanagerfactory.h"

static QObject *slack_client_provider(QQmlEngine *engine, QJSEngine *scriptEngine) {
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    SlackClient *client = new SlackClient();
    return client;
}

int main(int argc, char *argv[])
{
    // SailfishApp::main() will display "qml/template.qml", if you need more
    // control over initialization, you can use:
    //
    //   - SailfishApp::application(int, char *[]) to get the QGuiApplication *
    //   - SailfishApp::createView() to get a new QQuickView * instance
    //   - SailfishApp::pathTo(QString) to get a QUrl to a resource file
    //
    // To display the view, call "show()" (will show fullscreen on device).

    QCoreApplication::setOrganizationName("harbour-slackfish");
    QCoreApplication::setApplicationName("harbour-slackfish");

    SlackConfig::clearWebViewCache();
    //QSettings settings; settings.remove("user/accessToken");

    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    qmlRegisterSingletonType<SlackClient>("harbour.slackfish", 1, 0, "Client", slack_client_provider);

    view->setSource(SailfishApp::pathTo("qml/harbour-slackfish.qml"));
    view->engine()->setNetworkAccessManagerFactory(new NetworkAccessManagerFactory());
    view->showFullScreen();

    int result = app->exec();

    qDebug() << "Application terminating";
    return result;
}
