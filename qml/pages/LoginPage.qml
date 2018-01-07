import QtQuick 2.0
import Sailfish.Silica 1.0
import QtWebKit 3.0
import harbour.slackfish 1.0 as Slack
import "Settings.js" as Settings

Page {
    id: page

    property string processId: Math.random().toString(36).substring(7)
    property string startUrl: "https://slack.com/oauth/authorize?scope=client&client_id=64034884849.292601783733&redirect_uri=http%3A%2F%2Flocalhost%3A3000%2Foauth%2Fcallback"

    SilicaWebView {
        id: webView
        anchors.fill: parent
        url: page.startUrl + "&state=" + page.processId

        header: PageHeader {
            title: "Sign in with Slack"
        }

        onNavigationRequested: {
            if (isReturnUrl(request.url)) {
                visible = false
                request.action = WebView.IgnoreRequest

                if (isSuccessUrl(request.url)) {
                    Slack.Client.fetchAccessToken(request.url)
                }
                else {
                    pageStack.pop(undefined, PageStackAction.Animated)
                }
            }
            else {
                request.action = WebView.AcceptRequest
            }
        }
    }

    Component.onCompleted: {
        Slack.Client.onAccessTokenSuccess.connect(handleAccessTokenSuccess)
        Slack.Client.onAccessTokenFail.connect(handleAccessTokenFail)
    }

    Component.onDestruction: {
        Slack.Client.onAccessTokenSuccess.disconnect(handleAccessTokenSuccess)
        Slack.Client.onAccessTokenFail.disconnect(handleAccessTokenFail)
    }

    function isReturnUrl(url) {
        return url.toString().indexOf('http://localhost:3000/oauth/callback') !== -1
    }

    function isSuccessUrl(url) {
        return url.toString().indexOf('error=') === -1 && url.toString().indexOf('state=' + page.processId) !== -1
    }

    function handleAccessTokenSuccess(userId, teamId, teamName) {
        Settings.setUserInfo(userId, teamId, teamName)
        pageStack.pop(undefined, PageStackAction.Animated)
    }

    function handleAccessTokenFail() {
        console.log('access token failed')
        webView.visible = true
    }
}
