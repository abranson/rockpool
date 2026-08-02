#ifndef ROCKPOOLACCOUNT_H
#define ROCKPOOLACCOUNT_H

#include <QDBusAbstractInterface>
#include <QObject>
#include <QSet>
#include <QVariantMap>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class RockpoolOperation;

class RockpoolPropertiesInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit RockpoolPropertiesInterface(QObject *parent = 0);

signals:
    void PropertiesChanged(const QString &interfaceName,
                           const QVariantMap &changed,
                           const QStringList &invalidated);
};

class RockpoolAccountInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit RockpoolAccountInterface(QObject *parent = 0);
};

/**
 * Process-wide authoritative view of org.rockpool.Account1.
 *
 * This object is owned by Pebbles rather than a compatibility watch. Primary
 * account operations therefore survive org.rockwork owner changes and all
 * watch views share one latest-write epoch and pending/error state.
 */
class RockpoolAccount : public QObject
{
    Q_OBJECT

public:
    explicit RockpoolAccount(QObject *parent = 0);

    bool authenticated() const;
    QString name() const;
    QString email() const;
    bool tokenPending() const;
    QString tokenError() const;
    static QString oauthTokenFromCallback(const QString &callbackUrl);

public slots:
    void setOAuthToken(const QString &token);

signals:
    void authenticatedChanged();
    void nameChanged();
    void emailChanged();
    void tokenPendingChanged();
    void tokenErrorChanged();

private:
    void resetInterfaces();
    void refreshProperties();
    void applyProperties(const QVariantMap &properties);
    void propertiesChanged(const QString &interfaceName,
                           const QVariantMap &changed,
                           const QStringList &invalidated);
    void propertiesReplyFinished(QDBusPendingCallWatcher *watcher);
    void tokenReplyFinished(QDBusPendingCallWatcher *watcher);
    void tokenOperationCompleted(RockpoolOperation *operation,
                                 quint64 tokenEpoch,
                                 quint64 ownerEpoch,
                                 bool success);
    void finishTokenAttempt(quint64 tokenEpoch, quint64 ownerEpoch,
                            const QString &error);
    void setTokenPending(bool pending);
    void setTokenError(const QString &error);
    void serviceOwnerChanged(const QString &service,
                             const QString &oldOwner,
                             const QString &newOwner);

    RockpoolAccountInterface *m_interface = 0;
    RockpoolPropertiesInterface *m_properties = 0;
    QDBusServiceWatcher *m_serviceWatcher;
    bool m_authenticated = false;
    QString m_name;
    QString m_email;
    bool m_tokenPending = false;
    QString m_tokenError;
    QSet<RockpoolOperation *> m_tokenOperations;
    quint64 m_ownerEpoch = 0;
    quint64 m_propertiesEpoch = 0;
    quint64 m_tokenEpoch = 0;
};

#endif
