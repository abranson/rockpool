/* SPDX-License-Identifier: Apache-2.0 */
#include "securestorage.h"

#include <libpebble3d-platform.h>

#include <QByteArray>
#include <QtEndian>
#include <QString>
#include <QUuid>

#include <Secrets/createcollectionrequest.h>
#include <Secrets/deletecollectionrequest.h>
#include <Secrets/deletesecretrequest.h>
#include <Secrets/secretmanager.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/storesecretrequest.h>

using namespace Sailfish::Secrets;

namespace {
const uint32_t kOk = LP3_PLATFORM_OK;
const uint32_t kInternal = LP3_PLATFORM_INTERNAL_ERROR;

QString collectionName(const std::string &uuid) {
    // SQLCipher accepts up to 31 alphanumeric characters. Base32 retains
    // every UUID bit and leaves room for a Rockpool collection prefix.
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    const QByteArray bytes = QUuid(QString::fromStdString(uuid)).toRfc4122();
    QByteArray name("rp");
    uint32_t buffer = 0;
    int bits = 0;
    for (char byte : bytes) {
        buffer = (buffer << 8) | static_cast<uint8_t>(byte);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            name.append(alphabet[(buffer >> bits) & 31]);
        }
    }
    if (bits) name.append(alphabet[(buffer << (5 - bits)) & 31]);
    return QString::fromLatin1(name);
}

QString secretName(const std::string &key) {
    return QString::fromUtf8(key.data(), static_cast<int>(key.size()));
}
}

class SecureStorage::Private : public QObject {
public:
    Private(const CompleteCallback &complete, QObject *parent)
        : QObject(parent), complete(complete) {}

    CompleteCallback complete;
    SecretManager manager;
    uint64_t requestId = 0;
    uint32_t action = LP3_SECRET_GET;
    QString collection;
    QString key;
    QByteArray value;
    bool cancelled = false;
    quint64 generations[2] = {};
    QByteArray values[2];

    void finish(uint32_t status, int foundSlot = -1) {
        const auto id = requestId;
        std::vector<uint8_t> bytes;
        if (foundSlot >= 0) {
            const QByteArray &stored = values[foundSlot];
            bytes.assign(stored.begin(), stored.end());
        }
        const bool deliver = !cancelled;
        requestId = 0;
        cancelled = false;
        value.clear();
        for (int i = 0; i < 2; ++i) {
            values[i].clear();
            generations[i] = 0;
        }
        if (deliver) complete(id, status, foundSlot >= 0, bytes);
    }

    template<typename T, typename Callback>
    void start(T *request, Callback done) {
        request->setManager(&manager);
        request->setUserInteractionMode(SecretManager::PreventInteraction);
        QObject::connect(request, &Request::resultChanged, this, [this, request, done]() {
            if (request->status() != Request::Finished) return;
            request->deleteLater();
            if (cancelled) finish(LP3_PLATFORM_CANCELLED);
            else done(request->result());
        });
        request->startRequest();
    }

    static bool missing(const Result &result) {
        return result.errorCode() == Result::InvalidCollectionError ||
               result.errorCode() == Result::InvalidSecretError;
    }

    Secret::Identifier identifier(int slot) const {
        return Secret::Identifier(key + (slot ? QLatin1Char('b') : QLatin1Char('a')),
                                  collection, SecretManager::DefaultEncryptedStoragePluginName);
    }

    void readSlot(int slot) {
        auto *get = new StoredSecretRequest(this);
        get->setIdentifier(identifier(slot));
        start(get, [this, get, slot](const Result &result) {
            if (result.code() == Result::Succeeded) {
                const QByteArray data = get->secret().data();
                if (data.size() < 8 || data.size() > 8 + static_cast<int>(lp3wire::kSecureValueMax)) {
                    finish(kInternal);
                    return;
                }
                generations[slot] = qFromBigEndian<quint64>(
                    reinterpret_cast<const uchar *>(data.constData()));
                if (generations[slot] == 0) {
                    finish(kInternal);
                    return;
                }
                if (action == LP3_SECRET_GET) values[slot] = data.mid(8);
            } else if (!missing(result)) {
                finish(kInternal);
                return;
            }
            if (slot == 0) readSlot(1);
            else handleSlots();
        });
    }

    template<typename Callback>
    void removeSlot(int slot, Callback done) {
        if (!generations[slot]) {
            done(true);
            return;
        }
        auto *remove = new DeleteSecretRequest(this);
        remove->setIdentifier(identifier(slot));
        start(remove, [done](const Result &result) {
            done(result.code() == Result::Succeeded || missing(result));
        });
    }

    // The original API can insert and delete a secret, but cannot replace it.
    // Stage the next generation in the other slot before retiring the old one.
    // After interruption the highest complete generation is authoritative.
    void handleSlots() {
        const int latest = generations[1] > generations[0] ? 1 : 0;
        const int older = 1 - latest;
        if (generations[latest] && generations[latest] == generations[older]) {
            finish(kInternal);
            return;
        }
        if (action == LP3_SECRET_GET) {
            finish(kOk, generations[latest] ? latest : -1);
        } else if (action == LP3_SECRET_REMOVE) {
            removeSlot(older, [this, latest](bool ok) {
                if (!ok) finish(kInternal);
                else removeSlot(latest, [this](bool removed) { finish(removed ? kOk : kInternal); });
            });
        } else if (generations[latest] == UINT64_MAX) {
            finish(kInternal);
        } else {
            removeSlot(older, [this, older, latest](bool ok) {
                if (!ok) finish(kInternal);
                else storeSlot(older, latest);
            });
        }
    }

    void storeSlot(int target, int previous) {
        auto *store = new StoreSecretRequest(this);
        store->setSecretStorageType(StoreSecretRequest::CollectionSecret);
        QByteArray data(8, '\0');
        qToBigEndian<quint64>(generations[previous] + 1, reinterpret_cast<uchar *>(data.data()));
        data.append(value);
        Secret secret(identifier(target));
        secret.setType(Secret::TypeBlob);
        secret.setData(data);
        store->setSecret(secret);
        start(store, [this, previous](const Result &result) {
            if (result.code() != Result::Succeeded) {
                finish(kInternal);
                return;
            }
            // The new value is committed. If retiring the previous one fails,
            // the next write or removal will retry before changing the new one.
            removeSlot(previous, [this](bool) { finish(kOk); });
        });
    }

    void startOperation() {
        if (action == LP3_SECRET_DESTROY) {
            auto *destroy = new DeleteCollectionRequest(this);
            destroy->setCollectionName(collection);
            destroy->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
            start(destroy, [this](const Result &result) {
                finish(result.code() == Result::Succeeded || missing(result) ? kOk : kInternal);
            });
        } else if (action == LP3_SECRET_SET) {
            auto *create = new CreateCollectionRequest(this);
            create->setCollectionName(collection);
            create->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
            create->setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
            create->setCollectionLockType(CreateCollectionRequest::DeviceLock);
            create->setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
            create->setAccessControlMode(SecretManager::OwnerOnlyMode);
            start(create, [this](const Result &result) {
                if (result.code() == Result::Succeeded || result.errorCode() == Result::CollectionAlreadyExistsError)
                    readSlot(0);
                else finish(kInternal);
            });
        } else {
            readSlot(0);
        }
    }
};

SecureStorage::SecureStorage(const CompleteCallback &complete, QObject *parent)
    : QObject(parent), d(new Private(complete, this)) {}

SecureStorage::~SecureStorage() {}

void SecureStorage::request(uint64_t id, const lp3wire::SecureStorageData &input) {
    if (d->requestId != 0) {
        d->complete(id, LP3_PLATFORM_BUSY, false, std::vector<uint8_t>());
        return;
    }
    d->requestId = id;
    d->action = input.action;
    d->collection = collectionName(input.uuid);
    d->key = secretName(input.key);
    d->value = QByteArray(reinterpret_cast<const char *>(input.value.data()),
                          static_cast<int>(input.value.size()));
    d->cancelled = false;
    d->startOperation();
}

void SecureStorage::cancel(uint64_t id) {
    if (d->requestId != id)
        return;
    d->cancelled = true;
}
