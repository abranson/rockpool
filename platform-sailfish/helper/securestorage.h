/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The helper-side Sailfish Secrets adapter.  Requests are deliberately kept
 * asynchronous because the helper also owns the provider socket event loop.
 */
#ifndef ROCKPOOL_SAILFISH_SECURE_STORAGE_H
#define ROCKPOOL_SAILFISH_SECURE_STORAGE_H

#include <QObject>
#include <QScopedPointer>

#include <vector>
#include <cstdint>
#include <functional>

#include "../common/wire.h"

class SecureStorage : public QObject {
public:
    typedef std::function<void(uint64_t, uint32_t, bool,
                               const std::vector<uint8_t> &)> CompleteCallback;

    explicit SecureStorage(const CompleteCallback &complete, QObject *parent = 0);
    ~SecureStorage();

    void request(uint64_t requestId, const lp3wire::SecureStorageData &request);
    void cancel(uint64_t requestId);

private:
    class Private;
    QScopedPointer<Private> d;
};

#endif
