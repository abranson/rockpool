#include "rockpooloperation.h"

#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QTimer>
#include <QtGlobal>

namespace {
const char ROCKPOOL_SERVICE[] = "org.rockpool";
const char OPERATION_INTERFACE[] = "org.rockpool.Operation1";

QVariant unwrapDbusVariant(const QVariant &value)
{
    QVariant unwrapped = value;
    while (unwrapped.userType() == qMetaTypeId<QDBusVariant>()) {
        unwrapped = unwrapped.value<QDBusVariant>().variant();
    }
    return unwrapped;
}

bool decodeVariantMap(const QVariant &value, QVariantMap *map)
{
    const QVariant unwrapped = unwrapDbusVariant(value);
    if (unwrapped.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = unwrapped.value<QDBusArgument>();
        if (argument.currentSignature() != QStringLiteral("a{sv}")) {
            return false;
        }
        argument >> *map;
        return true;
    }
    if (unwrapped.type() == QVariant::Map) {
        *map = unwrapped.toMap();
        return true;
    }
    return false;
}

bool stringProperty(const QVariantMap &properties, const QString &name, QString *value)
{
    if (!properties.contains(name)) {
        return false;
    }
    const QVariant property = unwrapDbusVariant(properties.value(name));
    if (property.type() != QVariant::String) {
        return false;
    }
    *value = property.toString();
    return true;
}
}

RockpoolOperationInterface::RockpoolOperationInterface(const QString &path, QObject *parent):
    QDBusAbstractInterface(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        path,
        OPERATION_INTERFACE,
        QDBusConnection::sessionBus(),
        parent)
{
}

RockpoolOperationPropertiesInterface::RockpoolOperationPropertiesInterface(
        const QString &path, QObject *parent):
    QDBusAbstractInterface(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        path,
        "org.freedesktop.DBus.Properties",
        QDBusConnection::sessionBus(),
        parent)
{
}

RockpoolOperation::RockpoolOperation(const QDBusObjectPath &path, QObject *parent):
    QObject(parent),
    m_path(path.path()),
    m_operation(new RockpoolOperationInterface(m_path, this)),
    m_properties(new RockpoolOperationPropertiesInterface(m_path, this)),
    m_serviceWatcher(new QDBusServiceWatcher(
        QString::fromLatin1(ROCKPOOL_SERVICE),
        QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForOwnerChange,
        this))
{
    connect(m_operation, &RockpoolOperationInterface::Completed,
            this, &RockpoolOperation::operationCompleted);
    connect(m_properties, &RockpoolOperationPropertiesInterface::PropertiesChanged,
            this, &RockpoolOperation::operationPropertiesChanged);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &RockpoolOperation::serviceOwnerChanged);
    requestSnapshot();
}

QString RockpoolOperation::path() const
{
    return m_path;
}

bool RockpoolOperation::ready() const
{
    return m_ready;
}

bool RockpoolOperation::available() const
{
    return m_available;
}

bool RockpoolOperation::finished() const
{
    return m_finished;
}

bool RockpoolOperation::success() const
{
    return m_success;
}

QString RockpoolOperation::kind() const
{
    return m_kind;
}

QString RockpoolOperation::state() const
{
    return m_state;
}

double RockpoolOperation::progress() const
{
    return m_progress;
}

QVariantMap RockpoolOperation::result() const
{
    return m_result;
}

QString RockpoolOperation::error() const
{
    return m_error;
}

QString RockpoolOperation::errorDetail() const
{
    return m_errorDetail;
}

void RockpoolOperation::cancel()
{
    if (m_finished || !m_available || m_cancelRequested) {
        return;
    }
    m_cancelRequested = true;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_operation->asyncCall(QStringLiteral("Cancel")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &RockpoolOperation::cancelReplyFinished);
}

void RockpoolOperation::operationCompleted(bool success)
{
    Q_UNUSED(success)
    if (m_finished || !m_available) {
        return;
    }
    requestSnapshot();
}

void RockpoolOperation::operationPropertiesChanged(const QString &interfaceName,
                                                    const QVariantMap &changed,
                                                    const QStringList &invalidated)
{
    if (m_finished || !m_available ||
            interfaceName != QString::fromLatin1(OPERATION_INTERFACE)) {
        return;
    }

    applyChanges(changed);
    const QString state = unwrapDbusVariant(changed.value(QStringLiteral("State"))).toString();
    if (!m_ready || !invalidated.isEmpty() || isTerminalState(state)) {
        requestSnapshot();
    } else {
        ++m_snapshotEpoch;
    }
}

void RockpoolOperation::snapshotReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    const quint64 epoch = watcher->property("snapshotEpoch").toULongLong();
    watcher->deleteLater();
    m_snapshotInFlight = false;
    if (m_finished || !m_available || epoch != m_snapshotEpoch) {
        if (!m_finished && m_available && m_snapshotDirty) {
            requestSnapshot();
        }
        return;
    }
    m_snapshotDirty = false;

    if (reply.isError() || !applySnapshot(reply.value())) {
        // Operation1 terminal property changes are authoritative.  The daemon
        // may evict the object immediately after publishing them, so a failed
        // readback must not replace a known terminal result with an artificial
        // unavailable failure.
        if (isTerminalState(m_state)) {
            setReady(true);
            completeIfTerminal();
            return;
        }
        const QString error = reply.isError()
            ? reply.error().name() : QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs");
        const QString detail = reply.isError()
            ? reply.error().message() : QStringLiteral("invalid Operation1 property snapshot");
        if (++m_snapshotFailures < 2) {
            const quint64 retryEpoch = ++m_snapshotEpoch;
            QTimer::singleShot(250, this, [this, retryEpoch]() {
                if (!m_finished && m_available && retryEpoch == m_snapshotEpoch) {
                    requestSnapshot();
                }
            });
        } else {
            makeUnavailable(error, detail);
        }
        return;
    }

    m_snapshotFailures = 0;
    setReady(true);
    completeIfTerminal();
}

void RockpoolOperation::cancelReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();
    if (m_finished || !m_available) {
        return;
    }
    if (reply.isError()) {
        qWarning() << "Could not cancel Rockpool operation" << m_path << ':'
                   << reply.error().message();
    }
    requestSnapshot();
}

void RockpoolOperation::serviceOwnerChanged(const QString &service,
                                            const QString &oldOwner,
                                            const QString &newOwner)
{
    Q_UNUSED(service)
    Q_UNUSED(oldOwner)
    Q_UNUSED(newOwner)
    if (!m_finished) {
        ++m_snapshotEpoch;
        m_snapshotInFlight = false;
        m_snapshotDirty = false;
        makeUnavailable(QStringLiteral("org.freedesktop.DBus.Error.Disconnected"),
                        QStringLiteral("Rockpool service owner changed"));
    }
}

void RockpoolOperation::requestSnapshot()
{
    if (m_finished || !m_available) {
        return;
    }
    const quint64 epoch = ++m_snapshotEpoch;
    if (m_snapshotInFlight) {
        m_snapshotDirty = true;
        return;
    }
    m_snapshotInFlight = true;
    m_snapshotDirty = false;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_properties->asyncCall(QStringLiteral("GetAll"),
                                QString::fromLatin1(OPERATION_INTERFACE)),
        this);
    watcher->setProperty("snapshotEpoch", QVariant::fromValue<qulonglong>(epoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &RockpoolOperation::snapshotReplyFinished);
}

bool RockpoolOperation::applySnapshot(const QVariantMap &properties)
{
    QString kind;
    QString state;
    QString error;
    QString errorDetail;
    if (!stringProperty(properties, QStringLiteral("Kind"), &kind) ||
            !stringProperty(properties, QStringLiteral("State"), &state) ||
            !stringProperty(properties, QStringLiteral("Error"), &error) ||
            !stringProperty(properties, QStringLiteral("ErrorDetail"), &errorDetail) ||
            !(state == QStringLiteral("pending") || state == QStringLiteral("running") ||
              isTerminalState(state))) {
        return false;
    }

    bool progressValid = false;
    const QVariant progressValue = unwrapDbusVariant(
        properties.value(QStringLiteral("Progress")));
    const double progress = progressValue.toDouble(&progressValid);
    QVariantMap result;
    if (progressValue.type() != QVariant::Double || !progressValid ||
            !qIsFinite(progress) || progress < 0.0 || progress > 1.0 ||
            !decodeVariantMap(properties.value(QStringLiteral("Result")), &result)) {
        return false;
    }

    const bool changed = m_kind != kind || m_state != state || m_progress != progress ||
        m_result != result || m_error != error || m_errorDetail != errorDetail;
    m_kind = kind;
    m_state = state;
    m_progress = progress;
    m_result = result;
    m_error = error;
    m_errorDetail = errorDetail;
    if (changed) {
        emit this->changed();
    }
    return true;
}

void RockpoolOperation::applyChanges(const QVariantMap &properties)
{
    bool changed = false;
    QString value;
    if (stringProperty(properties, QStringLiteral("Kind"), &value) && m_kind != value) {
        m_kind = value;
        changed = true;
    }
    if (stringProperty(properties, QStringLiteral("State"), &value) &&
            (value == QStringLiteral("pending") || value == QStringLiteral("running") ||
             isTerminalState(value)) && m_state != value) {
        m_state = value;
        changed = true;
    }
    if (stringProperty(properties, QStringLiteral("Error"), &value) && m_error != value) {
        m_error = value;
        changed = true;
    }
    if (stringProperty(properties, QStringLiteral("ErrorDetail"), &value) &&
            m_errorDetail != value) {
        m_errorDetail = value;
        changed = true;
    }

    if (properties.contains(QStringLiteral("Progress"))) {
        bool valid = false;
        const QVariant progressValue = unwrapDbusVariant(
            properties.value(QStringLiteral("Progress")));
        const double progress = progressValue.toDouble(&valid);
        if (progressValue.type() == QVariant::Double && valid && qIsFinite(progress) &&
                progress >= 0.0 && progress <= 1.0 &&
                m_progress != progress) {
            m_progress = progress;
            changed = true;
        }
    }
    if (properties.contains(QStringLiteral("Result"))) {
        QVariantMap result;
        if (decodeVariantMap(properties.value(QStringLiteral("Result")), &result) &&
                m_result != result) {
            m_result = result;
            changed = true;
        }
    }
    if (changed) {
        emit this->changed();
    }
}

void RockpoolOperation::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readyChanged();
}

void RockpoolOperation::completeIfTerminal()
{
    if (m_finished || !isTerminalState(m_state)) {
        return;
    }
    m_finished = true;
    m_success = m_state == QStringLiteral("succeeded");
    emit finishedChanged();
    emit completed(m_success);
}

void RockpoolOperation::makeUnavailable(const QString &error, const QString &detail)
{
    if (m_finished) {
        return;
    }
    const bool availableChanged = m_available;
    m_available = false;
    m_state = QStringLiteral("failed");
    m_error = error;
    m_errorDetail = detail;
    setReady(true);
    emit changed();
    if (availableChanged) {
        emit this->availableChanged();
    }
    completeIfTerminal();
}

bool RockpoolOperation::isTerminalState(const QString &state) const
{
    return state == QStringLiteral("succeeded") || state == QStringLiteral("failed") ||
        state == QStringLiteral("cancelled");
}
