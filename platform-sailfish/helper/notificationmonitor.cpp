/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "notificationmonitor.h"

#include <QByteArray>
#include <QDateTime>
#include <QDataStream>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariant>

#include <dbus/dbus.h>

#include <string.h>

#include "libpebble3d-platform.h"
#include "wire.h"

namespace {

const char kNotificationsInterface[] = "org.freedesktop.Notifications";
const char kNotificationsService[] = "org.freedesktop.Notifications";
const char kNotificationsPath[] = "/org/freedesktop/Notifications";
const char kCommHistoryService[] = "org.nemomobile.CommHistory";
const char kMessagesService[] = "org.sailfishos.Messages";
const char kMessagesPath[] = "/";
const char kMessagesInterface[] = "org.sailfishos.Messages";
const char kMessagesMethod[] = "sendMessage";
const char kAccountPathPrefix[] = "/org/freedesktop/Telepathy/Account/";
const int kMaximumPending = 64;
const int kMaximumActive = 32;
const int kMaximumActions = 64;
const int kMaximumHints = 64;
const size_t kMaximumHintName = 64;
const size_t kMaximumRemoteAction = 4096;
const int kMaximumActionText = 256;
const int kMaximumEncodedArgument = 2048;
const int kMaximumAccountPathBytes = 512;
const int kMaximumRecipientBytes = 512;
const int kCommandTimeoutMs = 1000;
const int kPendingTimeoutMs = 5000;

struct PendingNotification {
    QString sender;
    QString appName;
    uint32_t replacesId;
    QString appIcon;
    QString summary;
    QString body;
    QHash<QString, QVariant> hints;
    QStringList actions;

    PendingNotification() : replacesId(0) {}
};

struct ReplyTarget {
    QString sourceOwner;
    QString accountPath;
    QString recipient;
};

QString pendingKey(const char *name, dbus_uint32_t serial) {
    return QString::fromUtf8(name == NULL ? "" : name) + QLatin1Char('#') +
        QString::number(serial);
}

QString stringHint(const QHash<QString, QVariant> &hints, const char *name) {
    return hints.value(QString::fromLatin1(name)).toString();
}

bool boolHint(const QHash<QString, QVariant> &hints, const char *name) {
    const QVariant value = hints.value(QString::fromLatin1(name));
    return value.type() == QVariant::Bool ? value.toBool() :
        value.toString().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

bool getNameOwner(DBusConnection *connection, const char *name,
                  QString *owner, DBusError *error) {
    DBusMessage *message = dbus_message_new_method_call(
        DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS,
        "GetNameOwner");
    const char *nameArgument = name;
    if (message == NULL || !dbus_message_append_args(
            message, DBUS_TYPE_STRING, &nameArgument,
            DBUS_TYPE_INVALID)) {
        if (message != NULL) {
            dbus_message_unref(message);
        }
        return false;
    }
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        connection, message, kCommandTimeoutMs, error);
    dbus_message_unref(message);
    if (reply == NULL) {
        return false;
    }
    const char *value = NULL;
    const bool succeeded = dbus_message_get_args(
        reply, error, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) != FALSE;
    if (succeeded) {
        *owner = QString::fromUtf8(value == NULL ? "" : value);
    }
    dbus_message_unref(reply);
    return succeeded && !owner->isEmpty();
}

QString normalizedApplicationId(const PendingNotification &pending) {
    QString id = stringHint(pending.hints, "x-nemo-origin-package");
    if (id.isEmpty()) {
        id = stringHint(pending.hints, "x-nemo-owner");
    }
    if (id.isEmpty()) {
        id = stringHint(pending.hints, "desktop-entry");
    }
    if (id.isEmpty()) {
        id = pending.appName.toLower();
        id.replace(QLatin1Char(' '), QLatin1Char('-'));
    }
    return id.isEmpty() ? QStringLiteral("unknown") : id;
}

QString themeIconName(const QString &value) {
    if (value.startsWith(QStringLiteral("image://theme/"))) {
        return value.mid(14);
    }
    if (value.startsWith(QLatin1Char('/')) ||
        value.startsWith(QStringLiteral("file:"))) {
        return QString();
    }
    return value;
}

bool readBasicString(DBusMessageIter *iterator, QString *value,
                     size_t maximum) {
    const char *text = NULL;
    if (dbus_message_iter_get_arg_type(iterator) != DBUS_TYPE_STRING) {
        return false;
    }
    dbus_message_iter_get_basic(iterator, &text);
    const size_t length = text == NULL ? 0 : strlen(text);
    if (length > maximum) {
        return false;
    }
    *value = QString::fromUtf8(text == NULL ? "" : text,
                              static_cast<int>(length));
    return true;
}

QVariant readVariant(DBusMessageIter *variant, size_t maximumString) {
    DBusMessageIter value;
    dbus_message_iter_recurse(variant, &value);
    switch (dbus_message_iter_get_arg_type(&value)) {
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE: {
        const char *text = NULL;
        dbus_message_iter_get_basic(&value, &text);
        const size_t length = text == NULL ? 0 : strlen(text);
        if (length > maximumString) {
            return QVariant();
        }
        return QString::fromUtf8(text == NULL ? "" : text,
                                 static_cast<int>(length));
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t boolean = FALSE;
        dbus_message_iter_get_basic(&value, &boolean);
        return boolean != FALSE;
    }
    case DBUS_TYPE_BYTE: {
        unsigned char byte = 0;
        dbus_message_iter_get_basic(&value, &byte);
        return static_cast<uint>(byte);
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t number = 0;
        dbus_message_iter_get_basic(&value, &number);
        return static_cast<int>(number);
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t number = 0;
        dbus_message_iter_get_basic(&value, &number);
        return static_cast<uint>(number);
    }
    default:
        return QVariant();
    }
}

size_t maximumHintString(const QString &name) {
    if (name == QStringLiteral("x-nemo-origin-package") ||
        name == QStringLiteral("x-nemo-owner") ||
        name == QStringLiteral("desktop-entry")) {
        return lp3wire::kNotificationApplicationIdMax;
    }
    if (name == QStringLiteral("category")) {
        return lp3wire::kNotificationCategoryMax;
    }
    if (name == QStringLiteral("x-nemo-icon") ||
        name == QStringLiteral("x-nemo-preview-icon")) {
        return lp3wire::kNotificationIconNameMax;
    }
    if (name == QStringLiteral("x-nemo-timestamp")) {
        return 64;
    }
    if (name == QStringLiteral("transient") ||
        name == QStringLiteral("x-nemo-hidden")) {
        return 5;
    }
    return 0;
}

size_t dynamicActionHintMaximum(const PendingNotification &pending,
                                const QString &name) {
    const QString actionPrefix = QStringLiteral("x-nemo-remote-action-");
    const QString typePrefix = QStringLiteral("x-nemo-remote-action-type-");
    QString action;
    size_t maximum = 0;
    if (name.startsWith(typePrefix)) {
        action = name.mid(typePrefix.size());
        maximum = 16;
    } else if (name.startsWith(actionPrefix)) {
        action = name.mid(actionPrefix.size());
        maximum = kMaximumRemoteAction;
    }
    if (action.isEmpty()) {
        return 0;
    }
    for (int index = 0; index + 1 < pending.actions.size(); index += 2) {
        if (pending.actions.at(index) == action) {
            return maximum;
        }
    }
    return 0;
}

uint32_t bigEndian32(const QByteArray &bytes, int offset) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(bytes.at(offset))) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(bytes.at(offset + 1))) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(bytes.at(offset + 2))) << 8) |
        static_cast<uint32_t>(static_cast<uint8_t>(bytes.at(offset + 3)));
}

bool exactSerializedQString(const QByteArray &bytes, int offset,
                            int maximumUtf16Bytes) {
    if (offset < 0 || bytes.size() - offset < 4) {
        return false;
    }
    const uint32_t length = bigEndian32(bytes, offset);
    return length != UINT32_MAX && (length % 2) == 0 &&
        length <= static_cast<uint32_t>(maximumUtf16Bytes) &&
        length == static_cast<uint32_t>(bytes.size() - offset - 4);
}

bool decodeCanonicalStringVariant(const QString &token, QString *value) {
    const QByteArray encoded = token.toLatin1();
    if (value == NULL || encoded.isEmpty() ||
        encoded.size() > kMaximumEncodedArgument ||
        QString::fromLatin1(encoded) != token || (encoded.size() % 4) != 0) {
        return false;
    }
    int padding = 0;
    bool sawPadding = false;
    for (int index = 0; index < encoded.size(); ++index) {
        const char character = encoded.at(index);
        if (character == '=') {
            sawPadding = true;
            ++padding;
            if (padding > 2) {
                return false;
            }
        } else if (sawPadding ||
                   !((character >= 'A' && character <= 'Z') ||
                     (character >= 'a' && character <= 'z') ||
                     (character >= '0' && character <= '9') ||
                     character == '+' || character == '/')) {
            return false;
        }
    }
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    if (decoded.isEmpty() || decoded.toBase64() != encoded ||
        decoded.size() < 9 || decoded.at(4) != 0 ||
        bigEndian32(decoded, 0) !=
            static_cast<uint32_t>(QVariant::String) ||
        !exactSerializedQString(decoded, 5,
                                2 * kMaximumAccountPathBytes)) {
        return false;
    }
    QDataStream stream(decoded);
    QVariant decodedValue;
    stream >> decodedValue;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
        !decodedValue.isValid()) {
        return false;
    }
    if (decodedValue.type() != QVariant::String) {
        return false;
    }
    *value = decodedValue.toString();
    return true;
}

bool validBoundedText(const QString &value, int maximumBytes) {
    const QByteArray bytes = value.toUtf8();
    return !value.isEmpty() && !value.contains(QChar(0)) &&
        bytes.size() <= maximumBytes &&
        lp3wire::validUtf8(std::string(
            bytes.constData(), static_cast<size_t>(bytes.size())));
}

bool replyTarget(const PendingNotification &pending, const QString &category,
                 const QString &commHistoryOwner, ReplyTarget *target) {
    if (target == NULL || commHistoryOwner.isEmpty() ||
        pending.sender != commHistoryOwner ||
        (category != QStringLiteral("x-nemo.messaging.sms") &&
         category != QStringLiteral("x-nemo.messaging.im") &&
         category != QStringLiteral("x-nemo.messaging.mms"))) {
        return false;
    }
    int candidates = 0;
    ReplyTarget candidate;
    for (int index = 0; index + 1 < pending.actions.size(); index += 2) {
        const QString action = pending.actions.at(index);
        const QString remoteName =
            QStringLiteral("x-nemo-remote-action-") + action;
        const QString typeName =
            QStringLiteral("x-nemo-remote-action-type-") + action;
        if (pending.hints.value(typeName).toString() != QStringLiteral("input")) {
            continue;
        }
        const QString remote = pending.hints.value(remoteName).toString();
        const QStringList parts = remote.split(QLatin1Char(' '),
                                               QString::KeepEmptyParts);
        if (parts.size() != 6 ||
            parts.at(0) != QString::fromLatin1(kMessagesService) ||
            parts.at(1) != QString::fromLatin1(kMessagesPath) ||
            parts.at(2) != QString::fromLatin1(kMessagesInterface) ||
            parts.at(3) != QString::fromLatin1(kMessagesMethod) ||
            !decodeCanonicalStringVariant(parts.at(4),
                                          &candidate.accountPath) ||
            !decodeCanonicalStringVariant(parts.at(5),
                                          &candidate.recipient)) {
            continue;
        }
        const QByteArray accountBytes = candidate.accountPath.toUtf8();
        if (!validBoundedText(candidate.accountPath,
                              kMaximumAccountPathBytes) ||
            !candidate.accountPath.startsWith(
                QString::fromLatin1(kAccountPathPrefix)) ||
            candidate.accountPath.size() <=
                static_cast<int>(strlen(kAccountPathPrefix)) ||
            !dbus_validate_path(accountBytes.constData(), NULL) ||
            !validBoundedText(candidate.recipient,
                              kMaximumRecipientBytes)) {
            continue;
        }
        ++candidates;
        candidate.sourceOwner = pending.sender;
        *target = candidate;
    }
    return candidates == 1;
}

DBusMessage *createReplyMessage(const ReplyTarget &target,
                                const QByteArray &text) {
    const QByteArray account = target.accountPath.toUtf8();
    const QByteArray recipient = target.recipient.toUtf8();
    const char *accountValue = account.constData();
    const char *recipientValue = recipient.constData();
    const char *textValue = text.constData();
    DBusMessage *message = dbus_message_new_method_call(
        kMessagesService, kMessagesPath, kMessagesInterface,
        kMessagesMethod);
    if (message == NULL || !dbus_message_append_args(
            message,
            DBUS_TYPE_STRING, &accountValue,
            DBUS_TYPE_STRING, &recipientValue,
            DBUS_TYPE_STRING, &textValue,
            DBUS_TYPE_INVALID)) {
        if (message != NULL) {
            dbus_message_unref(message);
        }
        return NULL;
    }
    return message;
}

} // namespace

class NotificationMonitorPrivate {
public:
    struct WatchData {
        NotificationMonitorPrivate *monitor;
        DBusWatch *watch;
        QSocketNotifier *read;
        QSocketNotifier *write;

        WatchData() : monitor(NULL), watch(NULL), read(NULL), write(NULL) {}
    };

    NotificationMonitorPrivate(NotificationMonitor *owner,
                               const NotificationMonitor::PostedCallback &posted,
                               const NotificationMonitor::ClosedCallback &closed,
                               const NotificationMonitor::HealthCallback &health,
                               const NotificationMonitor::HealthCallback &messagingHealth)
        : q(owner), connection(NULL), postedCallback(posted),
          closedCallback(closed), healthCallback(health),
          messagingHealthCallback(messagingHealth), started(false),
          available(false), messagingAvailable(false), retryScheduled(false),
          shuttingDown(false), connectionGeneration(0) {}

    ~NotificationMonitorPrivate() {
        shuttingDown = true;
        closeConnection();
    }

    bool start() {
        if (started) {
            return available;
        }
        DBusError error = DBUS_ERROR_INIT;
        connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        if (connection == NULL) {
            dbus_error_free(&error);
            scheduleReconnect();
            return false;
        }
        ++connectionGeneration;
        dbus_connection_set_exit_on_disconnect(connection, FALSE);
        if (!dbus_connection_set_watch_functions(
                connection, addWatch, removeWatch, toggleWatch, this, NULL) ||
            !dbus_connection_add_filter(connection, messageFilter, this, NULL)) {
            closeConnection();
            scheduleReconnect();
            return false;
        }
        const char *rules[] = {
            "type='method_call',interface='org.freedesktop.Notifications',"
            "member='Notify',eavesdrop='true'",
            // Sailfish's older dbus-daemon does not resolve a well-known name
            // in an eavesdrop sender match. Match replies broadly, then accept
            // only the currently owned unique service name in messageFilter().
            "type='method_return',eavesdrop='true'",
            "type='error',eavesdrop='true'",
            "type='signal',"
            "path='/org/freedesktop/Notifications',"
            "interface='org.freedesktop.Notifications',"
            "member='NotificationClosed'",
            "type='signal',sender='org.freedesktop.DBus',"
            "path='/org/freedesktop/DBus',"
            "interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',"
            "arg0='org.freedesktop.Notifications'",
            "type='signal',sender='org.freedesktop.DBus',"
            "path='/org/freedesktop/DBus',"
            "interface='org.freedesktop.DBus',"
            "member='NameOwnerChanged',"
            "arg0='org.nemomobile.CommHistory'",
        };
        for (size_t index = 0; index < sizeof(rules) / sizeof(rules[0]); ++index) {
            dbus_bus_add_match(connection, rules[index], &error);
            if (dbus_error_is_set(&error)) {
                dbus_error_free(&error);
                closeConnection();
                scheduleReconnect();
                return false;
            }
        }
        dbus_connection_flush(connection);
        started = true;
        const bool hasOwner = dbus_bus_name_has_owner(
            connection, kNotificationsService, &error) != FALSE;
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
            closeConnection();
            scheduleReconnect();
            return false;
        }
        if (hasOwner) {
            QString owner;
            if (!getNameOwner(connection, kNotificationsService, &owner,
                              &error) || dbus_error_is_set(&error)) {
                dbus_error_free(&error);
                closeConnection();
                scheduleReconnect();
                return false;
            }
            serviceOwner = owner;
        } else {
            serviceOwner.clear();
        }
        const bool hasCommHistoryOwner = dbus_bus_name_has_owner(
            connection, kCommHistoryService, &error) != FALSE;
        if (dbus_error_is_set(&error)) {
            dbus_error_free(&error);
            closeConnection();
            scheduleReconnect();
            return false;
        }
        if (hasCommHistoryOwner) {
            QString owner;
            if (!getNameOwner(connection, kCommHistoryService, &owner,
                              &error) || dbus_error_is_set(&error)) {
                dbus_error_free(&error);
                closeConnection();
                scheduleReconnect();
                return false;
            }
            commHistoryOwner = owner;
        } else {
            commHistoryOwner.clear();
        }
        setAvailable(hasOwner);
        return available;
    }

    int32_t command(uint32_t commandValue, const QString &id) {
        bool ok = false;
        const uint32_t numericId = id.toUInt(&ok);
        if (!started || !available || !ok || numericId == 0) {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
        if (commandValue == LP3_PLATFORM_NOTIFICATION_DISMISS) {
            if (!activeIds.contains(numericId)) {
                return LP3_PLATFORM_UNAVAILABLE;
            }
            DBusMessage *message = dbus_message_new_method_call(
                kNotificationsService, kNotificationsPath,
                kNotificationsInterface, "CloseNotification");
            if (message == NULL || !dbus_message_append_args(
                    message, DBUS_TYPE_UINT32, &numericId,
                    DBUS_TYPE_INVALID)) {
                if (message != NULL) {
                    dbus_message_unref(message);
                }
                return LP3_PLATFORM_INTERNAL_ERROR;
            }
            DBusError error = DBUS_ERROR_INIT;
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                connection, message, kCommandTimeoutMs, &error);
            dbus_message_unref(message);
            if (reply == NULL) {
                const bool disconnected =
                    !dbus_connection_get_is_connected(connection);
                dbus_error_free(&error);
                if (disconnected) {
                    busDisconnected();
                }
                return LP3_PLATFORM_IO_ERROR;
            }
            const bool succeeded =
                dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN;
            dbus_message_unref(reply);
            dbus_error_free(&error);
            if (!succeeded) {
                return LP3_PLATFORM_IO_ERROR;
            }
            removeActive(numericId);
            return LP3_PLATFORM_OK;
        }
        if (commandValue == LP3_PLATFORM_NOTIFICATION_OPEN) {
            // A notification hint is not authority for this privileged-group
            // process to issue an arbitrary D-Bus call. A future open action
            // must use one fixed Sailfish application-launcher API.
            return LP3_PLATFORM_NOT_SUPPORTED;
        }
        return LP3_PLATFORM_INVALID_ARGUMENT;
    }

    int32_t reply(const QString &id, const QString &text) {
        bool ok = false;
        const uint32_t numericId = id.toUInt(&ok);
        const QByteArray textBytes = text.toUtf8();
        if (!ok || numericId == 0 ||
            !validBoundedText(text,
                              static_cast<int>(LP3_PLATFORM_MESSAGE_TEXT_MAX))) {
            return LP3_PLATFORM_INVALID_ARGUMENT;
        }
        if (!started || !available) {
            return LP3_PLATFORM_UNAVAILABLE;
        }
        if (!replyTargets.contains(numericId)) {
            return LP3_PLATFORM_UNAVAILABLE;
        }

        const ReplyTarget target = replyTargets.value(numericId);
        DBusError ownerError = DBUS_ERROR_INIT;
        QString currentOwner;
        const bool ownerCurrent = getNameOwner(
            connection, kCommHistoryService, &currentOwner, &ownerError) &&
            !dbus_error_is_set(&ownerError) &&
            currentOwner == commHistoryOwner &&
            currentOwner == target.sourceOwner;
        dbus_error_free(&ownerError);
        if (!ownerCurrent) {
            setCommHistoryOwner(currentOwner);
            return LP3_PLATFORM_UNAVAILABLE;
        }

        // Sending is externally visible and cannot be made idempotent. Consume
        // the authority before dispatch, and never restore it after an
        // ambiguous timeout or error.
        replyTargets.remove(numericId);
        DBusMessage *message = createReplyMessage(target, textBytes);
        if (message == NULL) {
            return LP3_PLATFORM_INTERNAL_ERROR;
        }

        DBusError error = DBUS_ERROR_INIT;
        DBusMessage *response = dbus_connection_send_with_reply_and_block(
            connection, message, kCommandTimeoutMs, &error);
        dbus_message_unref(message);
        if (response == NULL) {
            const bool disconnected =
                !dbus_connection_get_is_connected(connection);
            dbus_error_free(&error);
            if (disconnected) {
                busDisconnected();
            }
            return LP3_PLATFORM_IO_ERROR;
        }
        const bool succeeded = dbus_message_get_type(response) ==
                DBUS_MESSAGE_TYPE_METHOD_RETURN &&
            dbus_message_get_signature(response)[0] == '\0';
        dbus_message_unref(response);
        dbus_error_free(&error);
        return succeeded ? LP3_PLATFORM_OK : LP3_PLATFORM_IO_ERROR;
    }

private:
    static dbus_bool_t addWatch(DBusWatch *watch, void *context) {
        NotificationMonitorPrivate *self =
            static_cast<NotificationMonitorPrivate *>(context);
        WatchData *data = new WatchData;
        data->monitor = self;
        data->watch = watch;
        const int descriptor = dbus_watch_get_unix_fd(watch);
        const unsigned int flags = dbus_watch_get_flags(watch);
        if ((flags & DBUS_WATCH_READABLE) != 0) {
            data->read = new QSocketNotifier(
                descriptor, QSocketNotifier::Read, self->q);
            QObject::connect(
                data->read,
                &QSocketNotifier::activated,
                self->q,
                [data](int) { data->monitor->watchActivated(
                    data, DBUS_WATCH_READABLE); });
        }
        if ((flags & DBUS_WATCH_WRITABLE) != 0) {
            data->write = new QSocketNotifier(
                descriptor, QSocketNotifier::Write, self->q);
            QObject::connect(
                data->write,
                &QSocketNotifier::activated,
                self->q,
                [data](int) { data->monitor->watchActivated(
                    data, DBUS_WATCH_WRITABLE); });
        }
        dbus_watch_set_data(watch, data, NULL);
        toggleWatch(watch, context);
        return TRUE;
    }

    static void removeWatch(DBusWatch *watch, void *) {
        WatchData *data = static_cast<WatchData *>(dbus_watch_get_data(watch));
        if (data == NULL) {
            return;
        }
        delete data->read;
        delete data->write;
        delete data;
        dbus_watch_set_data(watch, NULL, NULL);
    }

    static void toggleWatch(DBusWatch *watch, void *) {
        WatchData *data = static_cast<WatchData *>(dbus_watch_get_data(watch));
        if (data == NULL) {
            return;
        }
        const bool enabled = dbus_watch_get_enabled(watch);
        if (data->read != NULL) {
            data->read->setEnabled(enabled);
        }
        if (data->write != NULL) {
            data->write->setEnabled(enabled);
        }
    }

    void watchActivated(WatchData *data, unsigned int flag) {
        if (data == NULL || data->watch == NULL || connection == NULL) {
            return;
        }
        if (!dbus_watch_handle(data->watch, flag)) {
            return;
        }
        while (dbus_connection_get_dispatch_status(connection) ==
               DBUS_DISPATCH_DATA_REMAINS) {
            dbus_connection_dispatch(connection);
        }
        toggleWatch(data->watch, this);
    }

    void closeConnection() {
        started = false;
        serviceOwner.clear();
        commHistoryOwner.clear();
        ++connectionGeneration;
        if (connection != NULL) {
            dbus_connection_remove_filter(connection, messageFilter, this);
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
            connection = NULL;
        }
    }

    void scheduleReconnect() {
        if (retryScheduled || shuttingDown) {
            return;
        }
        retryScheduled = true;
        QTimer::singleShot(1000, q, [this]() {
            retryScheduled = false;
            if (shuttingDown) {
                return;
            }
            start();
            if (connection == NULL) {
                scheduleReconnect();
            }
        });
    }

    void clearTrackedNotifications() {
        const QList<uint32_t> ids = activeOrder;
        activeIds.clear();
        replyTargets.clear();
        activeOrder.clear();
        pending.clear();
        for (QList<uint32_t>::const_iterator it = ids.begin();
             it != ids.end(); ++it) {
            emitClosed(*it, 0);
        }
    }

    void setAvailable(bool value) {
        if (available == value) {
            return;
        }
        available = value;
        healthCallback(available);
        updateMessagingAvailable();
    }

    void updateMessagingAvailable() {
        const bool value = available && !commHistoryOwner.isEmpty();
        if (messagingAvailable == value) {
            return;
        }
        messagingAvailable = value;
        messagingHealthCallback(value);
    }

    void setCommHistoryOwner(const QString &owner) {
        if (commHistoryOwner == owner) {
            return;
        }
        // A same-state owner replacement is still a reply-authority generation
        // boundary. Publish loss before accepting capabilities from the new owner.
        if (messagingAvailable) {
            messagingAvailable = false;
            messagingHealthCallback(false);
        }
        replyTargets.clear();
        commHistoryOwner = owner;
        updateMessagingAvailable();
    }

    void serviceOwnerChanged(const char *oldOwner, const char *newOwner) {
        const bool hadOwner = oldOwner != NULL && oldOwner[0] != '\0';
        const bool hasOwner = newOwner != NULL && newOwner[0] != '\0';
        if (hadOwner) {
            clearTrackedNotifications();
            serviceOwner.clear();
            setAvailable(false);
        }
        if (hasOwner) {
            serviceOwner = QString::fromUtf8(newOwner);
            setAvailable(true);
        }
    }

    void commHistoryOwnerChanged(const char *, const char *newOwner) {
        setCommHistoryOwner(QString::fromUtf8(
            newOwner == NULL ? "" : newOwner));
    }

    void busDisconnected() {
        clearTrackedNotifications();
        setAvailable(false);
        closeConnection();
        scheduleReconnect();
    }

    static DBusHandlerResult messageFilter(DBusConnection *, DBusMessage *message,
                                           void *context) {
        NotificationMonitorPrivate *self =
            static_cast<NotificationMonitorPrivate *>(context);
        if (dbus_message_is_signal(
                message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
            const quint64 generation = self->connectionGeneration;
            QTimer::singleShot(0, self->q, [self, generation]() {
                if (generation == self->connectionGeneration) {
                    self->busDisconnected();
                }
            });
        } else if (dbus_message_is_signal(
                       message, DBUS_INTERFACE_DBUS,
                       "NameOwnerChanged") &&
                   dbus_message_has_sender(message, DBUS_SERVICE_DBUS) &&
                   dbus_message_has_path(message, DBUS_PATH_DBUS)) {
            DBusError error = DBUS_ERROR_INIT;
            const char *name = NULL;
            const char *oldOwner = NULL;
            const char *newOwner = NULL;
            if (dbus_message_get_args(message, &error,
                                      DBUS_TYPE_STRING, &name,
                                      DBUS_TYPE_STRING, &oldOwner,
                                      DBUS_TYPE_STRING, &newOwner,
                                      DBUS_TYPE_INVALID) &&
                name != NULL) {
                if (strcmp(name, kNotificationsService) == 0) {
                    self->serviceOwnerChanged(oldOwner, newOwner);
                } else if (strcmp(name, kCommHistoryService) == 0) {
                    self->commHistoryOwnerChanged(oldOwner, newOwner);
                }
            }
            dbus_error_free(&error);
        } else if (dbus_message_is_method_call(
                message, kNotificationsInterface, "Notify")) {
            const char *destination = dbus_message_get_destination(message);
            const QString destinationName = QString::fromUtf8(
                destination == NULL ? "" : destination);
            if (destinationName != QString::fromLatin1(kNotificationsService) &&
                (self->serviceOwner.isEmpty() ||
                 destinationName != self->serviceOwner)) {
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            PendingNotification pending;
            if (self->parseNotify(message, &pending)) {
                pending.sender = QString::fromUtf8(
                    dbus_message_get_sender(message) == NULL ? "" :
                    dbus_message_get_sender(message));
                const QString key = pendingKey(
                    dbus_message_get_sender(message),
                    dbus_message_get_serial(message));
                self->pending.insert(key, pending);
                const quint64 generation = self->connectionGeneration;
                QTimer::singleShot(kPendingTimeoutMs, self->q,
                                   [self, key, generation]() {
                    if (generation == self->connectionGeneration) {
                        self->pending.remove(key);
                    }
                });
                while (self->pending.size() > kMaximumPending) {
                    self->pending.erase(self->pending.begin());
                }
            }
        } else if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_ERROR) {
            const QString sender = QString::fromUtf8(
                dbus_message_get_sender(message) == NULL ? "" :
                dbus_message_get_sender(message));
            if (sender == self->serviceOwner ||
                sender == QString::fromLatin1(DBUS_SERVICE_DBUS)) {
                const QString key = pendingKey(
                    dbus_message_get_destination(message),
                    dbus_message_get_reply_serial(message));
                self->pending.remove(key);
            }
        } else if (dbus_message_get_type(message) ==
                       DBUS_MESSAGE_TYPE_METHOD_RETURN &&
                   QString::fromUtf8(dbus_message_get_sender(message) == NULL ?
                       "" : dbus_message_get_sender(message)) == self->serviceOwner) {
            const QString key = pendingKey(
                dbus_message_get_destination(message),
                dbus_message_get_reply_serial(message));
            if (self->pending.contains(key)) {
                const PendingNotification pending = self->pending.take(key);
                DBusError error = DBUS_ERROR_INIT;
                dbus_uint32_t id = 0;
                if (dbus_message_get_args(message, &error,
                                          DBUS_TYPE_UINT32, &id,
                                          DBUS_TYPE_INVALID)) {
                    self->post(id, pending);
                }
                dbus_error_free(&error);
            }
        } else if (dbus_message_is_signal(
                       message, kNotificationsInterface,
                       "NotificationClosed") &&
                   QString::fromUtf8(dbus_message_get_sender(message) == NULL ?
                       "" : dbus_message_get_sender(message)) == self->serviceOwner &&
                   dbus_message_has_path(message, kNotificationsPath)) {
            DBusError error = DBUS_ERROR_INIT;
            dbus_uint32_t id = 0;
            dbus_uint32_t reason = 0;
            if (dbus_message_get_args(message, &error,
                                      DBUS_TYPE_UINT32, &id,
                                      DBUS_TYPE_UINT32, &reason,
                                      DBUS_TYPE_INVALID)) {
                self->close(id, reason);
            }
            dbus_error_free(&error);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    bool parseNotify(DBusMessage *message, PendingNotification *pending) {
        if (pending == NULL || strcmp(dbus_message_get_signature(message),
                                      "susssasa{sv}i") != 0) {
            return false;
        }
        DBusMessageIter iterator;
        if (!dbus_message_iter_init(message, &iterator) ||
            !readBasicString(&iterator, &pending->appName,
                             lp3wire::kNotificationApplicationNameMax)) {
            return false;
        }
        dbus_message_iter_next(&iterator);
        if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_UINT32) {
            return false;
        }
        dbus_message_iter_get_basic(&iterator, &pending->replacesId);
        dbus_message_iter_next(&iterator);
        if (!readBasicString(&iterator, &pending->appIcon,
                             lp3wire::kNotificationIconNameMax)) {
            return false;
        }
        dbus_message_iter_next(&iterator);
        if (!readBasicString(&iterator, &pending->summary,
                             lp3wire::kNotificationTitleMax)) {
            return false;
        }
        dbus_message_iter_next(&iterator);
        if (!readBasicString(&iterator, &pending->body,
                             lp3wire::kNotificationBodyMax)) {
            return false;
        }
        dbus_message_iter_next(&iterator);
        if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_ARRAY) {
            return false;
        }
        DBusMessageIter actionsIterator;
        dbus_message_iter_recurse(&iterator, &actionsIterator);
        int actionCount = 0;
        while (dbus_message_iter_get_arg_type(&actionsIterator) == DBUS_TYPE_STRING) {
            QString action;
            if (++actionCount > kMaximumActions ||
                !readBasicString(&actionsIterator, &action,
                                 kMaximumActionText)) {
                return false;
            }
            pending->actions.append(action);
            dbus_message_iter_next(&actionsIterator);
        }
        if ((actionCount % 2) != 0 ||
            dbus_message_iter_get_arg_type(&actionsIterator) !=
                DBUS_TYPE_INVALID) {
            return false;
        }
        dbus_message_iter_next(&iterator);
        if (dbus_message_iter_get_arg_type(&iterator) != DBUS_TYPE_ARRAY) {
            return false;
        }
        DBusMessageIter hintsIterator;
        dbus_message_iter_recurse(&iterator, &hintsIterator);
        int hintCount = 0;
        QSet<QString> recognizedHints;
        while (dbus_message_iter_get_arg_type(&hintsIterator) ==
               DBUS_TYPE_DICT_ENTRY) {
            if (++hintCount > kMaximumHints) {
                return false;
            }
            DBusMessageIter entry;
            dbus_message_iter_recurse(&hintsIterator, &entry);
            QString key;
            if (!readBasicString(&entry, &key, kMaximumHintName) ||
                !dbus_message_iter_next(&entry) ||
                dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
                return false;
            }
            size_t maximum = maximumHintString(key);
            bool dynamicActionHint = false;
            if (maximum == 0) {
                maximum = dynamicActionHintMaximum(*pending, key);
                dynamicActionHint = maximum != 0;
            }
            if (maximum != 0) {
                if (recognizedHints.contains(key)) {
                    return false;
                }
                recognizedHints.insert(key);
                const QVariant value = readVariant(&entry, maximum);
                if (value.isValid() &&
                    (!dynamicActionHint || value.type() == QVariant::String)) {
                    pending->hints.insert(key, value);
                }
            }
            dbus_message_iter_next(&hintsIterator);
        }
        return true;
    }

    void post(uint32_t id, const PendingNotification &pendingNotification) {
        const QString category = stringHint(pendingNotification.hints, "category");
        ReplyTarget target;
        const bool canReply = replyTarget(
            pendingNotification, category, commHistoryOwner, &target);
        const bool replacedActive = pendingNotification.replacesId != 0 &&
            removeActive(pendingNotification.replacesId);
        const bool suppressed = id == 0 ||
            boolHint(pendingNotification.hints, "transient") ||
            boolHint(pendingNotification.hints, "x-nemo-hidden") ||
            category.endsWith(QStringLiteral(".group")) ||
            (pendingNotification.summary.isEmpty() &&
             pendingNotification.body.isEmpty());
        if (suppressed) {
            if (replacedActive) {
                emitClosed(pendingNotification.replacesId, 0);
            }
            return;
        }

        NotificationMonitor::Notification notification;
        notification.id = QString::number(id);
        if (pendingNotification.replacesId != 0) {
            notification.replacesId = QString::number(
                pendingNotification.replacesId);
        }
        notification.timestampMs = QDateTime::currentMSecsSinceEpoch();
        const QDateTime hintedTime = QDateTime::fromString(
            stringHint(pendingNotification.hints, "x-nemo-timestamp"),
            Qt::ISODate);
        if (hintedTime.isValid()) {
            notification.timestampMs = hintedTime.toMSecsSinceEpoch();
        }
        notification.applicationId = normalizedApplicationId(pendingNotification);
        notification.applicationName = pendingNotification.appName.isEmpty() ?
            notification.applicationId : pendingNotification.appName;
        notification.title = pendingNotification.summary;
        notification.body = pendingNotification.body;
        notification.category = category;
        if (canReply) {
            notification.flags |= LP3_PLATFORM_NOTIFICATION_HAS_REPLY_ACTION;
        }
        notification.iconName = themeIconName(pendingNotification.appIcon);
        if (notification.iconName.isEmpty()) {
            notification.iconName = themeIconName(
                stringHint(pendingNotification.hints, "x-nemo-icon"));
        }
        if (notification.iconName.isEmpty()) {
            notification.iconName = themeIconName(
                stringHint(pendingNotification.hints, "x-nemo-preview-icon"));
        }

        trackActive(id, canReply ? &target : NULL);
        postedCallback(notification);
    }

    void close(uint32_t id, uint32_t reason) {
        if (removeActive(id)) {
            emitClosed(id, reason);
        }
    }

    bool removeActive(uint32_t id) {
        if (!activeIds.remove(id)) {
            replyTargets.remove(id);
            return false;
        }
        replyTargets.remove(id);
        activeOrder.removeAll(id);
        return true;
    }

    void trackActive(uint32_t id, const ReplyTarget *target) {
        removeActive(id);
        activeIds.insert(id);
        if (target != NULL) {
            replyTargets.insert(id, *target);
        }
        activeOrder.append(id);
        while (activeOrder.size() > kMaximumActive) {
            const uint32_t evicted = activeOrder.takeFirst();
            if (activeIds.remove(evicted)) {
                replyTargets.remove(evicted);
                emitClosed(evicted, 0);
            }
        }
    }

    void emitClosed(uint32_t id, uint32_t reason) {
        NotificationMonitor::Notification notification;
        notification.id = QString::number(id);
        notification.closeReason = reason <= 4 ? reason : 0;
        closedCallback(notification);
    }

    NotificationMonitor *q;
    DBusConnection *connection;
    NotificationMonitor::PostedCallback postedCallback;
    NotificationMonitor::ClosedCallback closedCallback;
    NotificationMonitor::HealthCallback healthCallback;
    NotificationMonitor::HealthCallback messagingHealthCallback;
    bool started;
    bool available;
    bool messagingAvailable;
    bool retryScheduled;
    bool shuttingDown;
    quint64 connectionGeneration;
    QString serviceOwner;
    QString commHistoryOwner;
    QMap<QString, PendingNotification> pending;
    QSet<uint32_t> activeIds;
    QHash<uint32_t, ReplyTarget> replyTargets;
    QList<uint32_t> activeOrder;
};

NotificationMonitor::Notification::Notification()
    : flags(0), timestampMs(0), closeReason(0) {}

NotificationMonitor::NotificationMonitor(const PostedCallback &posted,
                                         const ClosedCallback &closed,
                                         const HealthCallback &health,
                                         const HealthCallback &messagingHealth,
                                         QObject *parent)
    : QObject(parent),
      m_private(new NotificationMonitorPrivate(
          this, posted, closed, health, messagingHealth)) {}

NotificationMonitor::~NotificationMonitor() {
    delete m_private;
}

bool NotificationMonitor::start() {
    return m_private->start();
}

int32_t NotificationMonitor::command(uint32_t commandValue,
                                     const QString &id) {
    return m_private->command(commandValue, id);
}

int32_t NotificationMonitor::reply(const QString &id, const QString &text) {
    return m_private->reply(id, text);
}
