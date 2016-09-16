#include "weatherproviderwu.h"

#include "pebble.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>


const QString WeatherProviderWU::url = "https://api.wunderground.com/api/%1/astronomy/conditions/forecast10day/lang:%2/q/__LOC__.json";
//    {'m',"metric"},
//    {'e',"imperial"},
//    {'h',"uk_hybrid"},
const QHash<QString,int> icon2code = {
    {"chanceflurries",2},
    //{"chancerain",6},
    {"chancesleet",8},
    {"chancesnow",2},
    {"chancetstorms",3},
    {"clear",7},
    {"cloudy",1},
    {"flurries",2},
    {"fog",1},
    //hazy
    {"mostlycloudy",0},
    {"mostlysunny",7},
    {"partlycloudy",0},
    {"partlysunny",0},
    {"rain",4},
    {"sleet",8},
    {"snow",5},
    {"sunny",7},
    {"tstorms",4}
};

WeatherProviderWU::WeatherProviderWU(Pebble *pebble, WatchConnection *connection, WeatherApp *weatherApp) :
    WebWeatherProvider(pebble,connection,weatherApp)
{
    m_language = "EN";
    weatherApp->setProvider(this);
}

QString WeatherProviderWU::urlTemplate() const
{
    if(!m_apiKey.isEmpty())
        return url.arg(m_language).arg(m_apiKey).replace("__LOC__","%1,%2");
    return QString();
}

void WeatherProviderWU::processLocation(const QString &locationName, const QByteArray &replyData)
{
    QJsonParseError jpe;
    QJsonDocument json = QJsonDocument::fromJson(replyData,&jpe);
    if(jpe.error == QJsonParseError::NoError && json.isObject()) {
        QJsonObject obj = json.object();
        if(obj.contains("response") \
                && obj.value("response").toObject().value("features").toObject().value("conditions").toInt() == 1 \
                && obj.value("response").toObject().value("features").toObject().value("forecast10day").toInt() == 1 \
                && obj.value("response").toObject().value("features").toObject().value("astronomy").toInt() == 1 \
                )
        {
            QJsonArray fdays = obj.value("forecast10day").toObject().value("simpleforecast").toObject().value("forecastday").toArray(),
                       tdays = obj.value("forecast10day").toObject().value("txt_forecast").toObject().value("forecastday").toArray();
            qDebug() << "Updating current conditions for" << locationName;
            // Update current conditions for all locations
            m_weatherApp->injectObservation(locationName,parseObs(obj.value("current_observation").toObject(),fdays));
            qDebug() << "Collecting forecast data for" << m_fcstDays << "days";
            // Collect forecasts for all locations
            QJsonObject sunr=obj.value("sun_phase").toObject().value("sunrise").toObject(),
                        suns=obj.value("sun_phase").toObject().value("sunset").toObject();
            for(int i=0;i<qMin(fdays.size(),m_fcstDays);i++) {
                if(m_fcstsBuff.size()==i) {
                    m_fcstsBuff.append(WeatherApp::Forecast(m_weatherApp->locOrder()));
                }
                WeatherApp::Forecast::Data fcst = parseDay(fdays[i].toObject(),tdays[i*2].toObject(),tdays[i*2+1].toObject());
                if(locationName == m_weatherApp->locOrder().first()) {
                    QDateTime sunrise(QDate::currentDate().addDays(i),QTime(sunr.value("hour").toInt(),sunr.value("minute").toInt())),
                              sunset(QDate::currentDate().addDays(i),QTime(suns.value("hour").toInt(),suns.value("minute").toInt()));
                    QString day = WeatherApp::code2icon.value(icon2code.value(tdays[i*2].toObject().value("icon").toString(),6)),
                            night = WeatherApp::code2icon.value(icon2code.value(tdays[i*2+1].toObject().value("icon").toString(),6));
                    m_fcstsBuff[i].initLoc(fcst,sunrise,day,sunset,night);
                } else {
                    m_fcstsBuff[i].addCity(locationName,fcst);
                }
            }
            // On last iteration - check if anything needs to be updated in the pebble
            if(m_fcstsBuff.first().isComplete()) {
                m_weatherApp->updateConfig();
                // Update and repopulate pins from shadow buffer
                qDebug() << "Parsing" << m_fcstsBuff.size() << "forecasts";
                m_weatherApp->updateForecasts(m_fcstsBuff,"The Weather Underground");
                m_fcstsBuff.clear();
            }
        } else if(obj.contains("errors") && obj.value("errors").toArray().size()>0) {
            qWarning() << "Error in WU transaction" << obj.value("errors").toArray().at(0).toObject().value("error").toObject().value("message");
        } else {
            qWarning() << "Unknown WU reply" << json.toJson();
        }
    } else {
        qWarning() << "Error parsing WU response" << jpe.errorString();
    }
}

#define TEMP_TUSFX ((m_units=='e')?"temp_f":"temp_c")
#define TEMP_UNITS ((m_units=='e')?"fahreheit":"celsius")
WeatherApp::Observation WeatherProviderWU::parseObs(const QJsonObject &obj, const QJsonArray &days)
{
    // Use celsius unless ecplicitly requested imperial units (e)
    return WeatherApp::Observation(
        obj.value(TEMP_TUSFX).toInt(SHRT_MAX),
        obj.value("weather").toString(),
        {
            icon2code.value(obj.value("icon").toString(),6), // default is weather icon
            days.at(0).toObject().value("high").toObject().value(TEMP_UNITS).toInt(SHRT_MAX),
            days.at(0).toObject().value("low").toObject().value(TEMP_UNITS).toInt(SHRT_MAX)
        },
        {
            icon2code.value(days.at(1).toObject().value("icon").toString(),6),
            days.at(1).toObject().value("high").toObject().value(TEMP_UNITS).toInt(SHRT_MAX),
            days.at(1).toObject().value("min").toObject().value(TEMP_UNITS).toInt(SHRT_MAX)
        }
    );
}
#define TEXT_UNITS ((m_units=='m')?"fcttext_metric":"fcttext")
WeatherApp::Forecast::Data WeatherProviderWU::parseDay(const QJsonObject &fcst, const QJsonObject &day, const QJsonObject &night)
{
    WeatherApp::Forecast::Data data;
    data.min_temp = fcst.value("low").toObject().value(TEMP_UNITS).toInt(SHRT_MAX);
    data.max_temp = fcst.value("high").toObject().value(TEMP_UNITS).toInt(SHRT_MAX);
    data.day_text = day.value(TEXT_UNITS).toString();
    data.night_text = night.value(TEXT_UNITS).toString();
    return data;
}
