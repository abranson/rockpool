#ifndef MUSICENDPOINT_H
#define MUSICENDPOINT_H

#include "musicmetadata.h"
#include "enums.h"

#include <QObject>

class Pebble;
class WatchConnection;

class MusicEndpoint : public QObject
{
    Q_OBJECT
public:
    explicit MusicEndpoint(Pebble *pebble, WatchConnection *connection);

public slots:
    void setMusicMetadata(const MusicMetaData &metaData);

private slots:
    void handleMessage(const QByteArray &data);

signals:
    void musicControlPressed(MusicControlButton button);

private:
    void writeMetadata();

private:
    Pebble *m_pebble;
    WatchConnection *m_watchConnection;

    MusicMetaData m_metaData;
};

#endif // MUSICENDPOINT_H
