/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "iconcolor.h"
#include "libpebble3d-icon-color.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace {

const int MaxDesktopFiles = 2048;
const int MaxIconEntries = 8192;
const qint64 MaxInputBytes = 8 * 1024 * 1024;
const qint64 MaxInputPixels = 4096LL * 4096LL;

QString appIdForSource(const QString &source)
{
    if (source == QLatin1String("commhistoryd"))
        return QLatin1String("jolla-messages");
    if (source == QLatin1String("messageserver5"))
        return QLatin1String("jolla-email");
    return source;
}

QString iconFromDesktop(const QString &path, const QString &source,
                        const QString &appId)
{
    QFile file(path);
    if (QFileInfo(path).size() > 64 * 1024 || !file.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray data = file.read(64 * 1024 + 1);
    if (data.size() > 64 * 1024)
        return QString();

    const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    const QString base = QFileInfo(path).fileName();
    bool inEntry = false;
    bool hidden = false;
    bool match = base == appId + QLatin1String(".desktop")
            || base == source + QLatin1String(".desktop");
    QString icon;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == QLatin1String("Hidden") && value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            hidden = true;
        else if (key == QLatin1String("Icon"))
            icon = value;
        else if ((key == QLatin1String("X-apkd-packageName") || key == QLatin1String("StartupWMClass"))
                 && (value == appId || value == source))
            match = true;
    }
    return match && !hidden ? icon : QString();
}

QStringList iconRoots()
{
    QStringList roots;
    const QStringList dataLocations = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (int i = 0; i < dataLocations.size(); ++i) {
        roots << dataLocations.at(i) + QLatin1String("/apkd-bridge/launcherIcon")
              << dataLocations.at(i) + QLatin1String("/icons");
    }
    roots << QLatin1String("/usr/share/themes/sailfish-default")
          << QLatin1String("/usr/share/themes/Sailfish")
          << QLatin1String("/usr/share/themes/sailfish")
          << QLatin1String("/usr/share/themes/Sailfish-dark")
          << QLatin1String("/usr/share/themes/Sailfish-ambience")
          << QLatin1String("/usr/share/themes/sailfish/ambience");
    roots.removeDuplicates();
    return roots;
}

QString findIcon(const QString &name)
{
    if (name.isEmpty())
        return QString();
    if (name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1String("file://"))) {
        QString path = name;
        if (name.startsWith(QLatin1String("file://"))) {
            const QUrl url = QUrl::fromEncoded(name.toUtf8());
            if (!url.isLocalFile() || !url.host().isEmpty())
                return QString();
            path = url.toLocalFile();
        }
        const QFileInfo fi(path);
        const QString canonical = fi.canonicalFilePath();
        const QStringList roots = iconRoots();
        for (int r = 0; r < roots.size(); ++r) {
            const QString rootPath = QFileInfo(roots.at(r)).canonicalFilePath();
            if (!canonical.isEmpty() && !rootPath.isEmpty()
                    && canonical.startsWith(rootPath + QLatin1Char('/'))
                    && fi.size() > 0 && fi.size() <= MaxInputBytes)
                return canonical;
        }
        return QString();
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1String("..")))
        return QString();
    QStringList names;
    names << name;
    if (!name.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
            && !name.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        names << name + QLatin1String(".png") << name + QLatin1String(".svg");

    const QStringList roots = iconRoots();
    int visited = 0;
    for (int r = 0; r < roots.size(); ++r) {
        const QString rootPath = QFileInfo(roots.at(r)).canonicalFilePath();
        if (rootPath.isEmpty())
            continue;
        QDirIterator it(roots.at(r), QDir::AllEntries | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && visited < MaxIconEntries) {
            const QString path = it.next();
            ++visited;
            const QFileInfo fi(path);
            if (!fi.isFile() || !names.contains(fi.fileName())
                    || fi.size() <= 0 || fi.size() > MaxInputBytes)
                continue;
            const QString canonical = fi.canonicalFilePath();
            if (!canonical.isEmpty() && canonical.startsWith(rootPath + QLatin1Char('/')))
                return canonical;
        }
        if (visited >= MaxIconEntries)
            break;
    }
    return QString();
}

bool safeSvg(const QByteArray &bytes)
{
    const QByteArray lower = bytes.toLower();
    if (lower.contains("<!doctype") || lower.contains("<!entity")
            || lower.contains("@import") || lower.contains("xml:base"))
        return false;
    const QString svg = QString::fromUtf8(bytes);
    const QRegularExpression references(
        "(?:href\\s*=\\s*[\"']([^\"']*)[\"']"
        "|url\\(\\s*[\"']([^\"']*)[\"']\\s*\\)"
        "|url\\(\\s*([^\\s)]+)\\s*\\))",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = references.globalMatch(svg);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString target = !match.captured(1).isEmpty() ? match.captured(1)
                : (!match.captured(2).isEmpty() ? match.captured(2) : match.captured(3));
        if (!target.startsWith(QLatin1Char('#')))
            return false;
    }
    return true;
}

bool representativeColor(const QString &path, QRgb *result)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MaxInputBytes)
        return false;
    const QByteArray bytes = file.read(MaxInputBytes + 1);
    if (bytes.size() <= 0 || bytes.size() > MaxInputBytes)
        return false;
    if ((path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
         || bytes.toLower().contains("<svg")) && !safeSvg(bytes))
        return false;

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0
            || qint64(size.width()) * size.height() > MaxInputPixels)
        return false;
    reader.setScaledSize(size.scaled(32, 32, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull() || image.width() <= 0 || image.height() <= 0
            || qint64(image.width()) * image.height() > MaxInputPixels)
        return false;
    if (image.width() > 32 || image.height() > 32)
        image = image.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);

    const int32_t color = libpebble3d_icon_color(
                reinterpret_cast<const uint32_t *>(image.constBits()),
                image.width(), image.height(), image.bytesPerLine() / sizeof(uint32_t));
    if (color < 0)
        return false;
    *result = static_cast<QRgb>(color);
    return true;
}

}

int32_t iconColor(const QString &sourceName, const QString &fallback)
{
    QString source = sourceName;
    if (source.endsWith(QLatin1String(".desktop")))
        source.chop(8);
    if (source.isEmpty() || source.contains(QLatin1Char('/'))
            || source.contains(QLatin1Char('\n')) || fallback.contains(QLatin1Char('\n')))
        return -1;

    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    dirs.removeDuplicates();
    const QString appId = appIdForSource(source);
    QString desktop;
    int visited = 0;
    for (int d = 0; d < dirs.size() && visited < MaxDesktopFiles; ++d) {
        QDirIterator it(dirs.at(d), QStringList() << QLatin1String("*.desktop"),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && visited < MaxDesktopFiles) {
            const QString path = it.next();
            ++visited;
            desktop = iconFromDesktop(path, source, appId);
            if (!desktop.isEmpty())
                break;
        }
        if (!desktop.isEmpty())
            break;
    }
    QString icon = findIcon(desktop);
    if (icon.isEmpty())
        icon = findIcon(fallback);
    QRgb color;
    if (icon.isEmpty() || !representativeColor(icon, &color))
        return -1;
    return static_cast<int32_t>(color & 0xffffff);
}
