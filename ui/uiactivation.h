#ifndef UIACTIVATION_H
#define UIACTIVATION_H

#include <QObject>
#include <QDBusContext>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusMessage>
#include <QRegularExpression>
#include <QUuid>

class UiActivation : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.UI")
    Q_PROPERTY(QString firmwareAddress READ firmwareAddress NOTIFY firmwareAddressChanged)
    Q_PROPERTY(QString appUpdateAddress READ appUpdateAddress NOTIFY appUpdateRequestChanged)
    Q_PROPERTY(QString appUpdateUuid READ appUpdateUuid NOTIFY appUpdateRequestChanged)
    Q_PROPERTY(bool watchfaceUpdate READ watchfaceUpdate NOTIFY appUpdateRequestChanged)
    Q_PROPERTY(QString installReviewToken READ installReviewToken NOTIFY installReviewChanged)
    Q_PROPERTY(QString installReviewJson READ installReviewJson NOTIFY installReviewChanged)
    Q_PROPERTY(QString installReviewOwner READ installReviewOwner NOTIFY installReviewChanged)
public:
    explicit UiActivation(QObject *parent = nullptr) : QObject(parent)
    {
        auto *interface = QDBusConnection::sessionBus().interface();
        connect(interface, &QDBusConnectionInterface::serviceOwnerChanged, this,
                [this](const QString &name, const QString &oldOwner, const QString &newOwner) {
            if (name == QStringLiteral("org.rockpool") && !oldOwner.isEmpty()
                    && oldOwner != newOwner) {
                if (m_installReviewOwner == oldOwner) {
                    m_installReviewToken.clear();
                    m_installReviewJson.clear();
                    m_installReviewOwner.clear();
                    emit installReviewChanged();
                }
                emit installReviewDaemonChanged();
            }
        });
    }
    QString firmwareAddress() const { return m_firmwareAddress; }
    QString appUpdateAddress() const { return m_appUpdateAddress; }
    QString appUpdateUuid() const { return m_appUpdateUuid; }
    bool watchfaceUpdate() const { return m_watchfaceUpdate; }
    QString installReviewToken() const { return m_installReviewToken; }
    QString installReviewJson() const { return m_installReviewJson; }
    QString installReviewOwner() const { return m_installReviewOwner; }
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

    Q_INVOKABLE void consumeInstallReview()
    {
        m_installReviewToken.clear();
        m_installReviewJson.clear();
        m_installReviewOwner.clear();
        emit installReviewChanged();
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

    bool OpenInstallReview(const QString &token, const QString &reviewJson)
    {
        if (!calledFromDBus() || token.isEmpty() || reviewJson.isEmpty())
            return false;
        const QDBusMessage incoming = message();
        const QString sender = incoming.service();
        if (sender.isEmpty())
            return false;
        const QDBusConnection incomingConnection = connection();
        QDBusMessage call = QDBusMessage::createMethodCall(
                    QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("/org/freedesktop/DBus"),
                    QStringLiteral("org.freedesktop.DBus"),
                    QStringLiteral("GetNameOwner"));
        call << QStringLiteral("org.rockpool");
        setDelayedReply(true);
        auto *watcher = new QDBusPendingCallWatcher(incomingConnection.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, incomingConnection, incoming, sender, token, reviewJson]() {
            const QDBusMessage reply = watcher->reply();
            watcher->deleteLater();
            const bool valid = reply.type() == QDBusMessage::ReplyMessage
                    && reply.arguments().size() == 1
                    && reply.arguments().first().toString() == sender;
            incomingConnection.send(incoming.createReply(QVariant(valid)));
            if (!valid)
                return;
            m_installReviewToken = token;
            m_installReviewJson = reviewJson;
            m_installReviewOwner = sender;
            emit installReviewChanged();
            emit installReviewRequested();
        });
        return true;
    }

signals:
    void firmwareAddressChanged();
    void firmwareRequested();
    void appUpdateRequestChanged();
    void appUpdatesRequested();
    void installReviewChanged();
    void installReviewRequested();
    void installReviewDaemonChanged();

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
    QString m_installReviewToken;
    QString m_installReviewJson;
    QString m_installReviewOwner;
};

#endif
