#ifndef JSKITLOCALSTORAGE_P_H
#define JSKITLOCALSTORAGE_P_H

#include <QSettings>
#include <QJSEngine>
#include <QUuid>

class JSKitLocalStorage : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int length READ length)

public:
    explicit JSKitLocalStorage(QJSEngine *engine, const QString &storagePath, const QUuid &uuid);

    int length() const;

    Q_INVOKABLE QJSValue getItem(const QJSValue &key) const;
    Q_INVOKABLE bool setItem(const QJSValue &key, const QJSValue &value);
    Q_INVOKABLE bool removeItem(const QJSValue &key);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QJSValue key(int index);

    Q_INVOKABLE QJSValue get(const QJSValue &proxy, const QJSValue &key) const;
    Q_INVOKABLE bool set(const QJSValue &proxy, const QJSValue &key, const QJSValue &value);
    Q_INVOKABLE bool has(const QJSValue &proxy, const QJSValue &key);
    Q_INVOKABLE bool deleteProperty(const QJSValue &proxy, const QJSValue &key);
    Q_INVOKABLE QJSValue keys(const QJSValue &proxy=0);
    Q_INVOKABLE QJSValue enumerate();

private:
    static QString getStorageFileFor(const QString &storageDir, const QUuid &uuid);

private:
    QJSEngine *m_engine;
    QSettings *m_storage;
};

#endif // JSKITLOCALSTORAGE_P_H
