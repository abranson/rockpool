#ifndef SERVICECONTROL_H
#define SERVICECONTROL_H

#include <QDBusAbstractInterface>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;

static const QString ROCKPOOLD_SYSTEMD_UNIT("libpebble3d.service");

class SystemdManagerInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit SystemdManagerInterface(QObject *parent = 0);
};

class SystemdUnitPropertiesInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit SystemdUnitPropertiesInterface(const QString &path, QObject *parent = 0);

signals:
    void PropertiesChanged(const QString &interfaceName,
                           const QVariantMap &changed,
                           const QStringList &invalidated);
};

class ServiceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool serviceRunning READ serviceRunning WRITE setServiceRunning NOTIFY serviceRunningChanged)

public:
    explicit ServiceControl(QObject *parent = 0);

    bool ready() const;
    bool serviceRunning() const;
    bool setServiceRunning(bool running);
    Q_INVOKABLE bool startService();
    Q_INVOKABLE bool stopService();
    Q_INVOKABLE bool restartService();

signals:
    void readyChanged();
    void serviceRunningChanged();

private:
    enum Operation {
        NoOperation,
        StartOperation,
        StopOperation,
        RestartOperation
    };

    void initialize();
    void requestLoadUnit(quint64 serviceEpoch);
    void getUnitProperties();
    void applyUnitProperties(const QVariantMap &properties);
    void setReady(bool ready);
    bool enqueueOperation(Operation operation);
    void beginOperation(Operation operation);
    void dispatchOperationStep();
    void finishOperation();

    void initializationReplyFinished(QDBusPendingCallWatcher *watcher);
    void unitPropertiesReplyFinished(QDBusPendingCallWatcher *watcher);
    void operationReplyFinished(QDBusPendingCallWatcher *watcher);
    void onPropertiesChanged(const QString &interfaceName,
                             const QVariantMap &changed,
                             const QStringList &invalidated);
    void serviceOwnerChanged(const QString &service,
                             const QString &oldOwner,
                             const QString &newOwner);

private:
    SystemdManagerInterface *m_systemd;
    SystemdUnitPropertiesInterface *m_unitPropertiesInterface = 0;
    QDBusServiceWatcher *m_serviceWatcher;
    QDBusObjectPath m_unitPath;
    QVariantMap m_unitProperties;
    bool m_ready = false;
    quint64 m_serviceEpoch = 0;
    quint64 m_propertiesEpoch = 0;
    quint64 m_operationEpoch = 0;
    Operation m_activeOperation = NoOperation;
    Operation m_queuedOperation = NoOperation;
    int m_operationStep = 0;
};

#endif // SERVICECONTROL_H
