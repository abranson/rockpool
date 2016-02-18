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

