#ifndef MUSICMETADATA_H
#define MUSICMETADATA_H

#include <QString>

class MusicMetaData
{
public:
    MusicMetaData();
    MusicMetaData(const QString &artist, const QString &album, const QString &title);

    QString artist;
    QString album;
    QString title;
};

#endif // MUSICMETADATA_H
