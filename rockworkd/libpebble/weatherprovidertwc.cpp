#include "weatherprovidertwc.h"

#include "pebble.h"

#include <QTimerEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>


const QString WeatherProviderTWC::urlTWC = "https://api.weather.com/v2/geocode/%1/aggregate.json?products=conditions,fcstdaily7&language=%2&units=%3&apiKey=%4";
const QHash<QChar,QString> WeatherProviderTWC::code2units = {
    {'a',""},//all
    {'m',"metric"},
    {'e',"imperial"},
    {'h',"uk_hybrid"},
    {'s',"metric_si"},
};
// Condition Codes suitable from Yahoo API https://developer.yahoo.com/weather/documentation.html
const QHash<int,QString> WeatherProviderTWC::code2icon = {
    // {4, "system://images/"}, - rain possible, t-storm
    {11,"system://images/HEAVY_RAIN"},
    {12,"system://images/HEAVY_RAIN"},
    {13,"system://images/LIGHT_SNOW"}, // Snow flurries
    {14,"system://images/LIGHT_SNOW"}, // Light Snow
    {15,"system://images/HEAVY_SNOW"}, // Blowing Snow
    {16,"system://images/HEAVY_SNOW"}, // Snow
    {17,"system://images/HEAVY_RAIN"}, // Hail
    {18,"system://images/RAINING_AND_SNOWING"}, // Sleet
    //{}, // Dust, fog, haze, etc.
    {26,"system://images/CLOUDY_DAY"}, // Clouds
    {27,"system://images/CLOUDY_DAY"}, // M Cloudy N
    {28,"system://images/CLOUDY_DAY"}, // M Cloudy D
    {29,"system://images/PARTLY_CLOUDY"},
    {30,"system://images/PARTLY_CLOUDY"},
    {31,"system://images/TIMELINE_SUN"}, // Clear Night
    {32,"system://images/TIMELINE_SUN"}, // Sunny Day
    {33,"system://images/TIMELINE_SUN"}, // Mostly Clear N
    {34,"system://images/TIMELINE_SUN"}, // Mostly Clear D
    {35,"system://images/HEAVY_RAIN"}, // Rain and Hail
    {36,"system://images/TIMELINE_SUN"}, // Hot
    //{37} // Isolated Thunderstorms
    {39,"system://images/LIGHT_RAIN"}, // Scattered T-Storms
    {40,"system://images/LIGHT_RAIN"}, // Scattered showers
    {45,"system://images/HEAVY_RAIN"}, // Thundershowers
    {46,"system://images/HEAVY_SNOW"}, // Snow Showers
    {47,"system://images/HEAVY_RAIN"} // Isolated Thundershowers
};
const QHash<int,int> code2code = {
    {11,4},
    {12,4},
    {13,2},
    {14,2},
    {15,5},
    {16,5},
    {17,4},
    {18,8},
    {26,1},
    {27,1},
    {28,1},
    {29,0},
    {30,0},
    {31,7},
    {32,7},
    {33,7},
    {34,7},
    {35,4},
    {36,7},
    {37,6},
    {39,6},
    {40,6},
    {45,4},
    {46,5},
    {47,4}
};

WeatherProviderTWC::WeatherProviderTWC(Pebble *pebble, WeatherApp *weatherApp) :
    QObject(pebble),
    m_nam(pebble->nam()),
    m_gps(nullptr),
    m_pebble(pebble),
    m_weatherApp(weatherApp)
{
    weatherApp->setProvider(this);
}

void WeatherProviderTWC::setApiKey(const QString &key)
{
    m_apiKey = key;
    if(!m_apiKey.isEmpty()) {
        startTimer(refreshInterval);
        timerEvent(0);
    }
}

void WeatherProviderTWC::setLanguage(const QString &lang)
{
    m_language = lang;
    timerEvent(0);
}

QChar WeatherProviderTWC::getUnits() const
{
    return m_units;
}
void WeatherProviderTWC::setUnits(const QChar &u)
{
    m_units = u;
}

void WeatherProviderTWC::refreshWeather()
{
    timerEvent(0);
}

void WeatherProviderTWC::initGPS()
{
    if (!m_gps) {
        m_gps = QGeoPositionInfoSource::createDefaultSource(this);

        connect(m_gps, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(&QGeoPositionInfoSource::error), this, &WeatherProviderTWC::gpsError);
        connect(m_gps, &QGeoPositionInfoSource::positionUpdated, this, &WeatherProviderTWC::gotPosition);
        connect(m_gps, &QGeoPositionInfoSource::updateTimeout, this, &WeatherProviderTWC::gpsTimeout);
    }
}

void WeatherProviderTWC::timerEvent(QTimerEvent *e)
{
    if(m_apiKey.isEmpty() || m_weatherApp->locOrder().isEmpty()) {
        if(m_apiKey.isEmpty() && e)
            killTimer(e->timerId());
        qDebug() << "API Key" << m_apiKey << "or Locations" << m_weatherApp->locOrder() << "are empty, ignoring update";
        return;
    }
    initGPS();
    QGeoPositionInfo gpi = m_gps->lastKnownPosition();
    if(gpi.isValid() && gpi.timestamp().addSecs(60*61)>QDateTime::currentDateTimeUtc()) {
        gotPosition(gpi);
    } else {
        m_gps->requestUpdate();
    }
}
void WeatherProviderTWC::gotPosition(const QGeoPositionInfo &gpi)
{
    qDebug() << "Updating forecast for current location" << gpi.coordinate().latitude() << gpi.coordinate().longitude() << "with" << m_apiKey;
    WeatherApp::Location &loc = m_weatherApp->location(m_weatherApp->locOrder().first());
    loc.lat = QString::number(gpi.coordinate().latitude());
    loc.lng = QString::number(gpi.coordinate().longitude());
    if(!m_apiKey.isEmpty())
        updateForecast();
}

void WeatherProviderTWC::gpsError(int err)
{
    qWarning() << "Error getting location data" << err;
}

void WeatherProviderTWC::gpsTimeout()
{
    qWarning() << "Timeout getting location data";
}

WeatherApp::Observation WeatherProviderTWC::parseObs(const QJsonObject &obj, const QJsonArray &days)
{
    QString text = obj.value("phrase_12char").toString();
    if(text.isEmpty())
        text = obj.value("phrase_22char").toString();
    if(text.isEmpty())
        text = obj.value("phrase_32char").toString();
    return WeatherApp::Observation(obj.value(code2units.value(m_units)).toObject().value("temp").toInt(SHRT_MAX),text,
    {code2code.value(obj.value("icon_code").toInt()),
     days.at(0).toObject().value("max_temp").toInt(SHRT_MAX),days.at(0).toObject().value("min_temp").toInt(SHRT_MAX)},
    {code2code.value(days.at(1).toObject().contains("day")?days.at(1).toObject().value("day").toObject().value("icon_code").toInt():days.at(1).toObject().value("night").toObject().value("icon_code").toInt()),
     days.at(1).toObject().value("max_temp").toInt(SHRT_MAX),days.at(1).toObject().value("min_temp").toInt(SHRT_MAX)});
}

WeatherApp::Forecast::Data WeatherProviderTWC::parseDay(const QJsonObject &day)
{
    WeatherApp::Forecast::Data data;
    data.min_temp = day.value("min_temp").toInt(SHRT_MAX);
    data.max_temp = day.value("max_temp").toInt(SHRT_MAX);
    if(day.contains("day"))
        data.day_text = day.value("day").toObject().value("narrative").toString();
    if(day.contains("night"))
        data.night_text = day.value("night").toObject().value("narrative").toString();
    return data;
}
void WeatherProviderTWC::parseLoc(WeatherApp::Forecast &loc, const QJsonObject &day)
{
    QDateTime sunset = day.value("sunset").toVariant().toDateTime(),
             sunrise = day.value("sunrise").toVariant().toDateTime();
    QString day_icon, night_icon;
    if(day.contains("day"))
        day_icon = code2icon.value(day.value("day").toObject().value("icon_code").toInt(),"system://images/TIMELINE_WEATHER");
    if(day.contains("night"))
        night_icon = code2icon.value(day.value("night").toObject().value("icon_code").toInt(),"system://images/TIMELINE_WEATHER");
    loc.initLoc(parseDay(day),sunrise,day_icon,sunset,night_icon);
}

void WeatherProviderTWC::updateForecast()
{
    foreach(const QString &l, m_weatherApp->locOrder()) {
        WeatherApp::Location loc = m_weatherApp->getLocation(l);
        if(code2units.value(m_units).isEmpty()) {
            qWarning() << "Cannot translate units" << m_units << "into container";
            return;
        }
        QUrl twc(urlTWC.arg(loc.lat+"/"+loc.lng).arg(m_language,m_units).arg(m_apiKey));
        QNetworkRequest req(twc);
        QNetworkReply *rpl = m_nam->get(req);
        qDebug() << "Fetching weather data for" << l << "from" << twc;
        connect(rpl,&QNetworkReply::finished, [this,l,rpl](){
            rpl->deleteLater();
            QByteArray reply = rpl->readAll();
            //qDebug() << reply;
            if(rpl->error() == QNetworkReply::NoError) {
                QJsonParseError jpe;
                QJsonDocument json = QJsonDocument::fromJson(reply,&jpe);
                if(jpe.error == QJsonParseError::NoError && json.isObject()) {
                    QJsonObject obj = json.object();
                    if(obj.contains("conditions") && !obj.value("conditions").toObject().value("errors").toBool() \
                            && obj.contains("fcstdaily7") && !obj.value("fcstdaily7").toObject().value("errors").toBool())
                    {
                        QJsonArray days = obj.value("fcstdaily7").toObject().value("data").toObject().value("forecasts").toArray();
                        qDebug() << "Updating current conditions for" << l;
                        // Update current conditions for all locations
                        m_weatherApp->injectObservation(l,parseObs(obj.value("conditions").toObject().value("data").toObject().value("observation").toObject(),days));
                        qDebug() << "Collecting forecast data for" << m_fcstDays << "days";
                        // Collect forecasts for all locations
                        for(int i=0;i<qMin(days.size(),m_fcstDays);i++) {
                            if(m_fcstsBuff.size()==i) {
                                m_fcstsBuff.append(WeatherApp::Forecast(m_weatherApp->locOrder()));
                            }
                            if(l == m_weatherApp->locOrder().first()) {
                                parseLoc(m_fcstsBuff[i],days[i].toObject());
                            } else {
                                m_fcstsBuff[i].addCity(l,parseDay(days[i].toObject()));
                            }
                        }
                        // On last iteration - check if anything needs to be updated in the pebble
                        if(m_fcstsBuff.first().isComplete()) {
                            m_weatherApp->updateConfig();
                            // Update and repopulate pins from shadow buffer
                            qDebug() << "Parsing" << m_fcstsBuff.size() << "forecasts";
                            m_weatherApp->updateForecasts(m_fcstsBuff,"The Weather Channel");
                            m_fcstsBuff.clear();
                        }
                        return;
                    } else if(obj.contains("errors") && obj.value("errors").toArray().size()>0) {
                        qWarning() << "Error in TWC transaction" << obj.value("errors").toArray().at(0).toObject().value("error").toObject().value("message");
                    } else {
                        qWarning() << "Unknown TWC reply" << json.toJson();
                    }
                } else {
                    qWarning() << "Error parsing TWC response" << jpe.errorString();
                }
            } else if(rpl->error() == QNetworkReply::AuthenticationRequiredError) {
                m_apiKey.clear();
                qWarning() << "Wrong TWC API key" << rpl->errorString();
            } else {
                qWarning() << "Error fetching TWC data" << rpl->errorString();
            }
        });
    }
}
