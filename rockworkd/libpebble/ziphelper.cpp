#include "ziphelper.h"

#include <QFileInfo>
#include <QDebug>
#include <QDir>

#include <quazip/quazipfile.h>
#include <quazip/quazip.h>

ZipHelper::ZipHelper()
{

}

bool ZipHelper::unpackArchive(const QString &archiveFilename, const QString &targetDir)
{
    QuaZip zipFile(archiveFilename);
    if (!zipFile.open(QuaZip::mdUnzip)) {
        qWarning() << "Failed to open zip file" << zipFile.getZipName();
        return false;
    }

    foreach (const QuaZipFileInfo &fi, zipFile.getFileInfoList()) {
        QuaZipFile f(archiveFilename, fi.name);
        if (!f.open(QFile::ReadOnly)) {
            qWarning() << "could not extract file" << fi.name;
            return false;
        }
        if (fi.name.endsWith("/")) {
            qDebug() << "skipping" << fi.name;
            continue;
        }
        qDebug() << "Inflating:" << fi.name;
        QFileInfo dirInfo(targetDir + "/" + fi.name);
        if (!dirInfo.absoluteDir().exists() && !dirInfo.absoluteDir().mkpath(dirInfo.absolutePath())) {
            qWarning() << "Error creating target dir" << dirInfo.absoluteDir();
            return false;
        }
        QFile of(targetDir + "/" + fi.name);
        if (!of.open(QFile::WriteOnly | QFile::Truncate)) {
            qWarning() << "Could not open output file for writing" << fi.name;
            f.close();
            return false;
        }
        of.write(f.readAll());
        f.close();
        of.close();
    }
    return true;
}

bool ZipHelper::packArchive(const QString &archiveFilename, const QString &sourceDir)
{
    QuaZip zip(archiveFilename);
    if (!zip.open(QuaZip::mdCreate)){
        qWarning() << "Error creating zip file";
        return false;
    }

    QDir dir(sourceDir);
    QuaZipFile outfile(&zip);

    foreach (const QFileInfo &fi, dir.entryInfoList()) {
        if (!fi.isFile()) {
            continue;
        }
        qDebug() << "have file" << fi.absoluteFilePath();
        QuaZipNewInfo newInfo(fi.fileName(), fi.absoluteFilePath());

        if (!outfile.open(QFile::WriteOnly, newInfo)) {
            qWarning() << "Error opening zipfile for writing";
            zip.close();
            return false;
        }

        QFile sourceFile(fi.absoluteFilePath());
        if (!sourceFile.open(QFile::ReadOnly)) {
            qWarning() << "Error opening log file for reading" << fi.absoluteFilePath();
            outfile.close();
            zip.close();
            return false;
        }
        outfile.write(sourceFile.readAll());
        outfile.close();
        sourceFile.close();

    }
    outfile.close();
    zip.close();
    return true;
}
