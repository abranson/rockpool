/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include <QDataStream>
#include <QVariant>
#include <QTemporaryDir>

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

DBusMessage *androidNotification(const QByteArray &action, const QString &imagePath) {
    DBusMessage *message = dbus_message_new_method_call(
        kNotificationsService, kNotificationsPath, kNotificationsInterface, "Notify");
    assert(message != NULL);
    const char *app = "WhatsApp", *icon = "", *title = "Image test", *body = "Photo";
    dbus_uint32_t replaces = 0;
    assert(dbus_message_append_args(message,
        DBUS_TYPE_STRING, &app, DBUS_TYPE_UINT32, &replaces,
        DBUS_TYPE_STRING, &icon, DBUS_TYPE_STRING, &title,
        DBUS_TYPE_STRING, &body, DBUS_TYPE_INVALID));
    DBusMessageIter args, actions, hints;
    dbus_message_iter_init_append(message, &args);
    assert(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions));
    const char *actionKey = action.constData(), *label = "Reply";
    assert(dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &actionKey));
    assert(dbus_message_iter_append_basic(&actions, DBUS_TYPE_STRING, &label));
    assert(dbus_message_iter_close_container(&args, &actions));
    assert(dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints));
    QMap<QByteArray, QByteArray> values;
    values.insert("category", "chat");
    values.insert("x-nemo-origin-package", "com.whatsapp");
    values.insert("x-nemo-image-preview-path", imagePath.toUtf8());
    values.insert("x-nemo-remote-action-" + action, "org.example.Test / org.example.Test Reply");
    values.insert("x-nemo-remote-action-type-" + action, "input");
    values.insert("x-nemo-remote-action-input-" + action, "reply");
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        DBusMessageIter entry, variant;
        const char *key = it.key().constData(), *value = it.value().constData();
        assert(dbus_message_iter_open_container(&hints, DBUS_TYPE_DICT_ENTRY, NULL, &entry));
        assert(dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key));
        assert(dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant));
        assert(dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value));
        assert(dbus_message_iter_close_container(&entry, &variant));
        assert(dbus_message_iter_close_container(&hints, &entry));
    }
    assert(dbus_message_iter_close_container(&args, &hints));
    dbus_int32_t timeout = -1;
    assert(dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &timeout));
    return message;
}

void testAndroidActionHintsWithImage() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/photo.png");
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::blue);
    assert(image.save(path));
    QList<NotificationMonitor::Notification> posted;
    NotificationMonitorPrivate monitor(NULL,
        [&posted](const NotificationMonitor::Notification &notification) {
            posted.append(notification);
        }, [](const NotificationMonitor::Notification &) {}, [](bool) {}, [](bool) {});
    // Android embeds its intent name in each action hint, exceeding 64 bytes.
    const QByteArray whatsappAction("Reply|com.whatsapp.intent.action.DIRECT_REPLY_FROM_MESSAGE");
    const QList<QByteArray> actions = {whatsappAction, QByteArray(kMaximumActionText, 'a')};
    for (const QByteArray &action : actions) {
        DBusMessage *message = androidNotification(action, path);
        PendingNotification pending;
        assert(monitor.parseNotify(message, &pending));
        monitor.post(1, pending);
        assert(posted.last().applicationId == QStringLiteral("com.whatsapp"));
        assert(posted.last().body == QStringLiteral("Photo"));
        assert(!posted.last().image.isEmpty());
        assert(!(posted.last().flags & LP3_PLATFORM_NOTIFICATION_HAS_REPLY_ACTION));
        dbus_message_unref(message);
    }
    DBusMessage *oversized = androidNotification(QByteArray(kMaximumActionText + 1, 'a'), path);
    PendingNotification pending;
    assert(!monitor.parseNotify(oversized, &pending));
    dbus_message_unref(oversized);
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

void testSendTextUsesTelepathyDispatcher() {
    const QString account = QStringLiteral(
        "/org/freedesktop/Telepathy/Account/ring/tel/ril_0");
    const QDBusMessage message = createSendMessage(
        account, QStringLiteral("123"), QStringLiteral("Hello"));
    assert(message.service() == QStringLiteral("org.freedesktop.Telepathy.ChannelDispatcher"));
    assert(message.path() == QStringLiteral("/org/freedesktop/Telepathy/ChannelDispatcher"));
    assert(message.interface() == QStringLiteral(
        "org.freedesktop.Telepathy.ChannelDispatcher.Interface.Messages.DRAFT"));
    assert(message.member() == QStringLiteral("SendMessage"));
    const QList<QVariant> args = message.arguments();
    assert(args.size() == 4);
    assert(args.at(0).value<QDBusObjectPath>().path() == account);
    assert(args.at(1).toString() == QStringLiteral("123"));
    const QList<QVariantMap> parts = args.at(2).value<QList<QVariantMap> >();
    assert(parts.size() == 2);
    assert(parts.at(0).value(QStringLiteral("message-type")).type() == QVariant::UInt);
    assert(parts.at(0).value(QStringLiteral("message-type")).toUInt() == 0);
    assert(parts.at(1).value(QStringLiteral("content-type")) == QStringLiteral("text/plain"));
    assert(parts.at(1).value(QStringLiteral("content")) == QStringLiteral("Hello"));
    assert(args.at(3).type() == QVariant::UInt && args.at(3).toUInt() == 0);

    assert(messageSendSucceeded(message.createReply(QVariantList() << QStringLiteral("token"))));
    assert(!messageSendSucceeded(message.createReply(QVariantList() << QString())));
    assert(!messageSendSucceeded(message.createReply()));
    assert(!messageSendSucceeded(message.createReply(QVariantList() << 1)));
    assert(!messageSendSucceeded(message.createErrorReply(
        QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"), QStringLiteral("Denied"))));
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

void testPreviewImageHints() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/preview.png");
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::blue);
    assert(image.save(path));
    QList<NotificationMonitor::Notification> posted;
    NotificationMonitorPrivate monitor(NULL,
        [&posted](const NotificationMonitor::Notification &notification) {
            posted.append(notification);
        }, [](const NotificationMonitor::Notification &) {}, [](bool) {}, [](bool) {});
    PendingNotification pending;
    pending.summary = QStringLiteral("Photo");
    pending.hints.insert(QStringLiteral("image-path"), path);
    pending.hints.insert(QStringLiteral("image_path"), path);
    monitor.post(1, pending);
    assert(posted.last().image.isEmpty());
    pending.hints.insert(QStringLiteral("x-nemo-image-preview-path"),
                         QUrl::fromLocalFile(path).toString());
    monitor.post(2, pending);
    assert(!posted.last().image.isEmpty());
}

void testNotificationImages() {
    QImage original(256, 128, QImage::Format_ARGB32);
    original.fill(qRgba(255, 0, 0, 255));
    const QByteArray thumbnail = notificationThumbnail(original);
    assert(thumbnail.size() == 4 + 128 * 64 * 3);
    assert(static_cast<unsigned char>(thumbnail[0]) == 128);
    assert(static_cast<unsigned char>(thumbnail[2]) == 64);
    assert(static_cast<unsigned char>(thumbnail[4]) == 255);
    assert(thumbnail[5] == 0 && thumbnail[6] == 0);
    original.fill(qRgba(0, 0, 0, 0));
    const QByteArray transparent = notificationThumbnail(original);
    assert(static_cast<unsigned char>(transparent[4]) == 255);
    assert(static_cast<unsigned char>(transparent[5]) == 255);
    assert(notificationThumbnail(QImage()).isEmpty());
    assert(notificationImageFile(QStringLiteral("https://example.com/image.png")).isEmpty());
    assert(notificationImageFile(QStringLiteral("relative.png")).isEmpty());

    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/image.png");
    assert(original.save(path, "PNG"));
    assert(notificationImageFile(path) == transparent);
    assert(notificationImageFile(QUrl::fromLocalFile(path).toString()) == transparent);
    const QString link = directory.path() + QStringLiteral("/link.png");
    assert(QFile::link(path, link));
    assert(notificationImageFile(link).isEmpty());
    QFile corrupt(path);
    assert(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("not an image");
    corrupt.close();
    assert(notificationImageFile(path).isEmpty());

    DBusMessage *message = dbus_message_new_signal("/test", "org.example.Test", "Image");
    DBusMessageIter args, variant, structure, array;
    dbus_message_iter_init_append(message, &args);
    assert(dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "(iiibiiay)", &variant));
    assert(dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &structure));
    int width = 1, height = 1, stride = 4, bits = 8, channels = 4;
    dbus_bool_t alpha = true;
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &width);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &height);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &stride);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_BOOLEAN, &alpha);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &bits);
    dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &channels);
    assert(dbus_message_iter_open_container(&structure, DBUS_TYPE_ARRAY, "y", &array));
    const unsigned char red[] = {255, 0, 0, 255};
    const unsigned char *pixels = red;
    dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &pixels, 4);
    dbus_message_iter_close_container(&structure, &array);
    dbus_message_iter_close_container(&variant, &structure);
    dbus_message_iter_close_container(&args, &variant);
    dbus_message_iter_init(message, &args);
    const QByteArray raw = notificationImageData(&args);
    // A tiny source is scaled consistently with other image sources.
    assert(!raw.isEmpty());
    assert(static_cast<unsigned char>(raw[4]) == 255 && raw[5] == 0);
    dbus_message_unref(message);
}

int main() {
    testNotificationImages();
    testPreviewImageHints();
    testAndroidActionHintsWithImage();
    testExactReplyCapability();
    testExactOpenMessage();
    testSendTextUsesTelepathyDispatcher();
    testRejectsForgedOrReplacedOwner();
    testRejectsInvalidCapability();
    testRejectsNonCanonicalSerializedArguments();
    testConversationTargetAuthorityAndRetirement();
    return 0;
}
