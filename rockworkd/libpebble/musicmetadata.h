#ifndef MUSICMETADATA_H
#define MUSICMETADATA_H

#include <QString>

class MusicMetaData
{
public:
    MusicMetaData();
    MusicMetaData(const QString &artist, const QString &album, const QString &title, const int duration);

    QString artist;
    QString album;
    QString title;
    int duration;
};

#endif // MUSICMETADATA_H
