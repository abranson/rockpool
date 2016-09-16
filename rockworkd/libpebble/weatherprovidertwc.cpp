#include "weatherprovidertwc.h"

#include "pebble.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

const QString WeatherProviderTWC::urlTWC = "https://api.weather.com/v2/geocode/__LOC__/aggregate.json?products=conditions,fcstdaily7&language=%2&units=%3&apiKey=%4";

const QHash<QChar,QString> WeatherProviderTWC::code2units = {
    {'a',""},//all
    {'m',"metric"},
    {'e',"imperial"},
    {'h',"uk_hybrid"},
    {'s',"metric_si"},
};

// Condition Codes suitable from Yahoo API https://developer.yahoo.com/weather/documentation.html
const QHash<int,int> icon2code = {
    // {4,}, - rain possible, t-storm
    {11,4},
    {12,4},
    {13,2}, // Snow flurries
    {14,2}, // Light Snow
    {15,5}, // Blowing Snow
    {16,5}, // Snow
    {17,4}, // Hail
    {18,8}, // Sleet
    //{},   // Dust, fog, haze, etc.
    {26,1}, // Clouds
    {27,1}, // M Cloudy N
    {28,1}, // M Cloudy D
    {29,0},
    {30,0},
    {31,7}, // Clear Night
    {32,7}, // Sunny Day
    {33,7}, // Mostly Clear N
    {34,7}, // Mostly Clear D
    {35,4}, // Rain and Hail
    {36,7}, // Hot
    {37,6}, // Isolated Thunderstorms
    {39,6}, // Scattered T-Storms
    {40,6}, // Scattered showers
    {45,4}, // Thundershowers
    {46,5}, // Snow Showers
    {47,4}  // Isolated Thundershowers
};

WeatherProviderTWC::WeatherProviderTWC(Pebble *pebble, WatchConnection *connection, WeatherApp *weatherApp) :
    WebWeatherProvider(pebble, connection, weatherApp)
{
    weatherApp->setProvider(this);
}

QString WeatherProviderTWC::urlTemplate() const
{
    if(!m_apiKey.isEmpty() && code2units.contains(m_units))
        return urlTWC.arg(m_language).arg(m_units).arg(m_apiKey).replace("__LOC__","%1/%2");
    return QString();
}

void WeatherProviderTWC::processLocation(const QString &locationName, const QByteArray &replyData)
{
    QJsonParseError jpe;
    QJsonDocument json = QJsonDocument::fromJson(replyData,&jpe);
    if(jpe.error == QJsonParseError::NoError && json.isObject()) {
        QJsonObject obj = json.object();
        if(obj.contains("conditions") && !obj.value("conditions").toObject().value("errors").toBool() \
                && obj.contains("fcstdaily7") && !obj.value("fcstdaily7").toObject().value("errors").toBool())
        {
            QJsonArray days = obj.value("fcstdaily7").toObject().value("data").toObject().value("forecasts").toArray();
            qDebug() << "Updating current conditions for" << locationName;
            // Update current conditions for all locations
            m_weatherApp->injectObservation(locationName,parseObs(obj.value("conditions").toObject().value("data").toObject().value("observation").toObject(),days));
            qDebug() << "Collecting forecast data for" << m_fcstDays << "days";
            // Collect forecasts for all locations
            for(int i=0;i<qMin(days.size(),m_fcstDays);i++) {
                if(m_fcstsBuff.size()==i) {
                    m_fcstsBuff.append(WeatherApp::Forecast(m_weatherApp->locOrder()));
                }
                if(locationName == m_weatherApp->locOrder().first()) {
                    parseLoc(m_fcstsBuff[i],days[i].toObject());
                } else {
                    m_fcstsBuff[i].addCity(locationName,parseDay(days[i].toObject()));
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
}

WeatherApp::Observation WeatherProviderTWC::parseObs(const QJsonObject &obj, const QJsonArray &days)
{
    QString text = obj.value("phrase_12char").toString();
    if(text.isEmpty())
        text = obj.value("phrase_22char").toString();
    if(text.isEmpty())
        text = obj.value("phrase_32char").toString();
    return WeatherApp::Observation(obj.value(code2units.value(m_units)).toObject().value("temp").toInt(SHRT_MAX),text,
    {icon2code.value(obj.value("icon_code").toInt()),
     days.at(0).toObject().value("max_temp").toInt(SHRT_MAX),days.at(0).toObject().value("min_temp").toInt(SHRT_MAX)},
    {icon2code.value(days.at(1).toObject().contains("day")?days.at(1).toObject().value("day").toObject().value("icon_code").toInt():days.at(1).toObject().value("night").toObject().value("icon_code").toInt()),
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
        day_icon = WeatherApp::code2icon.at(icon2code.value(day.value("day").toObject().value("icon_code").toInt(),6));
    if(day.contains("night"))
        night_icon = WeatherApp::code2icon.at(icon2code.value(day.value("night").toObject().value("icon_code").toInt(),6));
    loc.initLoc(parseDay(day),sunrise,day_icon,sunset,night_icon);
}
