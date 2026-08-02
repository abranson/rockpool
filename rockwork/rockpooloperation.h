#ifndef ROCKPOOLOPERATION_H
#define ROCKPOOLOPERATION_H

#include <QDBusAbstractInterface>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;

class RockpoolOperationInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit RockpoolOperationInterface(const QString &path, QObject *parent = 0);

signals:
    void Completed(bool success);
};

class RockpoolOperationPropertiesInterface : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    explicit RockpoolOperationPropertiesInterface(const QString &path,
                                                   QObject *parent = 0);

signals:
    void PropertiesChanged(const QString &interfaceName,
                           const QVariantMap &changed,
                           const QStringList &invalidated);
};

/**
 * Authoritative client-side view of one org.rockpool.Operation1 object.
 *
 * Operations can finish before the D-Bus method returning their path reaches
 * the caller.  This watcher subscribes first and then reads all properties, so
 * a terminal snapshot and a later Completed signal are handled identically.
 */
class RockpoolOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY finishedChanged)
    Q_PROPERTY(bool success READ success NOTIFY finishedChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)

public:
    explicit RockpoolOperation(const QDBusObjectPath &path, QObject *parent = 0);

    QString path() const;
    bool ready() const;
    bool available() const;
    bool finished() const;
    bool success() const;
    QString kind() const;
    QString state() const;
    double progress() const;
    QVariantMap result() const;
    QString error() const;
    QString errorDetail() const;

    Q_INVOKABLE void cancel();

signals:
    void changed();
    void readyChanged();
    void availableChanged();
    void finishedChanged();
    void completed(bool success);

private slots:
    void operationCompleted(bool success);
    void operationPropertiesChanged(const QString &interfaceName,
                                    const QVariantMap &changed,
                                    const QStringList &invalidated);
    void snapshotReplyFinished(QDBusPendingCallWatcher *watcher);
    void cancelReplyFinished(QDBusPendingCallWatcher *watcher);
    void serviceOwnerChanged(const QString &service,
                             const QString &oldOwner,
                             const QString &newOwner);

private:
    void requestSnapshot();
    bool applySnapshot(const QVariantMap &properties);
    void applyChanges(const QVariantMap &properties);
    void setReady(bool ready);
    void completeIfTerminal();
    void makeUnavailable(const QString &error, const QString &detail);
    bool isTerminalState(const QString &state) const;

    QString m_path;
    RockpoolOperationInterface *m_operation;
    RockpoolOperationPropertiesInterface *m_properties;
    QDBusServiceWatcher *m_serviceWatcher;
    bool m_ready = false;
    bool m_available = true;
    bool m_finished = false;
    bool m_success = false;
    bool m_cancelRequested = false;
    QString m_kind;
    QString m_state;
    double m_progress = 0.0;
    QVariantMap m_result;
    QString m_error;
    QString m_errorDetail;
    quint64 m_snapshotEpoch = 0;
    int m_snapshotFailures = 0;
    bool m_snapshotInFlight = false;
    bool m_snapshotDirty = false;
};

#endif
