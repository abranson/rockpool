#include "musicendpoint.h"
#include "pebble.h"
#include "core.h"
#include "platforminterface.h"
#include "watchdatawriter.h"
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
    qDebug() << "setMusicMetadata";
    m_metaData = metaData;
    writeMetadata();
}

MusicPlayState MusicEndpoint::getMusicPlayState() {
    return Core::instance()->platform()->getMusicPlayState();
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
    QByteArray res = m_watchConnection->buildMessageData(MusicControlUpdateCurrentTrack, tmp);
    WatchDataWriter writer(&res); // Used to skip these if not present in the metadata, but the watch didn't clear duration data
    writer.writeLE(m_metaData.duration);
    writer.writeLE(m_metaData.trackCount);
    writer.writeLE(m_metaData.currentTrack);

    m_watchConnection->writeToPebble(WatchConnection::EndpointMusicControl, res);
}

void MusicEndpoint::writePlayState(const MusicPlayState &playState) {
    qDebug() << "Writing playstate" << playState.state << " Position: " << playState.trackPosition;
    if (!m_watchConnection->isConnected()) {
        return;
    }
    QByteArray res;
    WatchDataWriter writer(&res);
    res.append(MusicControlUpdatePlayStateInfo); // MusicControlUpdatePlayStateInfo
    res.append(playState.state);
    writer.writeLE(playState.trackPosition);
    writer.writeLE(playState.playRate);
    res.append(playState.shuffle);
    res.append(playState.repeat);

    m_watchConnection->writeToPebble(WatchConnection::EndpointMusicControl, res);
}

void MusicEndpoint::handleMessage(const QByteArray &data)
{
    qDebug() << "Music control : " << data.toHex().toInt();
    MusicControlButton controlButton;
    switch (data.toHex().toInt()) {
    case MusicControlPlayPause:
        controlButton = MusicControlPlayPause;
        break;
    case MusicControlPause:
        controlButton = MusicControlPause;
        break;
    case MusicControlPlay:
        controlButton = MusicControlPlay;
        break;
    case MusicControlNextTrack:
        controlButton = MusicControlNextTrack;
        break;
    case MusicControlPreviousTrack:
        controlButton = MusicControlPreviousTrack;
        break;
    case MusicControlVolumeUp:
        controlButton = MusicControlVolumeUp;
        break;
    case MusicControlVolumeDown:
        controlButton = MusicControlVolumeDown;
        break;
    case MusicControlGetCurrentTrack: // MusicControlGetCurrentTrack
        writeMetadata();
        writePlayState(getMusicPlayState());
        return;
    default:
        qWarning() << "Unhandled music control button pressed:" << data.toHex();
        return;
    }
    emit musicControlPressed(controlButton);
}

