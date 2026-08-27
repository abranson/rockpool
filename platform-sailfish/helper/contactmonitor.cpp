/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "contactmonitor.h"

#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>

#include <QtContacts/QContact>
#include <QtContacts/QContactDetailFilter>
#include <QtContacts/QContactDisplayLabel>
#include <QtContacts/QContactManager>
#include <QtContacts/QContactPhoneNumber>

#include <algorithm>
#include <vector>

#include "libpebble3d-platform.h"

using namespace QtContacts;

namespace {

std::string boundedUtf8(const QString &value, size_t maximum) {
    QByteArray bytes = value.toUtf8();
    if (bytes.size() > static_cast<int>(maximum)) {
        bytes.truncate(static_cast<int>(maximum));
        while (!bytes.isEmpty() && !lp3wire::validUtf8(std::string(
                   bytes.constData(), static_cast<size_t>(bytes.size())))) {
            bytes.chop(1);
        }
    }
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

bool contactLess(const lp3wire::ContactData &left,
                 const lp3wire::ContactData &right) {
    const int nameOrder = QString::fromUtf8(left.displayName.data(),
                                             left.displayName.size()).localeAwareCompare(
        QString::fromUtf8(right.displayName.data(), right.displayName.size()));
    return nameOrder < 0 || (nameOrder == 0 && left.id < right.id);
}

QString firstPhoneNumber(const QContact &contact) {
    const QList<QContactPhoneNumber> numbers =
        contact.details<QContactPhoneNumber>();
    for (QList<QContactPhoneNumber>::const_iterator it = numbers.constBegin();
         it != numbers.constEnd(); ++it) {
        if (!it->number().trimmed().isEmpty()) {
            return it->number().trimmed();
        }
    }
    return QString();
}

bool convertContact(const QContact &contact, lp3wire::ContactData *data) {
    if (data == 0 || contact.id().isNull()) {
        return false;
    }
    const QString name = contact.detail<QContactDisplayLabel>().label().trimmed();
    if (name.isEmpty()) {
        return false;
    }
    data->flags = 0;
    data->id = boundedUtf8(contact.id().toString(), lp3wire::kContactIdMax);
    data->displayName = boundedUtf8(name, lp3wire::kContactNameMax);
    data->phoneNumber = boundedUtf8(
        firstPhoneNumber(contact), lp3wire::kContactNumberMax);
    data->avatar.clear();
    return lp3wire::validContact(*data);
}

} // namespace

class ContactCancelState {
public:
    void cancel(quint64 requestId) {
        QMutexLocker locker(&mutex);
        cancelled.insert(requestId);
    }

    bool isCancelled(quint64 requestId) {
        QMutexLocker locker(&mutex);
        return cancelled.contains(requestId);
    }

    void retire(quint64 requestId) {
        QMutexLocker locker(&mutex);
        cancelled.remove(requestId);
    }

private:
    QMutex mutex;
    QSet<quint64> cancelled;
};

class ContactWorker : public QObject {
    Q_OBJECT

public:
    explicit ContactWorker(ContactCancelState *cancelState)
        : m_cancelState(cancelState), m_manager(0), m_ready(false) {}

public slots:
    void start() {
        if (m_ready) return;
        QMap<QString, QString> parameters;
        parameters.insert(QStringLiteral("mergePresenceChanges"),
                          QStringLiteral("false"));
        m_manager = new QContactManager(
            QStringLiteral("org.nemomobile.contacts.sqlite"), parameters, this);
        if (m_manager->error() != QContactManager::NoError) {
            delete m_manager;
            m_manager = 0;
            emit healthChanged(false);
            return;
        }
        connect(m_manager, &QContactManager::contactsAdded,
                this, &ContactWorker::sourceChanged);
        connect(m_manager, &QContactManager::contactsChanged,
                this, &ContactWorker::sourceChanged);
        connect(m_manager, &QContactManager::contactsRemoved,
                this, &ContactWorker::sourceChanged);
        connect(m_manager, &QContactManager::dataChanged,
                this, &ContactWorker::sourceChanged);
        m_ready = true;
        emit healthChanged(true);
    }

    void stop() {
        m_ready = false;
        delete m_manager;
        m_manager = 0;
        emit stopped();
    }

    void query(quint64 requestId, quint32 kind, quint32 maxRecords,
               quint32 offset, const QString &queryText) {
        lp3wire::ContactReplyData reply;
        reply.kind = kind;
        reply.nextOffset = 0;
        int32_t status = LP3_PLATFORM_OK;

        if (m_cancelState->isCancelled(requestId)) {
            m_cancelState->retire(requestId);
            return;
        }
        if (!m_ready || m_manager == 0) {
            status = LP3_PLATFORM_UNAVAILABLE;
        } else if (kind == lp3wire::ContactQueryList) {
            status = queryList(requestId, maxRecords, offset, &reply);
        } else {
            status = queryPhone(requestId, queryText, &reply);
        }
        if (m_cancelState->isCancelled(requestId)) {
            m_cancelState->retire(requestId);
            return;
        }
        if (status != LP3_PLATFORM_OK) {
            reply.nextOffset = 0;
            reply.contacts.clear();
        }
        std::vector<uint8_t> payload;
        if (!lp3wire::encodeContactReply(
                static_cast<uint32_t>(status), reply, &payload)) {
            reply.nextOffset = 0;
            reply.contacts.clear();
            payload.clear();
            lp3wire::encodeContactReply(
                LP3_PLATFORM_PROTOCOL_ERROR, reply, &payload);
            status = LP3_PLATFORM_PROTOCOL_ERROR;
        }
        const QByteArray bytes(
            reinterpret_cast<const char *>(&payload[0]),
            static_cast<int>(payload.size()));
        m_cancelState->retire(requestId);
        emit completed(requestId, status, bytes);
    }

signals:
    void completed(quint64 requestId, int status, const QByteArray &payload);
    void contactsChanged();
    void healthChanged(bool ready);
    void stopped();

private slots:
    void sourceChanged() {
        emit contactsChanged();
    }

private:
    int32_t queryList(quint64 requestId, quint32 maxRecords, quint32 offset,
                      lp3wire::ContactReplyData *reply) {
        const QList<QContact> contacts = m_manager->contacts();
        if (m_manager->error() != QContactManager::NoError) {
            return LP3_PLATFORM_UNAVAILABLE;
        }
        std::vector<lp3wire::ContactData> all;
        for (QList<QContact>::const_iterator it = contacts.constBegin();
             it != contacts.constEnd(); ++it) {
            if (m_cancelState->isCancelled(requestId)) {
                return LP3_PLATFORM_CANCELLED;
            }
            lp3wire::ContactData contact;
            if (convertContact(*it, &contact)) {
                all.push_back(contact);
                if (all.size() > lp3wire::kContactTotalMax) {
                    return LP3_PLATFORM_BUSY;
                }
            }
        }
        std::sort(all.begin(), all.end(), contactLess);
        const size_t begin = qMin(static_cast<size_t>(offset), all.size());
        const size_t end = qMin(begin + static_cast<size_t>(maxRecords),
                                all.size());
        reply->contacts.assign(all.begin() + begin, all.begin() + end);
        reply->nextOffset = end < all.size() ? static_cast<uint32_t>(end) : 0;
        return LP3_PLATFORM_OK;
    }

    int32_t queryPhone(quint64 requestId, const QString &number,
                       lp3wire::ContactReplyData *reply) {
        if (m_cancelState->isCancelled(requestId)) {
            return LP3_PLATFORM_CANCELLED;
        }
        QContactDetailFilter filter;
        filter.setDetailType(QContactDetail::TypePhoneNumber,
                             QContactPhoneNumber::FieldNumber);
        filter.setValue(number);
        filter.setMatchFlags(QContactFilter::MatchPhoneNumber);
        const QList<QContact> contacts = m_manager->contacts(filter);
        if (m_manager->error() != QContactManager::NoError) {
            return LP3_PLATFORM_UNAVAILABLE;
        }
        std::vector<lp3wire::ContactData> matches;
        for (QList<QContact>::const_iterator it = contacts.constBegin();
             it != contacts.constEnd(); ++it) {
            lp3wire::ContactData contact;
            if (convertContact(*it, &contact)) matches.push_back(contact);
        }
        std::sort(matches.begin(), matches.end(), contactLess);
        if (!matches.empty()) reply->contacts.push_back(matches.front());
        return LP3_PLATFORM_OK;
    }

    ContactCancelState *m_cancelState;
    QContactManager *m_manager;
    bool m_ready;
};

ContactMonitor::ContactMonitor(const CompletedCallback &completed,
                               const ChangedCallback &changed,
                               const HealthCallback &health,
                               QObject *parent)
    : QObject(parent),
      m_cancelState(new ContactCancelState),
      m_worker(new ContactWorker(m_cancelState)),
      m_completed(completed),
      m_changed(changed),
      m_health(health),
      m_started(false),
      m_ready(false) {
    m_worker->moveToThread(&m_thread);
    connect(this, &ContactMonitor::startRequested,
            m_worker, &ContactWorker::start);
    connect(this, &ContactMonitor::stopRequested,
            m_worker, &ContactWorker::stop);
    connect(this, &ContactMonitor::queryRequested,
            m_worker, &ContactWorker::query);
    connect(m_worker, &ContactWorker::completed, this,
            [this](quint64 id, int status, const QByteArray &payload) {
                m_completed(id, static_cast<int32_t>(status), payload);
            });
    connect(m_worker, &ContactWorker::contactsChanged, this,
            [this]() { m_changed(); });
    connect(m_worker, &ContactWorker::healthChanged, this,
            [this](bool ready) {
                m_ready = ready;
                m_health(ready);
            });
    // The main thread waits in the destructor, so quit must run in the worker thread.
    connect(m_worker, &ContactWorker::stopped,
            &m_thread, &QThread::quit, Qt::DirectConnection);
}

ContactMonitor::~ContactMonitor() {
    if (m_started) {
        emit stopRequested();
        m_thread.wait();
    }
    delete m_worker;
    delete m_cancelState;
}

bool ContactMonitor::start() {
    if (!m_started) {
        m_started = true;
        m_thread.start();
        emit startRequested();
    }
    return m_ready;
}

void ContactMonitor::query(quint64 requestId,
                           const lp3wire::ContactQueryData &query) {
    emit queryRequested(requestId, query.kind, query.maxRecords, query.offset,
                        QString::fromUtf8(query.query.data(),
                                          static_cast<int>(query.query.size())));
}

bool ContactMonitor::cancel(quint64 requestId) {
    if (!m_started || m_cancelState == 0) return false;
    m_cancelState->cancel(requestId);
    return true;
}

#include "contactmonitor.moc"
