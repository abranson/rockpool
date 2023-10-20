#ifndef TIMELINESYNC_H
#define TIMELINESYNC_H

#include <QObject>
#include <QUuid>
#include <QHash>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

QT_FORWARD_DECLARE_CLASS(Pebble)
QT_FORWARD_DECLARE_CLASS(TimelineManager)
QT_FORWARD_DECLARE_CLASS(QNetworkAccessManager)
QT_FORWARD_DECLARE_CLASS(QNetworkRequest)
QT_FORWARD_DECLARE_CLASS(QNetworkReply)
QT_FORWARD_DECLARE_CLASS(QSettings)

class TimelineSync : public QObject
{
    Q_OBJECT
public:
    TimelineSync(Pebble *pebble, TimelineManager *manager);

    // https://timeline-sync.getpebble.com/docs
    static const QString subscriptionsUrl;

    // for DBus interface
    const QString& oauthToken() const {return m_oauthToken;}
    void setOAuthToken(const QString &token);
    bool syncFromCloud() const { return m_syncFromCloud; }
    void setSyncFromCloud(bool enabled);
    // Account info
    const double & accountId() const {return m_accountId;}
    const QString accountName() const;
    const QString accountEmail() const;

signals:
    void oauthTokenChanged(const QString &token);
    void syncUrlChanged(const QString &url);
    void timelineOp(const QJsonObject &op);
    void wipePinKind(const QString &kind);

public slots:
    void resyncUrl(const QString &url);
    void syncLocker(bool force=false);

private slots:
    void doWebsync();
    void webOpHandler(const QJsonObject &op);
    void resyncLocker(bool force=false);

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    // https://timeline-api.getpebble.com/docs
    static const QString s_internalApi;
    static const QString s_accInfoUrl;
    static const QString initialSyncUrl() {return s_internalApi+"/v1/sync";}
    static const QString sandboxTokens() {return s_internalApi+"/v1/tokens/sandbox/";}
    static const QString s_lockerUrl;

    QString syncUrl() { return m_syncUrl.isEmpty() ? initialSyncUrl():m_syncUrl;}
    QString m_syncUrl;

    QString m_oauthToken;
    double m_accountId;
    bool m_syncFromCloud = false;
    void fetchLocker(bool force = false, void (*next)(void*) = 0, void *ctx = 0) const;
    mutable QHash<QUuid,QJsonObject> m_locker;

    // to use with templated callbacks hiding internal logic
    void timelineApiQuery(const QByteArray &verb, const QString &url, void *ctx, void(*ack)(void*,const QJsonObject&), void(*nack)(void*,const QString&) = 0, const QString &token = QString()) const;
    // builds request authenticated by m_oauthToken
    QNetworkRequest authedRequest(const QString &url, const QString &token = QString()) const;
    //QJsonObject processJsonReply(QNetworkReply *rpl, QString &err) const;

    QPointer<QNetworkReply> m_pendingReply;
    int m_tmr_websync = 0;
    QString m_timelineStoragePath;
    QNetworkAccessManager *m_nam;
    QSettings *m_ini;
    Pebble *m_pebble;
    TimelineManager *m_manager;
public:
    // JSKit Utilities - asynchronous calls with callback interface
    ////
    // getTimelineToken returns user_token from locker, or tries to obtain
    // token from sandbox when application is not available in the locker
    template<typename Ack, typename Nak>
    void getTimelineToken(const QUuid &app_uuid, Ack ack, Nak nak) const {
        struct ctx {
            const QUuid &id;
            const TimelineSync *me;
            Ack ack;
            Nak nak;
        } * context = new ctx({app_uuid,this,ack,nak});
        fetchLocker(false,[](void*p){
            struct ctx *c=(struct ctx*)p;
            if(c->me->m_locker.contains(c->id)) {
                c->ack(c->me->m_locker.value(c->id).value("user_token").toString());
                delete c;
            } else {
                // Check for sandbox token
                c->me->timelineApiQuery("GET",sandboxTokens()+"/"+c->id.toString(),p,[](void*pc,const QJsonObject &obj){
                    if(obj.contains("token"))
                        ((struct ctx*)pc)->ack(obj.value("token").toString());
                    else
                        ((struct ctx*)pc)->nak("Neither production nor sandbox token found");
                    delete ((struct ctx*)pc);
                },[](void*pc,const QString &err){
                    ((struct ctx*)pc)->nak(err);
                    delete ((struct ctx*)pc);
                });
            }
        },context);
    }

    // getSubscriptions return subscribed topics for given app represented by timeline token
    // from public timeline API
    template<typename Ack,typename Nak>
    void getSubscriptions(const QString &token, Ack ack, Nak nak) const {
        struct ctx {
            Ack ack;
            Nak nak;
        } * context = new ctx({ack,nak});
        timelineApiQuery("GET",subscriptionsUrl,context,[](void*c,const QJsonObject &obj){
            if(obj.contains("topics"))
                ((struct ctx*)c)->ack(obj.value("topics").toVariant().toStringList());
            else
                ((struct ctx*)c)->nak("Malformed response: missing 'topics' field in reply "+QJsonDocument(obj).toJson());
            delete ((struct ctx*)c);
        },[](void*c,const QString &err){
            ((struct ctx*)c)->nak(err);
            delete ((struct ctx*)c);
        },token);
    }

    // topicSubscribe subscribes user to the specified topic of specific app represented by
    // timeline token
    template<typename Ack,typename Nak>
    void topicSubscribe(const QString &token, const QString &topic, Ack ack, Nak nak) const {
        struct ctx {
            Ack ack;
            Nak nak;
        } * context = new ctx({ack,nak});
        timelineApiQuery("POST",subscriptionsUrl + "/" + topic,context,[](void*c,const QJsonObject &obj){
            ((struct ctx*)c)->ack(obj.value("id").toString());
            delete ((struct ctx*)c);
        },[](void*c,const QString &err){
            if(err.endsWith(" OK"))
                ((struct ctx*)c)->ack("OK");
            else
                ((struct ctx*)c)->nak(err);
            delete ((struct ctx*)c);
        },token);
    }

    // topicUnsubscribe removes subscription of the user of the app represented by timeline token
    // from specified topic
    template<typename Ack,typename Nak>
    void topicUnsubscribe(const QString &token, const QString &topic, Ack ack, Nak nak) const {
        struct ctx {
            Ack ack;
            Nak nak;
        } * context = new ctx({ack,nak});
        timelineApiQuery("DELETE",subscriptionsUrl + "/" + topic,context,[](void*c,const QJsonObject &obj){
            ((struct ctx*)c)->ack(obj.value("id").toString());
            delete ((struct ctx*)c);
        },[](void*c,const QString &err){
            if(err.endsWith(" OK"))
                ((struct ctx*)c)->ack("OK");
            else
                ((struct ctx*)c)->nak(err);
            delete ((struct ctx*)c);
        },token);
    }
};

#endif // TIMELINESYNC_H
