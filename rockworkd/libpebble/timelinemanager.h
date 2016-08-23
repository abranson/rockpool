#ifndef TIMELINEMANAGER_H
#define TIMELINEMANAGER_H

#include "blobdb.h"
#include "timelineitem.h"
#include <QObject>

#include <QMutex>
#include <QTimer>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// layouts.json attribute representation
struct Attr {
    quint8 id;
    quint16 max;
    QString type;
    QString note;
    QHash<QString,quint8> enums;
};

// Wrapper class to encapsulate persistance, serialization and nested objects
class TimelineManager;
class TimelinePin {
public:
    TimelinePin() {}
    TimelinePin(const TimelinePin &src);
    TimelinePin(const QJsonDocument &json, TimelineManager *manager) : TimelinePin(json.object(),manager){}
    TimelinePin(const QJsonObject &obj, TimelineManager *manager, const QUuid &uuid = QUuid(), const TimelinePin *parent = 0);
    TimelinePin(const QString &fileName, TimelineManager *manager);

    const QString id() const {return m_pin.contains("id")?m_pin.value("id").toString():m_uuid.toString();}
    const QUuid & guid() const {return m_uuid;}
    const QUuid & parent() const {return m_parent;}
    QString kind() const {return m_kind;}
    QString source() const {return m_pin.value("source").toString();}
    TimelineItem::Type type() const {return m_type;}
    BlobDB::BlobDBId blobId() const {return item2blob[m_type];}
    QDateTime time() const  {return (m_time.isValid()?m_time:(m_updated.isValid()?m_updated:m_created));}
    quint32 gmtime_t() const {return time().toTime_t();}
    QDateTime created() const {return m_created;}
    QDateTime updated() const {return m_updated;}
    int duration() const {return m_pin.value("duration").toInt();}
    const QJsonObject layout() const {return m_pin.value("layout").toObject();}
    QJsonArray actions() const {return m_pin.value("actions").toArray();}
    QJsonArray reminders() const {return m_pin.value("reminders").toArray();}
    QStringList topics() const { return m_topics;}

    // Lifecycle control flags
    bool isValid() const { return m_type != TimelineItem::TypeInvalid;}
    bool sendable() const { return m_sendable;}
    void setSendable(bool b) {m_sendable=b;}
    bool sent() const { return m_sent;}
    void setSent(bool b) { m_sent=b;m_pending=false;m_deleted=!b;m_rejected=!b;}
    bool rejected() const { return m_rejected;}
    void setRejected(bool b) {m_rejected=b;m_pending=false;}
    bool deleted() const { return m_deleted;}
    void setDeleted(bool b) {m_deleted=b;m_pending=false;m_sent=!b;m_rejected=!b;}
    bool pending() const {return m_pending;}

    // nested objects ops
    typedef QList<const TimelinePin*> PtrList;
    PtrList kids(TimelineItem::Type type=TimelineItem::TypeNotification) const;
    const TimelinePin makeNotification(const TimelinePin *old) const;
    const QList<TimelinePin> makeReminders() const;
    const QJsonArray & getActions() const;
    QList<TimelineAttribute> handleAction(TimelineAction::Type atype, quint8 id, const QJsonObject &param) const;
    void updateTopics(const TimelinePin &pin);

    void update(const TimelineItem &item, const QDateTime ts = QDateTime::currentDateTimeUtc());

    // watch operations
    TimelineItem toItem() const;
    void flush() const;
    void remove() const;
    void send() const;
    void erase(bool force=false) const;

private:
    void initJson();
    void buildActions() const;

    TimelineManager *m_manager;
    QUuid m_uuid;
    QUuid m_parent;
    QString m_kind;
    TimelineItem::Type m_type = TimelineItem::TypeInvalid;
    QDateTime m_created;
    QDateTime m_updated;
    QDateTime m_time;
    QJsonObject m_pin;
    QStringList m_topics;
    bool m_rejected = false;
    bool m_sendable = true;
    bool m_deleted = false;
    bool m_sent = false;
    mutable bool m_pending = false;
    mutable QJsonArray m_actions;

    static const BlobDB::BlobDBId item2blob[4];
};


class TimelineManager : public QObject
{
    Q_OBJECT
    friend class TimelinePin;
public:
    TimelineManager(Pebble *pebble, WatchConnection *connection);

    // Timeline Layout
    quint32 getRes(const QString &key) const;
    quint8 getLayout(const QString &key) const;
    Attr getAttr(const QString &key) const;
    // New Timeline API
    void insertTimelinePin(const QJsonObject &json);
    void removeTimelinePin(const QString &guid);
    void clearTimeline(const QUuid &parent);

    void setTimelineWindow(int daysPast, int eventFadeout, int daysFuture);
    int daysPast() const {return m_past_days;}
    int daysFuture() const {return m_future_days;}
    int secsEventFadeout() const {return m_event_fadeout;}

public slots:
    void reloadLayouts();
    void wipeTimeline(const QString &kind = QString());
    void wipeSubscription(const QString &topic);

signals:
    void muteSource(const QString &sourceId);
    void removeNotification(const QUuid &uuid);
    void actionTriggered(const QUuid &uuid, const QString &type, const QJsonObject &param);
    void snoozeReminder(const QUuid &event, const QString &id, const QDateTime &oldTime, const QDateTime &newTime);
    void actionSendText(const QString &contact, const QString &text);

private slots:
    void actionHandler(const QByteArray &data);
    void notifyHandler(const QDateTime &ts, const QUuid &key, const TimelineItem &val);
    void blobdbAckHandler(BlobDB::BlobDBId db, BlobDB::Operation cmd, const QByteArray &key, BlobDB::Status ack);
    void doMaintenance();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    void insert(const class TimelinePin &pin);
    void remove(const class TimelinePin &pin);
    void addPin(const class TimelinePin &pin);
    quint32 pinCount(const QUuid *parent = 0);
    bool pinExists(const QUuid &guid) const;
    TimelinePin * getPin(const QUuid &guid);
    void removePin(const QUuid &guid);
    const TimelinePin::PtrList pinKids(const QUuid &parent);

    // In-Memory Pin Storage Index. We need:
    // - global index <QUuid,TimelinePin> - object storage hash {guid: pin} - primary pin storage
    QHash<QUuid,class TimelinePin> m_pin_idx_guid;
    // - parent index <QUuid,QList<QUuid>> - parent lookup hash {pin.parent:[pin.guid,]}
    QHash<QUuid,QList<QUuid>> m_pin_idx_parent;
    // - time map <time_t,QList<QUuid>> - time lookup map with sorted keys {pin.time_t:[pin.guid,]} - needed for maintenance/retention
    QMap<time_t,QList<QUuid>> m_pin_idx_time;
    // Subscription Index. We need just topic->pins relation for unsubscribe() cleanup.
    QHash<QString,QList<QUuid>> m_idx_subscription;
    // All should be updated in atomic syncronized transaction to prevent retention/sync timer race condition
    QMutex m_mtx_pinStorage;

    // Timeline window knobs. Pebble doesn't show future further than 48hrs ahead.
    // However it keeps pins on watches and shows them once the time has come
    int m_future_days = 7;
    int m_past_days = -2;
    int m_event_fadeout = -3600;

    TimelineAttribute parseAttribute(const QString &key, const QJsonValue &val);
    QJsonObject &deserializeAttribute(const TimelineAttribute &attr, QJsonObject &obj);
    QJsonObject &deserializeAttribute(quint8 type, const QByteArray &buf, QJsonObject &obj);
    TimelineItem & parseLayout(TimelineItem &timelineItem, const QJsonObject &layout);
    TimelineItem & parseActions(TimelineItem &timelineItem, const QJsonArray &actions);

    QString m_timelineStoragePath;
    QHash<QString,quint8> m_layouts;
    QHash<QString,qint32> m_resources;
    QHash<QString,Attr> m_attributes;

    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // TIMELINEMANAGER_H
