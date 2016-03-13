#include "pebblemanager.h"

#include "core.h"

#include "libpebble/platforminterface.h"

#include <QHash>

#ifdef ENABLE_TESTING
#include <QQuickView>
#include <QQmlEngine>
#include <QQmlContext>
#endif

PebbleManager::PebbleManager(QObject *parent) : QObject(parent)
{
    m_bluezClient = new BluezClient(this);
    connect(m_bluezClient, &BluezClient::devicesChanged, this, &PebbleManager::loadPebbles);
    loadPebbles();
}

QList<Pebble *> PebbleManager::pebbles() const
{
    return m_pebbles;
}

void PebbleManager::loadPebbles()
{
    QList<Device> pairedPebbles = m_bluezClient->pairedPebbles();
    foreach (const Device &device, pairedPebbles) {
        qDebug() << "loading pebble" << device.address.toString();
        Pebble *pebble = get(device.address);
        if (!pebble) {
            qDebug() << "creating new pebble";
            pebble = new Pebble(device.address, this);
            pebble->setName(device.name);
            setupPebble(pebble);
            m_pebbles.append(pebble);
            qDebug() << "have pebbles:" << m_pebbles.count() << this;
            emit pebbleAdded(pebble);
        }
        if (!pebble->connected()) {
            pebble->connect();
        }
    }
    QList<Pebble*> pebblesToRemove;
    foreach (Pebble *pebble, m_pebbles) {
        bool found = false;
        foreach (const Device &dev, pairedPebbles) {
            if (dev.address == pebble->address()) {
                found = true;
                break;
            }
        }
        if (!found) {
            pebblesToRemove << pebble;
        }
    }

    while (!pebblesToRemove.isEmpty()) {
        Pebble *pebble = pebblesToRemove.takeFirst();
        qDebug() << "Removing pebble" << pebble->address().toString();
        m_pebbles.removeOne(pebble);
        emit pebbleRemoved(pebble);
        pebble->deleteLater();
    }
}

void PebbleManager::pebbleConnected()
{
}

void PebbleManager::setupPebble(Pebble *pebble)
{

#ifdef ENABLE_TESTING
    qmlRegisterUncreatableType<Pebble>("PebbleTest", 1, 0, "Pebble", "Dont");
    QQuickView *view = new QQuickView();
    view->engine()->rootContext()->setContextProperty("pebble", pebble);
    view->setSource(QUrl("qrc:///testui/PebbleController.qml"));
    view->show();
#endif

    connect(pebble, &Pebble::pebbleConnected, this, &PebbleManager::pebbleConnected);
}

Pebble* PebbleManager::get(const QBluetoothAddress &address)
{
    for (int i = 0; i < m_pebbles.count(); i++) {
        if (m_pebbles.at(i)->address() == address) {
            return m_pebbles.at(i);
        }
    }
    return nullptr;
}
