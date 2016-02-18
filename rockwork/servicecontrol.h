#ifndef SERVICECONTROL_H
#define SERVICECONTROL_H

#include <QDBusInterface>
#include <QObject>

static const QString ROCKPOOLD_SYSTEMD_UNIT("rockpoold.service");

class ServiceControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serviceRunning READ serviceRunning WRITE setServiceRunning NOTIFY serviceRunningChanged)

public:
    explicit ServiceControl(QObject *parent = 0);

    bool serviceRunning() const;
    bool setServiceRunning(bool running);
    Q_INVOKABLE bool startService();
    Q_INVOKABLE bool stopService();
    Q_INVOKABLE bool restartService();

private slots:
    void getUnitProperties();
    void onPropertiesChanged(QString interface, QMap<QString, QVariant> changed, QStringList invalidated);

signals:
    void serviceRunningChanged();

private:
    QDBusInterface *systemd;
    QDBusObjectPath unitPath;
    QVariantMap unitProperties;

};

#endif // SERVICECONTROL_H
