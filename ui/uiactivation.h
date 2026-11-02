#ifndef UIACTIVATION_H
#define UIACTIVATION_H

#include <QObject>
#include <QRegularExpression>

class UiActivation : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.rockpool.UI")
    Q_PROPERTY(QString firmwareAddress READ firmwareAddress NOTIFY firmwareAddressChanged)
public:
    explicit UiActivation(QObject *parent = nullptr) : QObject(parent) {}
    QString firmwareAddress() const { return m_firmwareAddress; }
    Q_INVOKABLE void consumeFirmwareRequest()
    {
        m_firmwareAddress.clear();
        emit firmwareAddressChanged();
    }

public slots:
    void OpenFirmwareUpdate(const QString &address)
    {
        static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
        if (!pattern.match(address).hasMatch())
            return;
        m_firmwareAddress = address.toUpper();
        emit firmwareAddressChanged();
        emit firmwareRequested();
    }

signals:
    void firmwareAddressChanged();
    void firmwareRequested();

private:
    QString m_firmwareAddress;
};

#endif
