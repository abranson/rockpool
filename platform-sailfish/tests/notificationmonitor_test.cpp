/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include <QDataStream>
#include <QVariant>

#include "../helper/notificationmonitor.cpp"

namespace {

const char kTrustedOwner[] = ":1.42";
const char kReplyAction[] = "action_123_1";

QString encodedVariant(const QVariant &value) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << value;
    assert(stream.status() == QDataStream::Ok);
    return QString::fromLatin1(bytes.toBase64());
}

QString remoteAction(const QVariant &account, const QVariant &recipient) {
    return QStringLiteral(
        "org.sailfishos.Messages / org.sailfishos.Messages sendMessage %1 %2")
        .arg(encodedVariant(account), encodedVariant(recipient));
}

PendingNotification validPending() {
    PendingNotification pending;
    pending.sender = QString::fromLatin1(kTrustedOwner);
    pending.actions
        << QStringLiteral("default") << QStringLiteral("Open")
        << QString::fromLatin1(kReplyAction) << QStringLiteral("Reply");
    pending.hints.insert(
        QStringLiteral("x-nemo-remote-action-") +
            QString::fromLatin1(kReplyAction),
        remoteAction(
            QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/account0"),
            QStringLiteral("+358401234567")));
    pending.hints.insert(
        QStringLiteral("x-nemo-remote-action-type-") +
            QString::fromLatin1(kReplyAction),
        QStringLiteral("input"));
    return pending;
}

void testExactReplyCapability() {
    const PendingNotification pending = validPending();
    ReplyTarget target;

    assert(containsActionKey(pending.actions, QStringLiteral("default")));
    assert(containsActionKey(pending.actions,
                             QString::fromLatin1(kReplyAction)));
    assert(!containsActionKey(pending.actions, QStringLiteral("missing")));
    assert(dynamicActionHintMaximum(
        pending, QStringLiteral("x-nemo-remote-action-action_123_1")) ==
        kMaximumRemoteAction);
    assert(dynamicActionHintMaximum(
        pending, QStringLiteral("x-nemo-remote-action-type-action_123_1")) ==
        16);
    assert(dynamicActionHintMaximum(
        pending, QStringLiteral("x-nemo-remote-action-action_999_1")) == 0);
    assert(dynamicActionHintMaximum(
        pending, QStringLiteral("x-nemo-remote-action-default")) == 0);

    assert(replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                       QString::fromLatin1(kTrustedOwner), &target));
    assert(target.sourceOwner == QString::fromLatin1(kTrustedOwner));
    assert(target.accountPath == QStringLiteral(
        "/org/freedesktop/Telepathy/Account/ring/tel/account0"));
    assert(target.recipient == QStringLiteral("+358401234567"));
    assert(replyTarget(pending, QStringLiteral("x-nemo.messaging.im"),
                       QString::fromLatin1(kTrustedOwner), &target));
    assert(replyTarget(pending, QStringLiteral("x-nemo.messaging.mms"),
                       QString::fromLatin1(kTrustedOwner), &target));

    DBusMessage *message = createReplyMessage(target, QByteArray("Hello"));
    assert(message != NULL);
    assert(strcmp(dbus_message_get_destination(message), kMessagesService) == 0);
    assert(strcmp(dbus_message_get_path(message), kMessagesPath) == 0);
    assert(strcmp(dbus_message_get_interface(message), kMessagesInterface) == 0);
    assert(strcmp(dbus_message_get_member(message), kMessagesMethod) == 0);
    assert(strcmp(dbus_message_get_signature(message), "sss") == 0);
    DBusError error = DBUS_ERROR_INIT;
    const char *account = NULL;
    const char *recipient = NULL;
    const char *text = NULL;
    assert(dbus_message_get_args(
        message, &error,
        DBUS_TYPE_STRING, &account,
        DBUS_TYPE_STRING, &recipient,
        DBUS_TYPE_STRING, &text,
        DBUS_TYPE_INVALID));
    assert(target.accountPath == QString::fromUtf8(account));
    assert(target.recipient == QString::fromUtf8(recipient));
    assert(strcmp(text, "Hello") == 0);
    dbus_error_free(&error);
    dbus_message_unref(message);
}

void testExactOpenMessage() {
    ReplyTarget target;
    assert(replyTarget(validPending(), QStringLiteral("x-nemo.messaging.sms"),
                       QString::fromLatin1(kTrustedOwner), &target));

    DBusMessage *message = createOpenMessage(target);
    assert(message != NULL);
    assert(strcmp(dbus_message_get_destination(message), kMessagesService) == 0);
    assert(strcmp(dbus_message_get_path(message), kMessagesPath) == 0);
    assert(strcmp(dbus_message_get_interface(message), kMessagesInterface) == 0);
    assert(strcmp(dbus_message_get_member(message), "startConversation") == 0);
    assert(strcmp(dbus_message_get_signature(message), "ss") == 0);
    DBusError error = DBUS_ERROR_INIT;
    const char *account = NULL;
    const char *recipient = NULL;
    assert(dbus_message_get_args(
        message, &error,
        DBUS_TYPE_STRING, &account,
        DBUS_TYPE_STRING, &recipient,
        DBUS_TYPE_INVALID));
    assert(target.accountPath == QString::fromUtf8(account));
    assert(target.recipient == QString::fromUtf8(recipient));
    dbus_error_free(&error);
    dbus_message_unref(message);
}

void testRejectsForgedOrReplacedOwner() {
    ReplyTarget target;
    PendingNotification pending = validPending();
    pending.sender = QStringLiteral(":1.99");
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QStringLiteral(":1.43"), &target));
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString(), &target));
}

void testRejectsInvalidCapability() {
    ReplyTarget target;
    PendingNotification pending = validPending();
    assert(!replyTarget(pending, QStringLiteral("email"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    pending.actions.removeAt(2);
    pending.actions.removeAt(2);
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    const QString remoteName = QStringLiteral("x-nemo-remote-action-") +
        QString::fromLatin1(kReplyAction);
    const QString typeName = QStringLiteral("x-nemo-remote-action-type-") +
        QString::fromLatin1(kReplyAction);
    pending = validPending();
    pending.hints.insert(typeName, QStringLiteral("button"));
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    QString route = pending.hints.value(remoteName).toString();
    route.replace(QStringLiteral("sendMessage"), QStringLiteral("open"));
    pending.hints.insert(remoteName, route);
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    pending.hints.insert(
        remoteName,
        remoteAction(QStringLiteral("/tmp/not-an-account"),
                     QStringLiteral("+358401234567")));
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    pending.hints.insert(
        remoteName,
        remoteAction(
            QStringLiteral("/org/freedesktop/Telepathy/Account/ring/tel/a"),
            QVariant::fromValue(42)));
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));

    pending = validPending();
    pending.actions
        << QStringLiteral("action_123_2") << QStringLiteral("Reply again");
    pending.hints.insert(
        QStringLiteral("x-nemo-remote-action-action_123_2"),
        pending.hints.value(remoteName));
    pending.hints.insert(
        QStringLiteral("x-nemo-remote-action-type-action_123_2"),
        QStringLiteral("input"));
    assert(!replyTarget(pending, QStringLiteral("x-nemo.messaging.sms"),
                        QString::fromLatin1(kTrustedOwner), &target));
}

void testRejectsNonCanonicalSerializedArguments() {
    QString value;
    assert(!decodeCanonicalStringVariant(QStringLiteral("AAAA===="), &value));
    assert(!decodeCanonicalStringVariant(QStringLiteral("AAAA\n"), &value));
    assert(!decodeCanonicalStringVariant(
        QString(kMaximumEncodedArgument + 1, QLatin1Char('A')), &value));
    const QByteArray hostileLength =
        QByteArray::fromHex("0000000a00ffffffff");
    assert(!decodeCanonicalStringVariant(
        QString::fromLatin1(hostileLength.toBase64()), &value));
    assert(!decodeCanonicalStringVariant(encodedVariant(42), &value));
    QByteArray objectPathBytes;
    QDataStream objectPathStream(&objectPathBytes, QIODevice::WriteOnly);
    objectPathStream << static_cast<quint32>(QMetaType::User)
                     << static_cast<quint8>(0)
                     << QByteArray("QDBusObjectPath")
                     << QStringLiteral("/org/example/Account/a");
    assert(!decodeCanonicalStringVariant(
        QString::fromLatin1(objectPathBytes.toBase64()), &value));
}

void testConversationTargetAuthorityAndRetirement() {
    QList<NotificationMonitor::Notification> posted;
    QList<NotificationMonitor::Notification> closed;
    QList<bool> notificationHealth;
    QList<bool> messagingHealth;
    NotificationMonitorPrivate monitor(
        NULL,
        [&posted](const NotificationMonitor::Notification &notification) {
            posted.append(notification);
        },
        [&closed](const NotificationMonitor::Notification &notification) {
            closed.append(notification);
        },
        [&notificationHealth](bool available) {
            notificationHealth.append(available);
        },
        [&messagingHealth](bool available) {
            messagingHealth.append(available);
        });
    monitor.setAvailable(true);
    monitor.setCommHistoryOwner(QString::fromLatin1(kTrustedOwner));

    PendingNotification pending = validPending();
    pending.summary = QStringLiteral("Message");
    pending.hints.insert(QStringLiteral("category"),
                         QStringLiteral("x-nemo.messaging.sms"));
    monitor.post(42, pending);
    assert(posted.size() == 1);
    assert(monitor.activeIds.contains(42));
    assert(monitor.replyTargets.contains(42));
    assert(monitor.conversationTargets.contains(42));
    assert(monitor.replyTargets.value(42).sourceOwner ==
           QString::fromLatin1(kTrustedOwner));
    assert(monitor.conversationTargets.value(42).sourceOwner ==
           QString::fromLatin1(kTrustedOwner));
    assert((posted.first().flags &
            LP3_PLATFORM_NOTIFICATION_HAS_DEFAULT_ACTION) != 0);

    // Consuming reply authority must leave the separately retained default
    // conversation authority intact.
    monitor.replyTargets.remove(42);
    assert(!monitor.replyTargets.contains(42));
    assert(monitor.conversationTargets.contains(42));

    monitor.setCommHistoryOwner(QStringLiteral(":1.43"));
    assert(monitor.activeIds.contains(42));
    assert(!monitor.replyTargets.contains(42));
    assert(!monitor.conversationTargets.contains(42));
    assert(messagingHealth.size() == 3);
    assert(messagingHealth.at(0));
    assert(!messagingHealth.at(1));
    assert(messagingHealth.at(2));

    pending = validPending();
    pending.sender = QStringLiteral(":1.43");
    pending.actions.removeFirst();
    pending.actions.removeFirst();
    pending.summary = QStringLiteral("Reply-only message");
    pending.hints.insert(QStringLiteral("category"),
                         QStringLiteral("x-nemo.messaging.sms"));
    monitor.post(43, pending);
    assert(monitor.activeIds.contains(43));
    assert(monitor.replyTargets.contains(43));
    assert(!monitor.conversationTargets.contains(43));
    assert((posted.last().flags &
            LP3_PLATFORM_NOTIFICATION_HAS_REPLY_ACTION) != 0);
    assert((posted.last().flags &
            LP3_PLATFORM_NOTIFICATION_HAS_DEFAULT_ACTION) == 0);

    pending = validPending();
    pending.sender = QStringLiteral(":1.43");
    pending.summary = QStringLiteral("Replacement source");
    pending.hints.insert(QStringLiteral("category"),
                         QStringLiteral("x-nemo.messaging.sms"));
    monitor.post(44, pending);
    assert(monitor.replyTargets.contains(44));
    assert(monitor.conversationTargets.contains(44));

    pending.replacesId = 44;
    pending.summary = QStringLiteral("Replacement");
    monitor.post(45, pending);
    assert(!monitor.replyTargets.contains(44));
    assert(!monitor.conversationTargets.contains(44));
    assert(monitor.replyTargets.contains(45));
    assert(monitor.conversationTargets.contains(45));

    monitor.close(45, 2);
    assert(!monitor.activeIds.contains(45));
    assert(!monitor.replyTargets.contains(45));
    assert(!monitor.conversationTargets.contains(45));

    monitor.clearTrackedNotifications();
    pending.replacesId = 0;
    for (uint32_t index = 0;
         index <= static_cast<uint32_t>(kMaximumActive); ++index) {
        monitor.post(100 + index, pending);
    }
    assert(!monitor.activeIds.contains(100));
    assert(!monitor.replyTargets.contains(100));
    assert(!monitor.conversationTargets.contains(100));
    assert(monitor.replyTargets.contains(100 + kMaximumActive));
    assert(monitor.conversationTargets.contains(100 + kMaximumActive));
    assert(!closed.isEmpty());
    assert(notificationHealth.size() == 1 && notificationHealth.first());
}

} // namespace

int main() {
    testExactReplyCapability();
    testExactOpenMessage();
    testRejectsForgedOrReplacedOwner();
    testRejectsInvalidCapability();
    testRejectsNonCanonicalSerializedArguments();
    testConversationTargetAuthorityAndRetirement();
    return 0;
}
