#include "webweatherprovider.h"

#include "pebble.h"

#include <QTimerEvent>

#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>

#include <QNetworkRequest>
#include <QNetworkReply>

WebWeatherProvider::WebWeatherProvider(Pebble *pebble, WatchConnection *connection, WeatherApp *weatherApp) :
    QObject(pebble),
    m_nam(pebble->nam()),
    m_gps(nullptr),
    m_pebble(pebble),
    m_connection(connection),
    m_weatherApp(weatherApp)
{
    connect(connection,&WatchConnection::watchConnected, this, &WebWeatherProvider::watchConnected);
}
WebWeatherProvider::~WebWeatherProvider()
{
    if(m_gps) delete m_gps;
}

void WebWeatherProvider::setApiKey(const QString &key)
{
    m_apiKey = key;
    if(!m_apiKey.isEmpty()) {
        startTimer(refreshInterval);
        timerEvent(0);
    }
}

void WebWeatherProvider::setLanguage(const QString &lang)
{
    if(!lang.isEmpty()) {
        m_language = lang;
        timerEvent(0);
    }
}

QString WebWeatherProvider::getLanguage() const
{
    return m_language;
}

QChar WebWeatherProvider::getUnits() const
{
    return m_units;
}
void WebWeatherProvider::setUnits(const QChar &u)
{
    m_units = u;
}

void WebWeatherProvider::refreshWeather()
{
    timerEvent(0);
}

void WebWeatherProvider::initGPS()
{
    if (!m_gps) {
        m_gps = QGeoPositionInfoSource::createDefaultSource(this);

        connect(m_gps, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(&QGeoPositionInfoSource::error), this, &WebWeatherProvider::gpsError);
        connect(m_gps, &QGeoPositionInfoSource::positionUpdated, this, &WebWeatherProvider::gotPosition);
        connect(m_gps, &QGeoPositionInfoSource::updateTimeout, this, &WebWeatherProvider::gpsTimeout);
    }
}

void WebWeatherProvider::watchConnected()
{
    if(m_updateMissed) {
        m_updateMissed = false;
        timerEvent(0);
    }
}

void WebWeatherProvider::timerEvent(QTimerEvent *e)
{
    if(m_apiKey.isEmpty() || m_weatherApp->locOrder().isEmpty()) {
        if(m_apiKey.isEmpty() && e)
            killTimer(e->timerId());
        qDebug() << "API Key" << m_apiKey << "or Locations" << m_weatherApp->locOrder() << "are empty, ignoring update";
        return;
    }
    if(!m_connection->isConnected()) {
        m_updateMissed = true;
        qDebug() << "Delaying update till watch is connected";
        return;
    }
    // This could be called more frequently, suppress fast updates, meteo stations are slow
    if(m_lastUpdated.isValid() && m_lastUpdated.addSecs(300) > QDateTime::currentDateTime())
        return;
    initGPS();
    QGeoPositionInfo gpi = m_gps->lastKnownPosition();
    if(gpi.isValid() && gpi.timestamp().addSecs(300)>QDateTime::currentDateTimeUtc()) {
        gotPosition(gpi);
    } else {
        m_gps->requestUpdate();
    }
}
void WebWeatherProvider::gotPosition(const QGeoPositionInfo &gpi)
{
    qDebug() << "Updating forecast for current location" << gpi.coordinate().latitude() << gpi.coordinate().longitude() << "with" << m_apiKey;
    WeatherApp::Location &loc = m_weatherApp->location(m_weatherApp->locOrder().first());
    loc.lat = QString::number(gpi.coordinate().latitude());
    loc.lng = QString::number(gpi.coordinate().longitude());
    if(!m_apiKey.isEmpty())
        updateForecast();
}

void WebWeatherProvider::gpsError(int err)
{
    qWarning() << "Error getting location data" << err;
}

void WebWeatherProvider::gpsTimeout()
{
    qWarning() << "Timeout getting location data";
}

void WebWeatherProvider::updateForecast()
{
    QString urlTemp = urlTemplate();
    if(urlTemp.isEmpty()) {
        qWarning() << "Cannot get forecasts from empty URL template" << urlTemp;
        return;
    }
    foreach(const QString &l, m_weatherApp->locOrder()) {
        WeatherApp::Location loc = m_weatherApp->getLocation(l);
        QUrl url(urlTemp.arg(loc.lat,loc.lng));
        QNetworkRequest req(url);
        QNetworkReply *rpl = m_nam->get(req);
        qDebug() << "Fetching weather data for" << l << "from" << url;
        connect(rpl,&QNetworkReply::finished, [this,l,rpl](){
            rpl->deleteLater();
            QByteArray reply = rpl->readAll();
            //qDebug() << reply;
            if(rpl->error() == QNetworkReply::NoError) {
                processLocation(l,reply);
                m_lastUpdated = QDateTime::currentDateTime();
            } else if(rpl->error() == QNetworkReply::AuthenticationRequiredError) {
                m_apiKey.clear();
                qWarning() << "Wrong Authorisation or API key" << rpl->errorString();
            } else {
                qWarning() << "Error fetching Weather data" << rpl->errorString();
            }
        });
    }
}
