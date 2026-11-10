/* SPDX-License-Identifier: Apache-2.0 */
#include "phonestate.h"
#include "libpebble3d-platform.h"
#include "wire.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QRegularExpression>

namespace {
const int SnapshotTimeoutMs = 5000;
}

PrivacyInterface::PrivacyInterface(const QString &owner, const QDBusConnection &bus, QObject *parent)
    : QDBusAbstractInterface(owner, QStringLiteral("/privacyswitch"),
                             "org.sailfishos.privacyswitch", bus, parent) {}
ActivityInterface::ActivityInterface(const QString &owner, const QDBusConnection &bus, QObject *parent)
    : QDBusAbstractInterface(owner, QStringLiteral("/com/nokia/mce/signal"),
                             "com.nokia.mce.signal", bus, parent) {}

PhoneState::Source::Source(const char *connectionName, const char *serviceName,
                           const char *objectPath, const char *interfaceName,
                           const char *methodName, bool systemBus)
    : bus(QString::fromLatin1(connectionName)), connection(QString::fromLatin1(connectionName)),
      service(QString::fromLatin1(serviceName)), path(QString::fromLatin1(objectPath)),
      interface(QString::fromLatin1(interfaceName)), method(QString::fromLatin1(methodName)),
      system(systemBus), known(false), active(false), lookupPending(false),
      snapshotPending(false), generation(0), version(0), watcher(0), signalInterface(0) {}

PhoneState::PhoneState(const ChangedCallback &changed, const CompletedCallback &completed, QObject *parent)
    : QObject(parent),
      m_privacy("libpebble3d-phone-privacy", "org.sailfishos.privacyswitch", "/privacyswitch",
                "org.sailfishos.privacyswitch", "privacyModeActive", false),
      m_activity("libpebble3d-phone-activity", "com.nokia.mce", "/com/nokia/mce/request",
                 "com.nokia.mce.request", "get_inactivity_status", true),
      m_changed(changed), m_completed(completed), m_started(false)
{
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        maintain(m_privacy);
        maintain(m_activity);
    });
}

PhoneState::~PhoneState()
{
    m_timer.stop();
    delete m_privacy.signalInterface;
    delete m_activity.signalInterface;
    delete m_privacy.watcher;
    delete m_activity.watcher;
    QDBusConnection::disconnectFromBus(m_privacy.connection);
    QDBusConnection::disconnectFromBus(m_activity.connection);
}

void PhoneState::start()
{
    if (m_started) return;
    m_started = true;
    maintain(m_privacy);
    maintain(m_activity);
    m_timer.start();
}

uint32_t PhoneState::flags() const
{
    return (m_privacy.known ? 1u : 0u) | (m_privacy.known && m_privacy.active ? 2u : 0u) |
           (m_activity.known ? 4u : 0u) | (m_activity.known && m_activity.active ? 8u : 0u);
}

void PhoneState::publish() { m_changed(flags()); }

void PhoneState::maintain(Source &source)
{
    if (!source.bus.isConnected()) connectBus(source);
    if (!source.bus.isConnected()) return;
    if (source.owner.isEmpty()) lookup(source);
    else if (!source.known) snapshot(source);
}

void PhoneState::connectBus(Source &source)
{
    ++source.generation;
    delete source.signalInterface; source.signalInterface = 0;
    delete source.watcher; source.watcher = 0;
    source.owner.clear(); source.lookupPending = false; source.snapshotPending = false;
    if (source.known) { source.known = false; publish(); }
    QDBusConnection::disconnectFromBus(source.connection);
    source.bus = QDBusConnection::connectToBus(source.system ? QDBusConnection::SystemBus :
                                               QDBusConnection::SessionBus, source.connection);
    if (!source.bus.isConnected()) return;
    source.watcher = new QDBusServiceWatcher(source.service, source.bus,
                    QDBusServiceWatcher::WatchForOwnerChange, this);
    Source *ptr = &source;
    connect(source.watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this, ptr](const QString &, const QString &, const QString &owner) {
        setOwner(*ptr, owner);
    });
}

void PhoneState::lookup(Source &source)
{
    if (source.lookupPending) return;
    source.lookupPending = true;
    const quint64 generation = source.generation;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("GetNameOwner"));
    call << source.service;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(source.bus.asyncCall(call, SnapshotTimeoutMs), this);
    Source *ptr = &source;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, ptr, generation](QDBusPendingCallWatcher *reply) {
        QDBusPendingReply<QString> result = *reply;
        reply->deleteLater();
        if (ptr->generation != generation) return;
        ptr->lookupPending = false;
        if (!result.isError()) setOwner(*ptr, result.value());
    });
}

void PhoneState::setOwner(Source &source, const QString &owner)
{
    ++source.generation;
    source.lookupPending = false; source.snapshotPending = false;
    delete source.signalInterface; source.signalInterface = 0;
    source.owner = owner;
    if (source.known) { source.known = false; publish(); }
    if (owner.isEmpty()) return;
    Source *ptr = &source;
    const quint64 generation = source.generation;
    if (&source == &m_privacy) {
        PrivacyInterface *interface = new PrivacyInterface(owner, source.bus, this);
        source.signalInterface = interface;
        connect(interface, &PrivacyInterface::privacyModeActiveChanged, this,
                [this, ptr, generation](bool active) {
            if (ptr->generation == generation) { ++ptr->version; accept(*ptr, active); }
        });
    } else {
        ActivityInterface *interface = new ActivityInterface(owner, source.bus, this);
        source.signalInterface = interface;
        connect(interface, &ActivityInterface::system_inactivity_ind, this,
                [this, ptr, generation](bool inactive) {
            if (ptr->generation == generation) { ++ptr->version; accept(*ptr, !inactive); }
        });
    }
    snapshot(source);
}

void PhoneState::snapshot(Source &source)
{
    if (source.snapshotPending || source.owner.isEmpty()) return;
    source.snapshotPending = true;
    const quint64 generation = source.generation, version = source.version;
    QDBusMessage call = QDBusMessage::createMethodCall(source.owner, source.path, source.interface, source.method);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(source.bus.asyncCall(call, SnapshotTimeoutMs), this);
    Source *ptr = &source;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, ptr, generation, version](QDBusPendingCallWatcher *reply) {
        QDBusPendingReply<bool> result = *reply;
        reply->deleteLater();
        if (ptr->generation != generation) return;
        ptr->snapshotPending = false;
        if (!result.isError() && ptr->version == version)
            accept(*ptr, ptr == &m_activity ? !result.value() : result.value());
    });
}

void PhoneState::accept(Source &source, bool active)
{
    if (source.known && source.active == active) return;
    source.known = true; source.active = active;
    publish();
}

void PhoneState::command(quint64 id, uint16_t operation, const QString &value)
{
    if (!m_privacy.bus.isConnected()) {
        m_completed(id, operation, LP3_PLATFORM_UNAVAILABLE); return;
    }
    if (operation == lp3wire::PairingRequest) {
        static const QRegularExpression address(QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
        if (!address.match(value).hasMatch()) {
            m_completed(id, operation, LP3_PLATFORM_INVALID_ARGUMENT); return;
        }
        QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("com.jolla.lipstick"),
            QStringLiteral("/bluetooth"), QStringLiteral("com.jolla.lipstick"), QStringLiteral("pairWithDevice"));
        message << value;
        m_completed(id, operation, m_privacy.bus.send(message) ? LP3_PLATFORM_OK : LP3_PLATFORM_IO_ERROR);
        return;
    }
    if (operation != lp3wire::ProfileSet || value.isEmpty()) {
        m_completed(id, operation, LP3_PLATFORM_INVALID_ARGUMENT); return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("com.nokia.profiled"),
        QStringLiteral("/com/nokia/profiled"), QStringLiteral("com.nokia.profiled"), QStringLiteral("set_profile"));
    message << value;
    m_completed(id, operation, m_privacy.bus.send(message) ? LP3_PLATFORM_OK : LP3_PLATFORM_IO_ERROR);
}

void PhoneState::cancel(quint64) {}
