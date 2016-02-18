#ifndef PEBBLEMANAGER_H
#define PEBBLEMANAGER_H

#include "libpebble/pebble.h"
#include "libpebble/bluez/bluezclient.h"

#include <QObject>

class PebbleManager : public QObject
{
    Q_OBJECT
public:
    explicit PebbleManager(QObject *parent = 0);

    QList<Pebble*> pebbles() const;
    Pebble* get(const QBluetoothAddress &address);

signals:
    void pebbleAdded(Pebble *pebble);
    void pebbleRemoved(Pebble *pebble);

private slots:
    void loadPebbles();

    void pebbleConnected();

private:
    void setupPebble(Pebble *pebble);

    BluezClient *m_bluezClient;

    QList<Pebble*> m_pebbles;
};

#endif // PEBBLEMANAGER_H
