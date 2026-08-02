#include "pebbles.h"
#include "pebble.h"
#include "rockpoolaccount.h"

#include <QDBusConnection>
#include <QDebug>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <algorithm>

#define ROCKWORK_SERVICE QStringLiteral("org.rockwork")
#define ROCKWORK_MANAGER_PATH QStringLiteral("/org/rockwork/Manager")
#define ROCKWORK_MANAGER_INTERFACE QStringLiteral("org.rockwork.Manager")

static QDBusPendingCall callManager(const QString &method,
                                    const QVariantList &arguments = QVariantList())
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().asyncCall(message);
}

Pebbles::Pebbles(QObject *parent):
    QAbstractListModel(parent)
{
    m_account = new RockpoolAccount(this);
    m_watcher = new QDBusServiceWatcher(ROCKWORK_SERVICE, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    QDBusConnection::sessionBus().connect(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, "PebblesChanged", this, SLOT(refresh()));
    QDBusConnection::sessionBus().connect(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, "ScanningChanged", this, SLOT(onScanningChanged(bool)));
    QDBusConnection::sessionBus().connect(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, "ScanResultsChanged", this, SLOT(refreshScanResults()));
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            [this](const QString &, const QString &oldOwner, const QString &newOwner) {
        qDebug() << "service owner changed:" << oldOwner << "->" << newOwner;
        ++m_serviceEpoch;
        ++m_watchListEpoch;
        ++m_versionEpoch;
        ++m_scanningEpoch;
        ++m_scanResultsEpoch;
        if (!oldOwner.isEmpty()) {
            const bool hadPebbles = !m_pebbles.isEmpty();
            beginResetModel();
            qDeleteAll(m_pebbles);
            m_pebbles.clear();
            m_pebblesWithIdentity.clear();
            endResetModel();
            if (hadPebbles) {
                emit countChanged();
            }
            if (m_connectedToService) {
                m_connectedToService = false;
                emit connectedToServiceChanged();
            }
            if (!m_version.isEmpty()) {
                m_version.clear();
                emit versionChanged();
            }
            onScanningChanged(false);
            if (!m_scanResults.isEmpty()) {
                m_scanResults.clear();
                emit scanResultsChanged();
            }
        }
        if (!newOwner.isEmpty()) {
            refresh();
            refreshVersion();
            refreshScanning();
            refreshScanResults();
        }
    });
    refresh();
    refreshVersion();
    refreshScanning();
    refreshScanResults();
}

int Pebbles::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_pebbles.count();
}

QVariant Pebbles::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case RoleAddress:
        return m_pebbles.at(index.row())->address();
    case RoleName:
        return m_pebbles.at(index.row())->name();
    case RoleSerialNumber:
        return m_pebbles.at(index.row())->serialNumber();
    case RoleConnected:
        return m_pebbles.at(index.row())->connected();
    case RoleConnectionState:
        return m_pebbles.at(index.row())->connectionState();
    }

    return QVariant();
}

QHash<int, QByteArray> Pebbles::roleNames() const
{
    QHash<int,QByteArray> roles;
    roles.insert(RoleAddress, "address");
    roles.insert(RoleName, "name");
    roles.insert(RoleSerialNumber, "serialNumber");
    roles.insert(RoleConnected, "connected");
    roles.insert(RoleConnectionState, "connectionState");
    return roles;
}

bool Pebbles::connectedToService()
{
    return m_connectedToService;
}

QString Pebbles::version() const
{
    return m_version;
}

void Pebbles::refreshVersion()
{
    const quint64 requestEpoch = ++m_versionEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        callManager(QStringLiteral("Version")), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch", QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebbles::versionReplyFinished);
}

void Pebbles::versionReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_versionEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Error refreshing compatibility service version:" << reply.errorMessage();
        return;
    }
    if (reply.arguments().count() == 0) {
        qWarning() << "No version reply from compatibility service.";
        return;
    }
    const QString version = reply.arguments().first().toString();
    if (m_version != version) {
        m_version = version;
        emit versionChanged();
    }
}

Pebble *Pebbles::get(int index) const
{
    if (index >= 0 && index < m_pebbles.count()) {
        return m_pebbles.at(index);
    }
    return nullptr;
}

int Pebbles::find(const QString &address) const
{
    for (int i = 0; i < m_pebbles.count(); i++) {
        if (m_pebbles.at(i)->address() == address) {
            return i;
        }
    }
    return -1;
}

void Pebbles::refresh()
{
    qDebug() << "pebbles changed";
    const quint64 requestEpoch = ++m_watchListEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        callManager(QStringLiteral("ListWatches")), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch", QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebbles::watchListReplyFinished);
}

void Pebbles::watchListReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_watchListEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Error refreshing watches:" << reply.errorMessage();
        return;
    }
    if (reply.arguments().count() == 0) {
        qWarning() << "No reply from service.";
        return;
    }
    const QDBusArgument &arg = reply.arguments().first().value<QDBusArgument>();
    QStringList availableList;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusObjectPath p;
        arg >> p;
        if (find(p) == -1) {
            Pebble *pebble = new Pebble(p, this, m_account);
            connect(pebble, &Pebble::identityChanged, this, &Pebbles::pebbleIdentityChanged);
            connect(pebble, &Pebble::connectedChanged, this, &Pebbles::pebbleConnectedChanged);
            connect(pebble, &Pebble::connectionStateChanged, this, &Pebbles::pebbleConnectedChanged);
            beginInsertRows(QModelIndex(), m_pebbles.count(), m_pebbles.count());
            m_pebbles.append(pebble);
            endInsertRows();
            emit countChanged();
        }
        availableList << p.path();
    }
    arg.endArray();

    QList<Pebble*> toRemove;
    foreach (Pebble *pebble, m_pebbles) {
        bool found = false;
        foreach (const QString &path, availableList) {
            if (path == pebble->path().path()) {
                found = true;
                break;
            }
        }
        if (!found) {
            toRemove << pebble;
        }
    }

    while (!toRemove.isEmpty()) {
        Pebble *pebble = toRemove.takeFirst();
        int idx = m_pebbles.indexOf(pebble);
        beginRemoveRows(QModelIndex(), idx, idx);
        m_pebblesWithIdentity.remove(pebble);
        m_pebbles.takeAt(idx)->deleteLater();
        endRemoveRows();
        emit countChanged();
    }

    resortPebbles();

    if (!m_connectedToService) {
        m_connectedToService = true;
        emit connectedToServiceChanged();
    }
}

void Pebbles::resortPebbles()
{
    QList<Pebble*> sorted = m_pebbles;
    std::sort(sorted.begin(), sorted.end(), Pebbles::sortPebbles);
    if (sorted == m_pebbles) {
        return;
    }

    // Changing the backing order without model signals leaves QML delegates attached to the
    // wrong Pebble. Watch count is tiny and ordering changes infrequently, so a reset is clearer
    // and safer than trying to maintain persistent indexes across a pointer sort.
    beginResetModel();
    m_pebbles = sorted;
    endResetModel();
}

bool Pebbles::sortPebbles(Pebble *a, Pebble *b)
{
    if (a->connected() && !b->connected()) {
        return true;
    }
    else if (!a->connected() && b->connected()) {
        return false;
    }
    else if (a->name() != b->name()) {
        return a->name() < b->name();
    }
    return a->address() < b->address();
}

void Pebbles::pebbleConnectedChanged()
{
    Pebble *pebble = static_cast<Pebble*>(sender());
    resortPebbles();
    const int row = m_pebbles.indexOf(pebble);
    if (row >= 0) {
        emit dataChanged(index(row), index(row), {RoleConnected, RoleConnectionState});
    }
}

void Pebbles::pebbleIdentityChanged()
{
    Pebble *pebble = static_cast<Pebble*>(sender());
    resortPebbles();
    const int row = m_pebbles.indexOf(pebble);
    if (row >= 0) {
        emit dataChanged(index(row), index(row),
                         {RoleAddress, RoleName, RoleSerialNumber});
        if (!pebble->address().isEmpty()
                && !m_pebblesWithIdentity.contains(pebble)) {
            m_pebblesWithIdentity.insert(pebble);
            emit pebbleIdentityAvailable(pebble->address());
        }
    }
}

int Pebbles::find(const QDBusObjectPath &path) const
{
    for (int i = 0; i < m_pebbles.count(); i++) {
        if (m_pebbles.at(i)->path() == path) {
            return i;
        }
    }
    return -1;
}

bool Pebbles::scanning() const
{
    return m_scanning;
}

QVariantList Pebbles::scanResults() const
{
    return m_scanResults;
}

void Pebbles::startScan()
{
    sendManagerCommand(QStringLiteral("StartScan"));
}

void Pebbles::stopScan()
{
    sendManagerCommand(QStringLiteral("StopScan"));
}

void Pebbles::connectWatch(const QString &address)
{
    sendManagerCommand(QStringLiteral("ConnectWatch"), QVariantList() << address);
}

void Pebbles::disconnectWatch(const QString &address)
{
    sendManagerCommand(QStringLiteral("DisconnectWatch"), QVariantList() << address);
}

void Pebbles::forgetWatch(const QString &address)
{
    sendManagerCommand(QStringLiteral("ForgetWatch"), QVariantList() << address);
    // The daemon emits PebblesChanged once the watch is gone; the model refreshes from that.
}

void Pebbles::sendManagerCommand(const QString &method, const QVariantList &arguments)
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        callManager(method, arguments), this);
    watcher->setProperty("method", method);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebbles::managerCommandReplyFinished);
}

void Pebbles::managerCommandReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const QString method = watcher->property("method").toString();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << method << "failed:" << reply.errorMessage();
    }
}

void Pebbles::onScanningChanged(bool scanning)
{
    ++m_scanningEpoch;
    if (m_scanning != scanning) {
        m_scanning = scanning;
        emit scanningChanged();
    }
}

void Pebbles::refreshScanning()
{
    const quint64 requestEpoch = ++m_scanningEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        callManager(QStringLiteral("IsScanning")), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch", QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebbles::scanningReplyFinished);
}

void Pebbles::scanningReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_scanningEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() == 0) {
        qWarning() << "Error fetching scanning state:" << reply.errorMessage();
        return;
    }
    onScanningChanged(reply.arguments().first().toBool());
}

void Pebbles::refreshScanResults()
{
    const quint64 requestEpoch = ++m_scanResultsEpoch;
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        callManager(QStringLiteral("ScanResults")), this);
    watcher->setProperty("serviceEpoch", QVariant::fromValue<qulonglong>(m_serviceEpoch));
    watcher->setProperty("requestEpoch", QVariant::fromValue<qulonglong>(requestEpoch));
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &Pebbles::scanResultsReplyFinished);
}

void Pebbles::scanResultsReplyFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusMessage reply = watcher->reply();
    const quint64 serviceEpoch = watcher->property("serviceEpoch").toULongLong();
    const quint64 requestEpoch = watcher->property("requestEpoch").toULongLong();
    watcher->deleteLater();
    if (serviceEpoch != m_serviceEpoch || requestEpoch != m_scanResultsEpoch) {
        return;
    }
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().count() == 0) {
        qWarning() << "Error fetching scan results:" << reply.errorMessage();
        return;
    }

    QVariantList results;
    const QDBusArgument &arg = reply.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        QVariant mapEntryVariant;
        arg >> mapEntryVariant;
        // 'av' elements arrive as QDBusArgument (a{sv} inside the variant); tolerate
        // plain aa{sv} too, where Qt hands us a ready QVariantMap.
        QVariantMap resultMap;
        if (mapEntryVariant.userType() == qMetaTypeId<QDBusArgument>()) {
            QDBusArgument mapEntry = mapEntryVariant.value<QDBusArgument>();
            mapEntry >> resultMap;
        } else {
            resultMap = mapEntryVariant.toMap();
        }
        results.append(resultMap);
    }
    arg.endArray();

    if (m_scanResults != results) {
        m_scanResults = results;
        emit scanResultsChanged();
    }
}
