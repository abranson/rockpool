#ifndef FIRWAREDOWNLOADER_H
#define FIRWAREDOWNLOADER_H

#include <QObject>

#include "watchconnection.h"

class Pebble;
class QNetworkAccessManager;

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
