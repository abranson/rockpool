#include <QDesktopServices>
#include <QDir>
#include <QDebug>

#include "jskitlocalstorage.h"

JSKitLocalStorage::JSKitLocalStorage(QJSEngine *engine, const QString &storagePath, const QUuid &uuid):
    QObject(engine),
    m_engine(engine),
    m_storage(new QSettings(getStorageFileFor(storagePath, uuid), QSettings::IniFormat, this))
{
}

int JSKitLocalStorage::length() const
{
    return m_storage->allKeys().size();
}

QJSValue JSKitLocalStorage::getItem(const QJSValue &key) const
{
    QVariant value = m_storage->value(key.toString());

    if (value.isValid()) {
        return QJSValue(value.toString());
    } else {
        return QJSValue(QJSValue::NullValue);
    }
}

bool JSKitLocalStorage::setItem(const QJSValue &key, const QJSValue &value)
{
    m_storage->setValue(key.toString(), QVariant::fromValue(value.toString()));
    return true;
}

bool JSKitLocalStorage::removeItem(const QJSValue &key)
{
    if (m_storage->contains(key.toString())) {
        m_storage->remove(key.toString());
        return true;
    } else {
        return false;
    }
}

void JSKitLocalStorage::clear()
{
    m_storage->clear();
}

QJSValue JSKitLocalStorage::key(int index)
{
    QStringList allKeys = m_storage->allKeys();
    QJSValue key(QJSValue::NullValue);

    if (allKeys.size() > index) {
        key = QJSValue(allKeys[index]);
    }

    return key;
}

QJSValue JSKitLocalStorage::get(const QJSValue &proxy, const QJSValue &key) const
{
    Q_UNUSED(proxy);
    return getItem(key);
}

bool JSKitLocalStorage::set(const QJSValue &proxy, const QJSValue &key, const QJSValue &value)
{
    Q_UNUSED(proxy);
    return setItem(key, value);
}

bool JSKitLocalStorage::has(const QJSValue &proxy, const QJSValue &key)
{
    Q_UNUSED(proxy);
    return m_storage->contains(key.toString());
}

bool JSKitLocalStorage::deleteProperty(const QJSValue &proxy, const QJSValue &key)
{
    Q_UNUSED(proxy);
    return removeItem(key);
}

QJSValue JSKitLocalStorage::keys(const QJSValue &proxy)
{
    Q_UNUSED(proxy);

    QStringList allKeys = m_storage->allKeys();
    QJSValue keyArray = m_engine->newArray(allKeys.size());
    for (int i = 0; i < allKeys.size(); i++) {
        keyArray.setProperty(i, allKeys[i]);
    }

    return keyArray;
}

QJSValue JSKitLocalStorage::enumerate()
{
    return keys(0);
}

QString JSKitLocalStorage::getStorageFileFor(const QString &storageDir, const QUuid &uuid)
{
    QDir dataDir(storageDir + "/js-storage");
    if (!dataDir.exists() && !dataDir.mkpath(dataDir.absolutePath())) {
        qWarning() << "Error creating jskit storage dir";
        return QString();
    }

    QString fileName = uuid.toString();
    fileName.remove('{');
    fileName.remove('}');
    return dataDir.absoluteFilePath(fileName + ".ini");
}
