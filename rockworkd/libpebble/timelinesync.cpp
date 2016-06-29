#include "timelinesync.h"
#include "timelinemanager.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QSettings>

/**
 * @brief TimelineSync::TimelineSync
 * @param pebble
 * @param manager
 *
 * TimelineSync class starts a timer to run a websync task, to pull pins from pebble.com. For
 * authorisation it uses OAuth2 token which is locally stored. Valid token should be set by
 * pebble class i.e. via DBus. If token results in Auth.Error - token is cleared and stored.
 * Cleared (empty) token means websync is disabled (stops the timer).
 *
 * Since the class is responsible for timeline API calls, it also executes JSKit timeline calls.
 * And for that it also syncs and handles pabble locker API. Simply because no one else cares
 * about the locker or need it. As a side effect - it will install all the apps registered in the
 * locker but missing in the local configuration - i.e. "keep the apps in sync with the cloud".
 * Obviously all these actions are possible only with valid OAuth token.
 *
 * All the configuration is preserved in QSettings based file timeline/manager.ini
 */
TimelineSync::TimelineSync(Pebble *pebble, TimelineManager *manager):
  QObject(pebble),
  m_nam(pebble->nam()),
  m_pebble(pebble),
  m_manager(manager)
{
    m_timelineStoragePath = pebble->storagePath() + "timeline";
    m_ini = new QSettings(m_timelineStoragePath + "/sync.ini",QSettings::IniFormat);
    // Next URL to be used for sync (or last used if there was a crash)
    m_syncUrl = m_ini->value("syncUrl").toString();
    // OAuth2 token retreived from https://auth-client.getpebble.com/en_US/
    m_oauthToken = m_ini->value("oauthToken").toString();
    // Pebble Account ID retrievd from https://auth.getpebble.com/api/v1/me.json
    m_accountId = m_ini->value("accountId").toString();
    // Whether we install apps which are in the locker but missing locally
    m_syncFromCloud = m_ini->value("syncFromCloud").toBool();

    // Make connection to itself queued to prevent recursive closure and yield to event loop
    connect(this, &TimelineSync::timelineOp, this, &TimelineSync::webOpHandler, Qt::QueuedConnection);
    connect(this, &TimelineSync::syncUrlChanged, this, &TimelineSync::resyncUrl,Qt::QueuedConnection);
    connect(this,&TimelineSync::wipePinKind,manager,&TimelineManager::wipeTimeline,Qt::QueuedConnection);
    // Shortcut to catch token becoming invalid - invalidates timeline and account info
    connect(this, &TimelineSync::oauthTokenChanged, this, &TimelineSync::setOAuthToken,Qt::QueuedConnection);
    // websync needs more frequent update to keep apps up-to-whatever-they-need
    if(!m_oauthToken.isEmpty())
        m_tmr_websync = startTimer(30000);
}

const QString TimelineSync::subscriptionsUrl = "https://timeline-api.getpebble.com/v1/user/subscriptions";
const QString TimelineSync::s_internalApi = "https://timeline-sync.getpebble.com";
const QString TimelineSync::s_lockerUrl = "https://api2.getpebble.com/v2/locker/";

void TimelineSync::resyncUrl(const QString &url)
{
    m_syncUrl=url;
    m_ini->setValue("syncUrl",url);
    doWebsync();
}

QNetworkRequest TimelineSync::authedRequest(const QString &url, const QString &token) const
{
    QNetworkRequest req=QNetworkRequest(QUrl(url));
    req.setRawHeader("Authorization",QString("Bearer %1").arg(m_oauthToken).toLatin1());
    if(!token.isEmpty())
        req.setRawHeader("X-User-Token",token.toLatin1());
    return req;
}

QJsonObject processJsonReply(QNetworkReply *rpl, QString &err)
{
    rpl->deleteLater();
    if(rpl && rpl->error() == QNetworkReply::NoError) {
        QByteArray data = rpl->read(rpl->bytesAvailable());
        QJsonParseError jpe;
        QJsonDocument doc = QJsonDocument::fromJson(data,&jpe);
        if(jpe.error == QJsonParseError::NoError && doc.isObject() && !doc.object().isEmpty()) {
            QJsonObject obj = doc.object();
            if(obj.contains("error") || obj.contains("errorString")) {
                err.append("Response contains error: ").append(obj.value("error").toString()).append(obj.value("errorString").toString());
            } else {
                return obj;
            }
        } else {
            err.append(QString("Cannot parse response: %1 %2").arg(jpe.errorString(),QString(data)));
            qDebug() << "Cannot parse" << data;
        }
    } else {
        err.append("HTTP Error: ").append(rpl->errorString());
    }
    return QJsonObject();
}

void TimelineSync::timerEvent(QTimerEvent *event)
{
    if(event) {
        doWebsync();
    }
}

void TimelineSync::doWebsync()
{
    if(m_oauthToken.isEmpty()) {
        qDebug() << "No valid authentication token, skipping WebSync";
        return;
    }
    // Attempt to bring up internal state since qt is not tracking it properly
    if(m_nam->networkAccessible()!=QNetworkAccessManager::Accessible)
        m_nam->setNetworkAccessible(QNetworkAccessManager::Accessible);
    qDebug() << "Syncing from" << syncUrl();
    QNetworkReply *rpl = m_nam->get(authedRequest(syncUrl()));
    connect(rpl,&QNetworkReply::finished,[this,rpl](){
        QString err;
        QJsonObject obj = processJsonReply(rpl,err);
        if(!err.isEmpty()) {
            qWarning() << "Cannot parse response" << err;
            return;
        }
        if(obj.contains("error")) {
            if(obj.value("error").toString()=="Unauthorized") {
                m_oauthToken.clear();
                m_ini->setValue("oauthToken",m_oauthToken);
                emit oauthTokenChanged(m_oauthToken);
                return;
            }
            qCritical() << "Unknown error received" << obj.value("error").toString() << obj;
            return;
        }
        if(obj.value("mustResync").toBool()) {
            qDebug() << "Timeline forces full resync to" << obj.value("syncURL").toString();
            emit wipePinKind("web");
            emit syncUrlChanged(obj.value("syncURL").toString());
            return;
        }
        QJsonArray arr = obj.value("updates").toArray();
        if(arr.count()>0) {
            qDebug() << "Got stuff to work on" << obj;
            for(int i=0;i<arr.size();i++) {
                emit timelineOp(arr.at(i).toObject());
            }
        }
        if(obj.contains("nextPageURL")) {
            emit syncUrlChanged(obj.value("nextPageURL").toString());
        } else {
            m_syncUrl = obj.value("syncURL").toString();
            m_ini->setValue("syncUrl",m_syncUrl);
        }
    });
}

void TimelineSync::webOpHandler(const QJsonObject &op)
{
    QString opt = op.value("type").toString();
    if(opt.isEmpty()) {
        qWarning() << "Empty operation type, skipping" << opt;
    } else if(opt == "timeline.pin.create") {
        m_manager->insertTimelinePin(op.value("data").toObject());
    } else if(opt == "timeline.pin.delete") {
        m_manager->removeTimelinePin(op.value("data").toObject().value("guid").toString());
    } else if(opt == "timeline.topic.subscribe") {
        qDebug() <<  "Asked to subscribe but we have nothing to do here:" << op.value("data").toObject().value("topicKey");
    } else if(opt == "timelime.topic.unsubscribe") {
        m_manager->wipeSubscription(op.value("data").toObject().value("topicKey").toString());
    } else {
        qWarning() << "Unknown operation type, skipping" << opt;
    }
}

void TimelineSync::timelineApiQuery(const QByteArray &verb, const QString &url, void *ctx, void (*ack)(void *, const QJsonObject &), void (*nack)(void *, const QString &), const QString &token) const
{
    qDebug() << "API call for" << verb << url << token;
    QNetworkReply *rpl = m_nam->sendCustomRequest(authedRequest(url,token),verb);
    connect(rpl,&QNetworkReply::finished,[this,rpl,ctx,ack,nack](){
        QString err;
        QJsonObject obj=processJsonReply(rpl,err);
        if(obj.isEmpty()) {
            if(nack)
                nack(ctx,err);
            else
                qWarning() << err;
        } else {
            ack(ctx,obj);
        }
    });
}

// Set empty token to disable websync
void TimelineSync::setOAuthToken(const QString &token)
{
    if(token == m_oauthToken) return;
    m_oauthToken = token;
    m_ini->setValue("oauthToken",token);
    if(token.isEmpty()) {
        qDebug() << "Setting empty oauth: disable websync and cleanup web resources";
        if(m_tmr_websync) {
            killTimer(m_tmr_websync);
            m_tmr_websync = 0;
        }
        m_ini->remove("accountId");
        m_accountId = "";
        m_syncUrl = "";
        m_ini->remove("syncUrl");
        emit wipePinKind("web");
        m_locker.clear();
        emit oauthTokenChanged(m_oauthToken);
    } else {
        // Try to validate token by requesting accountId and comparing it to current
        qDebug() << "Validating OAuth token" << token;
        QNetworkReply *rpl = m_nam->get(authedRequest("https://auth.getpebble.com/api/v1/me.json"));
        connect(rpl,&QNetworkReply::finished,[this,rpl](){
            QString err;
            QJsonObject obj = processJsonReply(rpl,err);
            if(!obj.isEmpty()) {
                if(obj.contains("id") && obj.value("id").isString()) {
                    if(m_accountId == obj.value("id").toString())
                        return; // it was token refresh
                    if(m_tmr_websync==0)
                        m_tmr_websync=startTimer(30000);
                    qDebug() << "OAuth Token validated but points to different account" << m_accountId << obj.value("id").toString();
                    m_accountId = obj.value("id").toString();
                    m_ini->setValue("accountId",m_accountId);
                    m_ini->setValue("accountName",obj.value("name").toString());
                    m_ini->setValue("accountEmail",obj.value("email").toString());

                    // if token differs - invalidate timeline
                    emit wipePinKind("web");
                    emit oauthTokenChanged(m_oauthToken);
                    emit syncUrlChanged(initialSyncUrl());

                    // Also - the locker
                    m_locker.clear();
                    fetchLocker(); // Don't do actual synchronisation here, defer to next time
                    return;
                } else {
                    qWarning() << "Unexpected response" << QJsonDocument(obj).toJson();
                }
            } else
                qWarning() << err;
            setOAuthToken(QString());
        });
    }
}

const QString TimelineSync::accountName() const
{
    return m_ini->value("accountName").toString();
}
const QString TimelineSync::accountEmail() const
{
    return m_ini->value("accountEmail").toString();
}

void TimelineSync::setSyncFromCloud(bool enabled)
{
    if(m_syncFromCloud==enabled) return;
    m_syncFromCloud = enabled;
    m_ini->setValue("syncFromCloud",enabled);
    if(enabled)
        syncLocker();
}

void TimelineSync::syncLocker(bool force)
{
    struct ctx {
        TimelineSync *me;
        bool force;
    } * context = new ctx({this,force});
    fetchLocker(force,[](void*p){
        ((struct ctx*)p)->me->resyncLocker(((struct ctx*)p)->force);
        delete ((struct ctx*)p);
    },context);
}
void TimelineSync::fetchLocker(bool force, void (*next)(void*), void *ctx) const
{
    if(!m_oauthToken.isEmpty() && (m_locker.isEmpty() || force)) {
        qDebug() << "Fetching locker content" << force;
        QNetworkReply *rpl = m_nam->get(authedRequest(s_lockerUrl));
        connect(rpl,&QNetworkReply::finished, [this,rpl,next,ctx](){
            QString err;
            QJsonObject obj = processJsonReply(rpl,err);
            if(!obj.isEmpty()) {
                if(obj.contains("applications")) {
                    QJsonArray arr = obj.value("applications").toArray();
                    for(int i=0;i<arr.size();i++) {
                        QJsonObject el(arr.at(i).toObject());
                        el.remove("hardware_platforms"); // big and useless atm
                        qDebug() << "Adding to locker" << el.value("title").toString();
                        m_locker.insert(QUuid(el.value("uuid").toString()),el);
                    }
                } else {
                    qWarning() << "Response does not contain applications array:" << QJsonDocument(obj).toJson();
                }
            } else
                qWarning() << err;
            if(next)
                next(ctx);
        });
    } else if(next)
        next(ctx);
}
void TimelineSync::resyncLocker(bool force)
{
    if(m_oauthToken.isEmpty()) return;
    qDebug() << "Locker resync: first handle apps missing here:" << (m_syncFromCloud ? "install missing" : "clean the locker");
    if(!m_locker.isEmpty() && (force || m_syncFromCloud)) {
        for(QHash<QUuid,QJsonObject>::const_iterator it=m_locker.begin();it!=m_locker.end();it++) {
            if(!m_pebble->appInfo(it.key()).isValid()) {
                if(force) {
                    qDebug() << "Removing" << it.key() << it.value().value("title").toString() << "from locker";
                    QNetworkReply *rpl = m_nam->deleteResource(authedRequest(s_lockerUrl + it.key().toString().mid(1,36)));
                    connect(rpl,&QNetworkReply::finished,[this,&it,rpl](){
                        rpl->deleteLater();
                        if(rpl->error()==QNetworkReply::NoError)
                            m_locker.remove(it.key());
                        else
                            qWarning() << "Error deleting" << it.key() << rpl->errorString();
                    });
                } else { // No force means we're left to sync apps from cloud
                    qDebug() << "Syncing" << it.key().toString() << it.value().value("title").toString() << "from locker";
                    m_pebble->installApp(it.value().value("id").toString());
                }
            }
        }
    }
    QSet<QUuid> missing = m_pebble->installedAppIds().toSet().subtract(m_locker.keys().toSet());
    qDebug() << "Locker resync: next push to the locker those" << missing.size() << "missing there";
    foreach(const QUuid &id,missing) {
        QNetworkReply *rpl = m_nam->put(authedRequest(s_lockerUrl+id.toString().mid(1,36)),QByteArray());
        connect(rpl,&QNetworkReply::finished,[this,rpl](){
            QString err;
            QJsonObject obj=processJsonReply(rpl,err);
            if(obj.isEmpty()) {
                qWarning() << err;
            } else {
                if(obj.contains("application")) {
                    QJsonObject el(obj.value("application").toObject());
                    el.remove("hardware_platforms");
                    m_locker.insert(QUuid(el.value("uuid").toString()),el);
                    qDebug() << "Registered" << el.value("uuid").toString() << el.value("title").toString() << "to the locker";
                } else {
                    qWarning() << "Error adding application to locker - empty reply:" << QJsonDocument(obj).toJson();
                }
            }
        });
    }
}
