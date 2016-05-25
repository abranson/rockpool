#include "core.h"

#include "pebblemanager.h"
#include "dbusinterface.h"

#include "platformintegration/sailfish/sailfishplatform.h"
#ifdef ENABLE_TESTING
#include "platformintegration/testing/testingplatform.h"
#endif

#include <QDebug>

Core* Core::s_instance = nullptr;

Core *Core::instance()
{
    if (!s_instance) {
        s_instance = new Core();
    }
    return s_instance;
}

PebbleManager *Core::pebbleManager()
{
    return m_pebbleManager;
}

PlatformInterface *Core::platform()
{
    return m_platform;
}

Core::Core(QObject *parent):
    QObject(parent)
{
}

void Core::init()
{
    // Platform integration
#ifdef ENABLE_TESTING
    m_platform = new TestingPlatform(this);
#else
    m_platform = new SailfishPlatform(this);
#endif

    m_pebbleManager = new PebbleManager(this);

    m_dbusInterface = new DBusInterface(this);
}

const QString PlatformInterface::SysID("ed429c16-f674-4220-95da-454f303f15e2");
const QUuid PlatformInterface::UUID(PlatformInterface::SysID);

// App Specific Resources for Pins. appType: {icon, color, [sender, [...]]}
const QHash<QString,QStringList> PlatformInterface::AppResMap = {
    {"generic",{"system://images/NOTIFICATION_GENERIC","red"}},
    {"email",{"system://images/GENERIC_EMAIL","gray","E-Mails"}},
    {"gmail",{"system://images/NOTIFICATION_GMAIL","red","GMail"}},
    {"sms",{"system://images/GENERIC_SMS","lightblue","SMS"}},
    {"hangouts",{"system://images/NOTIFICATION_GOOGLE_HANGOUTS","green","Hangouts"}},
    {"facebook",{"system://images/NOTIFICATION_FACEBOOK","blue","Facebook"}},
    {"twitter",{"system://images/NOTIFICATION_TWITTER","blue","Twitter"}},
    {"telegram",{"system://images/NOTIFICATION_TELEGRAM","cyan","Telegram"}},
    {"watsapp",{"system://images/NOTIFICATION_WHATSAPP","green","WhatsApp"}},
    {"weather",{"system://images/TIMELINE_WEATHER","indigo","Weather"}},
    {"reminder",{"system://images/NOTIFICATION_REMINDER","red","Reminders"}},
    {"calls",{"system://images/TIMELINE_MISSED_CALL","red","Calls"}},
    {"music",{"system://images/MUSIC_EVENT","red"}},
    {"alarm",{"system://images/ALARM_CLOCK","red"}},
    {"unknown",{"system://images/NOTIFICATION_FLAG","red"}}
};
