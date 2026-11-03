#ifndef UIACTIVATION_H
#define UIACTIVATION_H

#include <QObject>
#include <QRegularExpression>
#include <QUuid>

class UiActivation : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.UI")
    Q_PROPERTY(QString firmwareAddress READ firmwareAddress NOTIFY firmwareAddressChanged)
    Q_PROPERTY(QString appUpdateAddress READ appUpdateAddress NOTIFY appUpdateRequestChanged)
    Q_PROPERTY(QString appUpdateUuid READ appUpdateUuid NOTIFY appUpdateRequestChanged)
    Q_PROPERTY(bool watchfaceUpdate READ watchfaceUpdate NOTIFY appUpdateRequestChanged)
public:
    explicit UiActivation(QObject *parent = nullptr) : QObject(parent) {}
    QString firmwareAddress() const { return m_firmwareAddress; }
    QString appUpdateAddress() const { return m_appUpdateAddress; }
    QString appUpdateUuid() const { return m_appUpdateUuid; }
    bool watchfaceUpdate() const { return m_watchfaceUpdate; }
    Q_INVOKABLE void consumeAppUpdateRequest()
    {
        m_appUpdateAddress.clear();
        m_appUpdateUuid.clear();
        emit appUpdateRequestChanged();
    }
    Q_INVOKABLE void consumeFirmwareRequest()
    {
        m_firmwareAddress.clear();
        emit firmwareAddressChanged();
    }

public slots:
    void OpenFirmwareUpdate(const QString &address)
    {
        static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
        if (!pattern.match(address).hasMatch())
            return;
        consumeAppUpdateRequest();
        m_firmwareAddress = address.toUpper();
        emit firmwareAddressChanged();
        emit firmwareRequested();
    }
    void OpenWatchappUpdates(const QString &address) { openAppUpdates(address, false); }
    void OpenWatchfaceUpdates(const QString &address) { openAppUpdates(address, true); }
    void OpenAppUpdate(const QString &address, const QString &uuid)
    {
        const QUuid appId(uuid);
        if (!appId.isNull())
            openAppUpdates(address, false, appId.toString());
    }

signals:
    void firmwareAddressChanged();
    void firmwareRequested();
    void appUpdateRequestChanged();
    void appUpdatesRequested();

private:
    void openAppUpdates(const QString &address, bool watchface, const QString &uuid = QString())
    {
        static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
        if (!pattern.match(address).hasMatch())
            return;
        consumeFirmwareRequest();
        m_appUpdateAddress = address.toUpper();
        m_appUpdateUuid = uuid;
        m_watchfaceUpdate = watchface;
        emit appUpdateRequestChanged();
        emit appUpdatesRequested();
    }
    QString m_firmwareAddress;
    QString m_appUpdateAddress;
    QString m_appUpdateUuid;
    bool m_watchfaceUpdate = false;
};

#endif
