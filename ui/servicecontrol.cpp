#include "servicecontrol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>

namespace {
const char SYSTEMD_SERVICE[] = "org.freedesktop.systemd1";
const char SYSTEMD_MANAGER_PATH[] = "/org/freedesktop/systemd1";
const char SYSTEMD_MANAGER_INTERFACE[] = "org.freedesktop.systemd1.Manager";
const char DBUS_PROPERTIES_INTERFACE[] = "org.freedesktop.DBus.Properties";
const char SYSTEMD_UNIT_INTERFACE[] = "org.freedesktop.systemd1.Unit";
}

SystemdManagerInterface::SystemdManagerInterface(QObject *parent)
    : QDBusAbstractInterface(QString::fromLatin1(SYSTEMD_SERVICE),
                             QString::fromLatin1(SYSTEMD_MANAGER_PATH),
                             SYSTEMD_MANAGER_INTERFACE,
                             QDBusConnection::sessionBus(), parent)
{
}

SystemdUnitPropertiesInterface::SystemdUnitPropertiesInterface(
        const QString &path, QObject *parent)
    : QDBusAbstractInterface(QString::fromLatin1(SYSTEMD_SERVICE), path,
                             DBUS_PROPERTIES_INTERFACE,
                             QDBusConnection::sessionBus(), parent)
{
}

ServiceControl::ServiceControl(QObject *parent)
    : QObject(parent),
      m_systemd(new SystemdManagerInterface(this)),
      m_serviceWatcher(new QDBusServiceWatcher(
          QString::fromLatin1(SYSTEMD_SERVICE), QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForOwnerChange, this))
{
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &ServiceControl::serviceOwnerChanged);
    initialize();
}

bool ServiceControl::ready() const
{
    return m_ready;
}

bool ServiceControl::serviceRunning() const
{
    const QString state = m_unitProperties.value(QStringLiteral("ActiveState")).toString();
    return state == QStringLiteral("active") ||
           state == QStringLiteral("activating") ||
           state == QStringLiteral("reloading");
}

bool ServiceControl::setServiceRunning(bool running)
{
    if (!m_ready) {
        return false;
    }
    if (running != serviceRunning()) {
        return running ? startService() : stopService();
    }
    return true;
}

bool ServiceControl::startService()
{
    return enqueueOperation(StartOperation);
}

bool ServiceControl::stopService()
{
    return enqueueOperation(StopOperation);
}

bool ServiceControl::restartService()
{
    return enqueueOperation(RestartOperation);
}

void ServiceControl::initialize()
{
    const quint64 serviceEpoch = ++m_serviceEpoch;
    ++m_propertiesEpoch;
    ++m_operationEpoch;
    m_activeOperation = NoOperation;
    m_queuedOperation = NoOperation;
    m_operationStep = 0;
    setReady(false);

    if (m_unitPropertiesInterface) {
        m_unitPropertiesInterface->deleteLater();
        m_unitPropertiesInterface = 0;
    }
    m_unitPath = QDBusObjectPath();

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_systemd->asyncCall(QStringLiteral("Subscribe")), this);
    watcher->setProperty("stage", QStringLiteral("Subscribe"));
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &ServiceControl::initializationReplyFinished);
}

void ServiceControl::requestLoadUnit(quint64 serviceEpoch)
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_systemd->asyncCall(QStringLiteral("LoadUnit"), ROCKPOOLD_SYSTEMD_UNIT),
        this);
    watcher->setProperty("stage", QStringLiteral("LoadUnit"));
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &ServiceControl::initializationReplyFinished);
}

void ServiceControl::initializationReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString stage = watcher->property("stage").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch) {
        return;
    }

    if (stage == QStringLiteral("Subscribe")) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "Could not subscribe to systemd state changes:"
                       << reply.errorMessage();
        }
        requestLoadUnit(serviceEpoch);
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() != 1) {
        qWarning() << "Could not load" << ROCKPOOLD_SYSTEMD_UNIT << ':'
                   << reply.errorMessage();
        applyUnitProperties(QVariantMap());
        setReady(true);
        return;
    }

    const QDBusObjectPath path = reply.arguments().first().value<QDBusObjectPath>();
    if (path.path().isEmpty()) {
        qWarning() << "systemd returned an empty unit path for" << ROCKPOOLD_SYSTEMD_UNIT;
        applyUnitProperties(QVariantMap());
        setReady(true);
        return;
    }

    m_unitPath = path;
    m_unitPropertiesInterface = new SystemdUnitPropertiesInterface(path.path(), this);
    SystemdUnitPropertiesInterface *const propertiesInterface = m_unitPropertiesInterface;
    const quint64 proxyServiceEpoch = m_serviceEpoch;
    connect(m_unitPropertiesInterface, &SystemdUnitPropertiesInterface::PropertiesChanged,
            this, [this, propertiesInterface, proxyServiceEpoch](
                const QString &interfaceName, const QVariantMap &changed,
                const QStringList &invalidated) {
        if (proxyServiceEpoch != m_serviceEpoch ||
                propertiesInterface != m_unitPropertiesInterface) {
            return;
        }
        onPropertiesChanged(interfaceName, changed, invalidated);
    });
    getUnitProperties();
}

void ServiceControl::getUnitProperties()
{
    if (!m_unitPropertiesInterface) {
        setReady(true);
        return;
    }

    const quint64 propertiesEpoch = ++m_propertiesEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_unitPropertiesInterface->asyncCall(QStringLiteral("GetAll"),
                                              QString::fromLatin1(SYSTEMD_UNIT_INTERFACE)),
        this);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("propertiesEpoch",
                         QVariant::fromValue<qulonglong>(propertiesEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &ServiceControl::unitPropertiesReplyFinished);
}

void ServiceControl::unitPropertiesReplyFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 propertiesEpoch = watcher->property("propertiesEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || propertiesEpoch != m_propertiesEpoch) {
        return;
    }

    if (reply.isError()) {
        qWarning() << "Could not read systemd unit state:" << reply.error().message();
        applyUnitProperties(QVariantMap());
    } else {
        applyUnitProperties(reply.value());
    }
    setReady(true);
}

void ServiceControl::applyUnitProperties(const QVariantMap &properties)
{
    const bool wasRunning = serviceRunning();
    m_unitProperties = properties;
    if (wasRunning != serviceRunning()) {
        emit serviceRunningChanged();
    }
}

void ServiceControl::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readyChanged();
}

void ServiceControl::onPropertiesChanged(const QString &interfaceName,
                                         const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    if (interfaceName != QString::fromLatin1(SYSTEMD_UNIT_INTERFACE)) {
        return;
    }

    if (changed.contains(QStringLiteral("ActiveState"))) {
        ++m_propertiesEpoch;
        QVariantMap properties = m_unitProperties;
        properties.insert(QStringLiteral("ActiveState"),
                          changed.value(QStringLiteral("ActiveState")));
        applyUnitProperties(properties);
        setReady(true);
    } else if (invalidated.contains(QStringLiteral("ActiveState"))) {
        getUnitProperties();
    }
}

bool ServiceControl::enqueueOperation(Operation operation)
{
    if (!m_ready) {
        return false;
    }
    if (m_activeOperation == NoOperation) {
        beginOperation(operation);
    } else if (m_activeOperation == operation) {
        m_queuedOperation = NoOperation;
    } else {
        m_queuedOperation = operation;
    }
    return true;
}

void ServiceControl::beginOperation(Operation operation)
{
    m_activeOperation = operation;
    m_operationStep = 0;
    ++m_operationEpoch;
    dispatchOperationStep();
}

void ServiceControl::dispatchOperationStep()
{
    QString method;
    QVariantList arguments;

    switch (m_activeOperation) {
    case StartOperation:
        if (m_operationStep == 0) {
            method = QStringLiteral("EnableUnitFiles");
            arguments << QVariant::fromValue(QStringList() << ROCKPOOLD_SYSTEMD_UNIT)
                      << false << true;
        } else if (m_operationStep == 1) {
            method = QStringLiteral("Reload");
        } else if (m_operationStep == 2) {
            method = QStringLiteral("StartUnit");
            arguments << ROCKPOOLD_SYSTEMD_UNIT << QStringLiteral("replace");
        }
        break;
    case StopOperation:
        if (m_operationStep == 0) {
            method = QStringLiteral("StopUnit");
            arguments << ROCKPOOLD_SYSTEMD_UNIT << QStringLiteral("replace");
        } else if (m_operationStep == 1) {
            method = QStringLiteral("DisableUnitFiles");
            arguments << QVariant::fromValue(QStringList() << ROCKPOOLD_SYSTEMD_UNIT)
                      << false;
        } else if (m_operationStep == 2) {
            method = QStringLiteral("Reload");
        }
        break;
    case RestartOperation:
        if (m_operationStep == 0) {
            method = QStringLiteral("RestartUnit");
            arguments << ROCKPOOLD_SYSTEMD_UNIT << QStringLiteral("replace");
        }
        break;
    case NoOperation:
        break;
    }

    if (method.isEmpty()) {
        finishOperation();
        return;
    }

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        m_systemd->asyncCallWithArgumentList(method, arguments), this);
    watcher->setProperty("method", method);
    watcher->setProperty("serviceEpoch",
                         QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("operationEpoch",
                         QVariant::fromValue<qulonglong>(m_operationEpoch));
    watcher->setProperty("operation", static_cast<int>(m_activeOperation));
    watcher->setProperty("step", m_operationStep);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &ServiceControl::operationReplyFinished);
}

void ServiceControl::operationReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString method = watcher->property("method").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 operationEpoch = watcher->property("operationEpoch").toULongLong();
    const Operation operation = static_cast<Operation>(watcher->property("operation").toInt());
    const int step = watcher->property("step").toInt();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || operationEpoch != m_operationEpoch ||
            operation != m_activeOperation || step != m_operationStep) {
        return;
    }

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "systemd" << method << "failed:" << reply.errorMessage();
        finishOperation();
        return;
    }

    ++m_operationStep;
    dispatchOperationStep();
}

void ServiceControl::finishOperation()
{
    m_activeOperation = NoOperation;
    m_operationStep = 0;
    const Operation next = m_queuedOperation;
    m_queuedOperation = NoOperation;
    if (next != NoOperation) {
        beginOperation(next);
    }
}

void ServiceControl::serviceOwnerChanged(const QString &service,
                                         const QString &oldOwner,
                                         const QString &newOwner)
{
    Q_UNUSED(service)
    if (!oldOwner.isEmpty()) {
        ++m_serviceEpoch;
        ++m_propertiesEpoch;
        ++m_operationEpoch;
        m_activeOperation = NoOperation;
        m_queuedOperation = NoOperation;
        m_operationStep = 0;
        if (m_unitPropertiesInterface) {
            m_unitPropertiesInterface->deleteLater();
            m_unitPropertiesInterface = 0;
        }
        m_unitPath = QDBusObjectPath();
        applyUnitProperties(QVariantMap());
        setReady(false);
    }
    if (!newOwner.isEmpty()) {
        initialize();
    }
}
