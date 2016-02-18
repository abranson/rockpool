#include "musicendpoint.h"
#include "pebble.h"
#include "watchconnection.h"

#include <QDebug>

MusicEndpoint::MusicEndpoint(Pebble *pebble, WatchConnection *connection):
    QObject(pebble),
    m_pebble(pebble),
    m_watchConnection(connection)
{
    m_watchConnection->registerEndpointHandler(WatchConnection::EndpointMusicControl, this, "handleMessage");
}

void MusicEndpoint::setMusicMetadata(const MusicMetaData &metaData)
{
    m_metaData = metaData;
    writeMetadata();
}

void MusicEndpoint::writeMetadata()
{
    if (!m_watchConnection->isConnected()) {
        return;
    }
    QStringList tmp;
    tmp.append(m_metaData.artist.left(30));
    tmp.append(m_metaData.album.left(30));
    tmp.append(m_metaData.title.left(30));
    QByteArray res = m_watchConnection->buildMessageData(16, tmp); // Not yet sure what the 16 is about :/

    m_watchConnection->writeToPebble(WatchConnection::EndpointMusicControl, res);
}

void MusicEndpoint::handleMessage(const QByteArray &data)
{
    MusicControlButton controlButton;
    switch (data.toHex().toInt()) {
    case 0x01:
        controlButton = MusicControlPlayPause;
        break;
    case 0x04:
        controlButton = MusicControlSkipNext;
        break;
    case 0x05:
        controlButton = MusicControlSkipBack;
        break;
    case 0x06:
        controlButton = MusicControlVolumeUp;
        break;
    case 0x07:
        controlButton = MusicControlVolumeDown;
        break;
    case 0x08:
        writeMetadata();
        return;
    default:
        qWarning() << "Unhandled music control button pressed:" << data.toHex();
        return;
    }
    emit musicControlPressed(controlButton);
}

