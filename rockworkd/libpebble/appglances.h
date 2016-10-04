#ifndef APPGLANCES_H
#define APPGLANCES_H

#include <QObject>

#include "watchconnection.h"
#include "timelineitem.h"

class Pebble;

class AppGlances: public QObject
{
    Q_OBJECT
public:
    AppGlances(Pebble *pebble, WatchConnection *connection);

    enum Type {
        TypeIconSubtitle = 0
    };

    class Slice : public PebblePacket {
    public:
        Slice(Type type, const QList<TimelineAttribute> &attributes);
        Slice(const QDateTime &expire, Type type, const QList<TimelineAttribute> &attributes);
        QByteArray serialize() const;
    private:
        quint8 type;
        quint8 att_count;
        QList<TimelineAttribute> atts;
    };
    //class SliceIconSubtitle : public Slice {
    //public:
    //    SliceIconSubtitle(const QDateTime &expire, quint32 icon, const QString &subtitle_template);
    //};

    typedef std::function<void(int,int)> Callback;

    class Glance: public BlobDbItem {
    public:
        Glance(const QUuid &app, quint8 vers, const QDateTime &ts, const QList<Slice> sls, Callback cb=Callback());
        QByteArray serialize() const;
        QByteArray itemKey() const;
        void blobComplete(int cmd, int ack) {if(complete)complete(cmd,ack);}

    private:
        const QUuid &app_uuid;
        quint8 version;
        time_t timestamp;
        QList<Slice> slices;
        Callback complete;
    };

signals:
    void appGlanceReloaded(const QUuid &app,int cmd, int ack);

public slots:
    void reloadAppGlances(const QUuid &uuid, const QList<Slice> &slices, Callback cb=Callback());

private slots:
    void blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack);

private:
    QHash<QUuid,Glance*> m_glances;
    Pebble *m_pebble;
    WatchConnection *m_connection;
};

#endif // APPGLANCES_H
