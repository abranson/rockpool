#ifndef FIRWAREDOWNLOADER_H
#define FIRWAREDOWNLOADER_H

#include <QObject>

#include "watchconnection.h"
#include <QDebug>
class Pebble;
class QNetworkAccessManager;

class FirmwareVersion
{
public:
    FirmwareVersion(const QString &versionString)
    {
        QString tmp = versionString;
        if (!tmp.startsWith("v")) {
            qWarning() << "firmware string expected to be of format vX.Y.Z but it is" << versionString;
            return;
        }
        tmp = tmp.right(tmp.length() - 1);
        QStringList fields = tmp.split(".");
        if (fields.length() != 3) {
            qWarning() << "firmware string expected to be of format vX.Y.Z but it is" << versionString;
            return;
        }
        major = fields.at(0).toInt();
        minor = fields.at(1).toInt();
        patch = fields.at(2).toInt();
    }

    bool operator>(const FirmwareVersion &other) {
        if (major > other.major) {
            return true;
        }
        if (major == other.major && minor > other.minor) {
            return true;
        }
        if (major == other.major && minor == other.minor && patch > other.patch) {
            return true;
        }
        return false;
    }

    bool isValid() const {
        return major > -1 && minor > -1 && patch > -1;
    }

    int major = -1;
    int minor = -1;
    int patch = -1;

};

class FirmwareDownloader : public QObject
{
    Q_OBJECT
public:
    explicit FirmwareDownloader(Pebble *pebble, WatchConnection *connection);

    bool updateAvailable() const;
    QString candidateVersion() const;
    QString releaseNotes() const;
    QString url() const;

    bool upgrading() const;

public slots:
    void checkForNewFirmware();
    void performUpgrade();

signals:
    void updateAvailableChanged();
    void upgradingChanged();

private slots:
    void systemMessageReceived(const QByteArray &data);

private:
    QNetworkAccessManager *m_nam;
    Pebble *m_pebble;
    WatchConnection *m_connection;

    bool m_updateAvailable = false;
    QString m_candidateVersion;
    QString m_releaseNotes;
    QString m_url;
    QByteArray m_hash;

    bool m_upgradeInProgress = false;
    QString m_bundlePath;
};

#endif // FIRWAREDOWNLOADER_H
