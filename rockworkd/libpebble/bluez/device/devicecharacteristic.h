#ifndef DEVICECHARACTERISTIC_H
#define DEVICECHARACTERISTIC_H

#include <QObject>
#include <QDBusInterface>
#include <QDBusReply>
#include <QPointer>

#include <functional>
#include <type_traits>

class PrvCallback {
public:
    QPointer<QObject> obj;
    std::function<void(QByteArray)> functor;
};

class DeviceCharacteristic : public QObject
{
    Q_OBJECT

public:
    DeviceCharacteristic(const QString &path, const QVariantMap &properties, QObject *parent = nullptr);

    QString uuid() const;

    QByteArray readCharacteristic();
    void writeCharacteristic(const QByteArray &data);

    template <class Object, typename Function>
    void subscribeToCharacteristic(Object obj, Function functor)
    {
        if (!m_notifyStarted) {
            m_iface->call("StartNotify");
            m_notifyStarted = true;
        }

        PrvCallback cb;
        cb.obj = obj;
        cb.functor = std::bind(functor, obj, std::placeholders::_1);

        m_callbacks.append(cb);
    }

public slots:
    void propertiesChanged(QString interface, QVariantMap properties, QStringList /*invalidated_properties*/);

private:
    QDBusInterface *m_iface;
    QVariantMap m_properties;

    bool m_notifyStarted = false;

    QList<PrvCallback> m_callbacks;
};

#endif // DEVICECHARACTERISTIC_H