#include "appglances.h"
#include "blobdb.h"
#include "pebble.h"

/**
 * @brief AppGlances::AppGlances
 * @param pebble
 * @param connection
 */
AppGlances::AppGlances(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_connection(connection)
{
    connect(pebble->blobdb(), &BlobDB::blobCommandResult, this, &AppGlances::blobdbAckHandler);
}

void AppGlances::reloadAppGlances(const QUuid &app, const QList<Slice> &slices, Callback cb)
{
    m_glances.insert(app,new Glance(app,1,QDateTime::currentDateTimeUtc(),slices,cb));
    m_pebble->blobdb()->insert(BlobDB::BlobDBIdAppGlance,*m_glances[app]);
}

void AppGlances::blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack)
{
    if(db!=BlobDB::BlobDBIdAppGlance) return;
    switch(cmd) {
    case BlobDB::OperationDelete:
    case BlobDB::OperationInsert:
        break;
    case BlobDB::OperationClear:
        m_glances.clear();
    default:
        return;
    }

    QUuid app = QUuid::fromRfc4122(key);
    if(app.isNull()) {
        qWarning() << "Wrong App UUID key format" << cmd << ack << key.toHex();
        return;
    } else if(m_glances.contains(app)) {
        Glance *g = m_glances.take(app);
        g->blobComplete(cmd,ack);
        emit appGlanceReloaded(app,cmd,ack);
        delete g;
    } else {
        qWarning() << "Result for non-exiting glance" << app << cmd << ack;
    }
}

/**
 * @brief AppGlances::Glance::Glance
 * @param app
 * @param vers
 * @param ts
 * @param sls
 * @param cb
 */
AppGlances::Glance::Glance(const QUuid &app, quint8 vers, const QDateTime &ts, const QList<Slice> sls, Callback cb):
    BlobDbItem(),
    app_uuid(app),
    version(vers),
    slices(sls),
    complete(cb)
{
    timestamp = ts.toUTC().toTime_t();
}

QByteArray AppGlances::Glance::itemKey() const
{
    return app_uuid.toRfc4122();
}
QByteArray AppGlances::Glance::serialize() const
{
    QByteArray ret;
    WatchDataWriter w(&ret);
    ret.append(version);
    w.writeLE<quint32>(timestamp);
    foreach (const Slice &s, slices) {
        ret.append(s.serialize());
    }
    return ret;
}

/**
 * @brief AppGlances::Slice::Slice
 * @param type
 * @param attributes
 */
AppGlances::Slice::Slice(Type type, const QList<TimelineAttribute> &attributes):
    PebblePacket(),
    type(type),
    atts(attributes)
{
    att_count = attributes.count();
}

AppGlances::Slice::Slice(const QDateTime &expire, Type type, const QList<TimelineAttribute> &attributes):
    Slice(type,attributes)
{
    atts.prepend(TimelineAttribute(37,expire.toUTC().toTime_t()));
    att_count++;
}

QByteArray AppGlances::Slice::serialize() const
{
    QByteArray ret,pkt;
    WatchDataWriter w(&ret);
    pkt.append(type);
    pkt.append(att_count);
    for(int i=0;i<att_count;i++)
        pkt.append(atts.at(i).serialize());
    w.writeLE<quint16>(pkt.length()+2); // pkt + uint16
    ret.append(pkt);
    return ret;
}
