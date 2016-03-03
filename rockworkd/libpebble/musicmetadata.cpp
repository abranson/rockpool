#include "musicmetadata.h"

MusicMetaData::MusicMetaData()
{

}

MusicMetaData::MusicMetaData(const QString &artist, const QString &album, const QString &title, const int duration):
    artist(artist),
    album(album),
    title(title),
    duration(duration)
{

}
