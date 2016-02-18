#include "pebbles.h"
#include "pebble.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDebug>
#include <QDBusArgument>
#include <QDBusServiceWatcher>
#include <algorithm>

#define ROCKWORK_SERVICE QStringLiteral("org.rockwork")
#define ROCKWORK_MANAGER_PATH QStringLiteral("/org/rockwork/Manager")
#define ROCKWORK_MANAGER_INTERFACE QStringLiteral("org.rockwork.Manager")

Pebbles::Pebbles(QObject *parent):
    QAbstractListModel(parent)
{
    refresh();
    m_watcher = new QDBusServiceWatcher(ROCKWORK_SERVICE, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    QDBusConnection::sessionBus().connect(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, "PebblesChanged", this, SLOT(refresh()));
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, [this]() {
        qDebug() << "service Registered!";
        refresh();
        QDBusConnection::sessionBus().connect(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE, "PebblesChanged", this, SLOT(refresh()));
    });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, [this]() {
        qDebug() << "service Unregistered!";
        beginResetModel();
        qDeleteAll(m_pebbles);
        m_pebbles.clear();
        endResetModel();
        m_connectedToService = false;
        emit connectedToServiceChanged();
    });
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
    return roles;
}

bool Pebbles::connectedToService()
{
    return m_connectedToService;
}

QString Pebbles::version() const
{
    QDBusInterface iface(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE);
    if (!iface.isValid()) {
        qWarning() << "Could not connect to rockworkd.";
        return QString();
    }
    QDBusMessage reply = iface.call("Version");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Error refreshing watches:" << reply.errorMessage();
        return QString();
    }
    if (reply.arguments().count() == 0) {
        qWarning() << "No reply from service.";
        return QString();
    }
    return reply.arguments().first().toString();
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
    QDBusInterface iface(ROCKWORK_SERVICE, ROCKWORK_MANAGER_PATH, ROCKWORK_MANAGER_INTERFACE);
    if (!iface.isValid()) {
        qWarning() << "Could not connect to rockworkd.";
        return;
    }
    QDBusMessage reply = iface.call("ListWatches");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Error refreshing watches:" << reply.errorMessage();
        return;
    }
    if (reply.arguments().count() == 0) {
        qWarning() << "No reply from service.";
        return;
    }
    QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
    QStringList availableList;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusObjectPath p;
        arg >> p;
        if (find(p) == -1) {
            Pebble *pebble = new Pebble(p, this);
            connect(pebble, &Pebble::connectedChanged, this, &Pebbles::pebbleConnectedChanged);
            beginInsertRows(QModelIndex(), m_pebbles.count(), m_pebbles.count());
            m_pebbles.append(pebble);
            endInsertRows();
            emit countChanged();
        }
        availableList << p.path();
        std::sort(m_pebbles.begin(), m_pebbles.end(), Pebbles::sortPebbles);
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
        m_pebbles.takeAt(idx)->deleteLater();
        endRemoveRows();
        emit countChanged();
    }

    if (!m_connectedToService) {
        m_connectedToService = true;
        emit connectedToServiceChanged();
    }
}

bool Pebbles::sortPebbles(Pebble *a, Pebble *b)
{
    if (a->connected() && !b->connected()) {
        return true;
    }
    else if (!a->connected() && b->connected()) {
        return false;
    }
    else {
        return a->name() < b->name();
    }
}

void Pebbles::pebbleConnectedChanged()
{
    Pebble *pebble = static_cast<Pebble*>(sender());
    emit dataChanged(index(find(pebble->address())), index(find(pebble->address())), {RoleConnected});
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
