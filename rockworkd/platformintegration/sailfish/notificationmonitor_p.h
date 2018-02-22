#ifndef WATCHFISH_NOTIFICATIONMONITOR_P_H
#define WATCHFISH_NOTIFICATIONMONITOR_P_H

#include <QtCore/QObject>
#include <dbus/dbus.h>

#include "notificationmonitor.h"

#define CATEGORY_DEFINITION_FILE_DIRECTORY "/usr/share/lipstick/notificationcategories"
#define CATEGORY_REFRESH_CHECK_TIME 120

namespace watchfish
{

struct CategoryCacheEntry
{
    QHash<QString, QString> data;
    QDateTime lastReadTime;
    QDateTime lastCheckTime;
};

struct ProtoNotification
{
    QString sender;
    QString appIcon;
    QString summary;
    QString body;
    QHash<QString, QString> hints;
    int expireTimeout;
    quint32 replacesId;
    QStringList actions;
};

class NotificationMonitorPrivate : public QObject
{
    Q_OBJECT

public:
    NotificationMonitorPrivate(NotificationMonitor *q);
    ~NotificationMonitorPrivate();

private:
    /** Converts a ProtoNotification into a Notification object and raises it. */
    void processIncomingNotification(quint32 id, const ProtoNotification &proto);
    /** Raises appropiate notification signal. */
    void processCloseNotification(quint32 id, quint32 reason);

    /** Sends a D-Bus message for a method call with a single string argument. */
    void sendMessageWithString(const char *service, const char *path, const char *iface, const char *method, const char *arg);
    /** Adds a D-Bus filter match rule. */
    void addMatchRule(const char *rule);
    void removeMatchRule(const char *rule);

    /** Converts a fdo Notification D-Bus message into a ProtoNotification object. */
    ProtoNotification parseNotifyCall(DBusMessage *msg) const;

    QHash<QString,QString> getCategoryInfo(const QString &s) const;

    QString getAppName(const QString &id) const;

    static dbus_bool_t busWatchAdd(DBusWatch *watch, void *data);
    static void busWatchRemove(DBusWatch *watch, void *data);
    static void busWatchToggle(DBusWatch *watch, void *data);

    static DBusHandlerResult busMessageFilter(DBusConnection *conn, DBusMessage *msg, void *user_data);

private slots:
    void handleBusSocketActivated();

private:
    NotificationMonitor * const q_ptr;
    Q_DECLARE_PUBLIC(NotificationMonitor)

    /** The current set of monitored notifications, indexed by id. */
    QMap<quint32, Notification*> _notifs;
    /** Low level dbus connection used for sniffing. */
    DBusConnection *_conn;
    /** Serials of DBUS method calls of which we are expecting a reply. */
    QHash<quint32, ProtoNotification> _pendingConfirmation;
    /** Cache of notification category info. */
    mutable QHash<QString, CategoryCacheEntry> _categoryCache;
};


}

#endif // WATCHFISH_NOTIFICATIONMONITOR_P_H

