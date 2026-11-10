import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0
import Sailfish.WebView.Popups 1.0

Page {
    id: appSettings

    property string uuid;
    property string url;
    property var pebble;
    property bool oauthBootFlow
    property bool oauthSubmissionActive
    property string oauthError
    property bool configurationSubmitted
    allowedOrientations: Orientation.All

    // Rebble only offers its boot link to Android/iOS clients. Keep the actual
    // engine version and apply the Android identity only to this login view.
    property bool isBootFlow: oauthBootFlow && url === "https://boot.rebble.io"
    property string bootUserAgent
    readonly property bool bootUaReady: !isBootFlow || (bootUserAgent.length > 0
                                                       && webview.webView.httpUserAgent === bootUserAgent)

    function mobileUserAgent(userAgent) {
        return userAgent.replace(/\(([^)]*)\)/, function(match, platform) {
            var revision = platform.match(/(?:^|;\s*)(rv:[^;]+)/);
            return "(Android; Mobile" + (revision ? "; " + revision[1] : "") + ")";
        });
    }

    // Runs in the web content process (see loadFrameScript below). Walks up from the click target
    // to the enclosing <a>, and if it points at a pebble scheme, cancels the doomed navigation and
    // hands the URL back to QML. dump() lands in the journal, so a miss is diagnosable on-device.
    property string pebbleLinkCatcher:
        "addEventListener('click', function(e) {" +
        "  var n = e.target;" +
        "  while (n && n.nodeType === 1) {" +
        "    if (n.localName === 'a' && n.href &&" +
        "        (n.href.indexOf('pebble://') === 0 || n.href.indexOf('pebblejs://') === 0)) {" +
        "      e.preventDefault();" +
        "      dump('pebbleLinkCatcher navigation received\\n');" +
        "      sendAsyncMessage('embed:pebble', { url: n.href });" +
        "      return;" +
        "    }" +
        "    n = n.parentNode;" +
        "  }" +
        "}, true);" +
        "dump('pebbleLinkCatcher installed\\n');"
    Connections {
        target: appSettings.pebble
        onConnectedChanged: {
            if (!appSettings.oauthBootFlow && !appSettings.pebble.connected) {
                pageStack.pop()
            }
        }
        onAccountTokenPendingChanged: {
            if (!appSettings.oauthSubmissionActive
                    || appSettings.pebble.accountTokenPending) {
                return
            }
            appSettings.oauthError = appSettings.pebble.accountTokenError
            if (appSettings.oauthError.length === 0) {
                pageStack.pop()
                return
            }
            appSettings.oauthSubmissionActive = false
        }
    }

    // Fill the whole page and let the flickable own the header, so its contentHeight spans
    // header + full web page — otherwise a config form taller than the screen can't be
    // scrolled all the way to its Save button.
    WebViewFlickable {
        id: webview
        anchors.fill: parent

        header: PageHeader {
            // The generated data: URL from pebble-clay isn't useful and looks like a bug.
            title: url.substring(0, 5) === "data:" ? qsTr("App settings") : ""
            description: url.substring(0, 5) === "data:" ? "" : url
        }

        // Both location changes and external-protocol requests return the same
        // encoded response to the watchapp.
        function handlePebbleUrl(u) {
            if (u.indexOf("pebblejs://") !== 0 && u.indexOf("pebble://") !== 0)
                return false;
            console.log("pebble configuration navigation received");
            var hIdx = u.indexOf("://") + 3;
            var hashIdx = u.indexOf("#");
            var action = hashIdx >= 0 ? u.substring(hIdx, hashIdx) : u.substring(hIdx);
            if (action.indexOf("close") === 0) {
                if (appSettings.configurationSubmitted)
                    return true;
                appSettings.configurationSubmitted = true;
                // The watchapp's webviewclosed handler (pebble-clay) wants only the fragment
                // after '#' — the URL-encoded config JSON — not the whole pebblejs://close URL.
                var response = hashIdx >= 0 ? u.substring(hashIdx + 1) : "";
                pebble.configurationClosed(appSettings.uuid, response);
                pageStack.pop();
            } else if (appSettings.isBootFlow
                       && action.indexOf("custom-boot-config-url") === 0) {
                if (!appSettings.oauthSubmissionActive
                        && !pebble.accountTokenPending) {
                    appSettings.oauthError = ""
                    appSettings.oauthSubmissionActive =
                        pebble.setOAuthTokenFromCallback(u)
                }
            }
            return true;
        }

        function handleExternalUrl(tabId, data) {
            if (!data || typeof data.url !== "string"
                    || !/^pebble(js)?:\/\//.test(data.url))
                return;

            // Replies belong to the requesting tab/document, even if navigation
            // has moved on before the asynchronous request reaches QML.
            var current = false;
            var tabs = webView.tabModel.snapshot();
            for (var i = 0; i < tabs.length; ++i) {
                if (String(tabs[i].tabId) === String(tabId)
                        && String(tabId) === String(webView.selectedTabId)
                        && String(tabs[i].locationRevision) === String(data.locationRevision)) {
                    current = true;
                    break;
                }
            }
            // Acknowledge before handling the callback can close this page.
            webView.sendAsyncMessageToTab(tabId, "externalurlresponse", {
                "winId": data.winId, "id": data.id,
                "outcome": current ? "dispatched" : "cancelled", "remember": false
            });
            if (current)
                handlePebbleUrl(data.url);
        }

        Connections {
            target: webview.webView
            ignoreUnknownSignals: true
            onRecvAsyncMessageFromTab: {
                if (message === "embed:externalurl")
                    webview.handleExternalUrl(tabId, data);
            }
        }

        webView {
            clip: true
            focus: true
            active: true

            httpUserAgent: appSettings.isBootFlow ? appSettings.bootUserAgent : ""
            // Read the engine's default UA from a blank document before the
            // first boot request; ordinary settings pages load immediately.
            onDomContentLoadedChanged: {
                if (webView.domContentLoaded && appSettings.isBootFlow
                        && appSettings.bootUserAgent.length === 0) {
                    webView.runJavaScript("return navigator.userAgent;", function(userAgent) {
                        if (typeof userAgent === "string" && userAgent.length > 0)
                            appSettings.bootUserAgent = appSettings.mobileUserAgent(userAgent);
                    });
                }
            }
            url: appSettings.oauthSubmissionActive
                 ? "about:blank"
                 : (bootUaReady ? appSettings.url : "about:blank")
            // Retain location changes for older engines. ESR153 also supplies
            // external-protocol requests above.
            onUrlChanged: handlePebbleUrl("" + webView.url)
            onViewInitialized: {
                webView.addMessageListener("embed:pebble");
                webView.addMessageListener("embed:externalurl");
                // Inject a content script that catches clicks on pebble://pebblejs:// links —
                // which gecko silently refuses to navigate to — and posts the URL back, the only
                // content->QML channel available (RawWebView exposes no navigation-request signal
                // carrying a URL). Inlined as a data: URI so there's no packaged file to locate or
                // read past the sandbox.
                webView.loadFrameScript("data:text/javascript," + encodeURIComponent(pebbleLinkCatcher));
            }
            onRecvAsyncMessage: {
                if (message === "embed:pebble" && data && data.url) {
                    console.log("embed:pebble navigation received");
                    handlePebbleUrl("" + data.url);
                }
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: appSettings.oauthSubmissionActive
                 && pebble && pebble.accountTokenPending
        visible: running
    }
    Label {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Theme.horizontalPageMargin
        }
        visible: appSettings.oauthError.length > 0
        text: appSettings.oauthError
        color: Theme.errorColor
        wrapMode: Text.Wrap
    }
}
