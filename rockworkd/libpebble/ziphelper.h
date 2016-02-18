#ifndef ZIPHELPER_H
#define ZIPHELPER_H

#include <QString>

class ZipHelper
{
public:
    ZipHelper();

    static bool unpackArchive(const QString &archiveFilename, const QString &targetDir);
    static bool packArchive(const QString &archiveFilename, const QString &sourceDir);
};

#endif // ZIPHELPER_H
