#include "musicmetadata.h"

MusicMetaData::MusicMetaData()
{

}

MusicMetaData::MusicMetaData(const QString &artist, const QString &album, const QString &title):
    artist(artist),
    album(album),
    title(title)
{

}

MusicPlayState::MusicPlayState()
{

}

MusicPlayState::MusicPlayState(const State state, const qint32 trackPosition, const qint32 playRate, const Shuffle shuffle, const Repeat repeat):
    state(state),
    trackPosition(trackPosition),
    playRate(playRate),
    shuffle(shuffle),
    repeat(repeat)
{

}
