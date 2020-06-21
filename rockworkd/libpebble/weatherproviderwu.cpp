#include "weatherproviderwu.h"

#include "pebble.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <math.h>

#define CALCULATE_SUNRISE
#ifdef CALCULATE_SUNRISE
const QString WeatherProviderWU::url = "https://api.wunderground.com/api/%1/conditions/forecast10day/lang:%2/q/__LOC__.json";
// We don't need sun phases (astronomy) as we're going to calculate them using below implementation
// https://github.com/dentoyan/sunrise
/*
 * sunrise.h
 *
 * Sunrise Time Class
 * written by Joerg Dentler
 *
 */
// Version 1.0.0        14-12-19 initial design
/////////////////////////////////////////////////////////////////////////////
// Permission is granted to anyone to use this software for any
// purpose and to redistribute it in any way, subject to the following
// restrictions:
//
// 1. The author is not responsible for the consequences of use of
//    this software, no matter how awful, even if they arise
//    from defects in it.
//
// 2. The origin of this software must not be misrepresented, either
//    by explicit claim or by omission.
//
// 3. Altered versions must be plainly marked as such, and must not
//    be misrepresented (by explicit claim or omission) as being
//    the original software.
//
// 4. This notice must not be removed or altered.
/////////////////////////////////////////////////////////////////////////////

class Sunrise
{
public:
    /**
     * create sunrise calculator for a certain map point
     *
     * latitude
     * north is +
     * south is -
     *
     * longitude
     * east is +
     * west is -
     *
     * eleveation in meters above ground
     *
     * examples:
     *
     * Berlin
     * 52.516N 13.271E
     *
     * Boa Vista
     * 16.141N 22.904W
     *
     */
    Sunrise(double latitude_, double longitude_, double elevation_ = 0) :
        latitude(latitude_),
        longitude(-longitude_),
        elevation(elevation_)
    {
    }


    /*
     * calculation functions
     * param d is local date
     * returns local time format
     */
    QTime sunrise(const QDate &d) {
        if (!d.isValid())
            return QTime();

        double j = sunrise(julian(d));
        return date(j).time();
    }
    QTime noon(const QDate &d) {
        if (!d.isValid())
            return QTime();

        double j = noon(julian(d));
        return date(j).time();
    }
    QTime sunset(const QDate &d) {
        if (!d.isValid())
            return QTime();

        double j = sunset(julian(d));
        return date(j).time();
    }

private:
    //  Convert radian angle to degrees
    double dRadToDeg(double dAngleRad) {
        return (180.0 * dAngleRad / M_PI);
    }
    //  Convert degree angle to radians
    double dDegToRad(double dAngleDeg) {
        return (M_PI * dAngleDeg / 180.0);
    }
    double normalizedAngle(double a) {
        while (!(a < 360.0))
            a -= 360.0;
        while (!(a > -360.0))
            a += 360.0;
        return a;
    }
    double julianCycle(int iJulianDay) {
        double n = double(iJulianDay) - 2451545.0009 - longitude / 360.0;
        return floor(n + 0.5);
    }
    double solarNoonApprox(int iJulianDay) {
        return 2451545.0009  + longitude / 360.0 + julianCycle(iJulianDay);
    }
    double solarMean(double noon) {
        double m = 357.5291 + 0.98560028 * (noon - 2451545.0);
        return normalizedAngle(m);
    }
    double center(double mean) {
        double m1 = normalizedAngle(mean * 1.0);
        double m2 = normalizedAngle(mean * 2.0);
        double m3 = normalizedAngle(mean * 3.0);
        m1 = sin(dDegToRad(m1));
        m2 = sin(dDegToRad(m2));
        m3 = sin(dDegToRad(m3));
        return 1.9148*m1 + 0.0200*m2 + 0.0003*m3;
    }
    double gamma(double m) {
        double c = center(m);
        return normalizedAngle(m + 102.9372 + c + 180.0);
    }
    double solarTransit(double noon) {
        double m = solarMean(noon);
        double g = gamma(m);
        m = sin(dDegToRad(m));
        g = normalizedAngle(g * 2.0);
        g = sin(dDegToRad(g));
        return noon + 0.0053*m - 0.0069*g;
    }
    bool omega(double &o, double noon) {
        double m = solarMean(noon);
        double g = gamma(m);
        double l = dDegToRad(latitude);
        double sd = sin(dDegToRad(g)) * sin(dDegToRad(23.45));
        double cd = cos(asin(sd));
        double el = 0;
        if (elevation > 0.0) {
          el = -2.076;
          el *= sqrt(elevation) / 60.0;
        }
        o = sin(dDegToRad(-0.83 + el)) - sin(l)*sd;
        o /= cos(l) * cd;
        if (o > 1.0 || o < -1.0)
          return false;
        o = dRadToDeg(acos(o));
        return true;
    }
    double sunset(int iJulianDay) {
        double noon = solarNoonApprox(iJulianDay);
        double m = solarMean(noon);
        double g = gamma(m);
        m = sin(dDegToRad(m));
        g = normalizedAngle(g * 2.0);
        g = sin(dDegToRad(g));
        double o;
        if (!omega(o, noon))
          return -1.0;
        o += longitude;
        o /= 360.0;
        return 2451545.0009 + o + julianCycle(iJulianDay) + 0.0053*m - 0.0069*g;
    }
    double noon(int iJulianDay) {
        double noon = solarNoonApprox(iJulianDay);
        return solarTransit(noon);
    }
    double sunrise(int iJulianDay) {
        double transit = noon(iJulianDay);
        double ss = sunset(iJulianDay);
        return ss < 0.0 ? -1.0 : transit - (ss - transit);
    }

    QDateTime date(double julian) {
        if (julian < 0.0)
            return QDateTime();
        // The day number is the integer part of the date
        int julianDays = (int)(julian);
        QDate d = QDate::fromJulianDay(julianDays);
        // The fraction is the time of day
        double julianMSecs = (julian - static_cast<double>(julianDays)) * 86400.0 * 1000.0;
        // Julian days start at noon (12:00 UTC)
        return QDateTime(d, QTime(12, 0, 0, 0), Qt::UTC).addMSecs(qRound(julianMSecs)).toLocalTime();
    }

    int julian(const QDate &d) {
        QDateTime dt(d, QTime(12, 1, 0, 0));
        return dt.toUTC().date().toJulianDay();
    }

    const double latitude;
    const double longitude;
    const double elevation;
};
#else //CALCULATE_SUNRISE
// Astronomy gives sun phases only for today, so all forecast days will have today's sunrise/set timings. Not a big deal for 2-5 days.
const QString WeatherProviderWU::url = "https://api.wunderground.com/api/%1/astronomy/conditions/forecast10day/lang:%2/q/__LOC__.json";
#endif//
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
    {"tstorms",4},
    // night
    {"nt_chanceflurries",2},
    //{"nt_chancerain",6},
    {"nt_chancesleet",8},
    {"nt_chancesnow",2},
    {"nt_chancetstorms",3},
    {"nt_clear",7},
    {"nt_cloudy",1},
    {"nt_flurries",2},
    {"nt_fog",1},
    //hazy
    {"nt_mostlycloudy",0},
    {"nt_mostlysunny",7},
    {"nt_partlycloudy",0},
    {"nt_partlysunny",0},
    {"nt_rain",4},
    {"nt_sleet",8},
    {"nt_snow",5},
    {"nt_sunny",7},
    {"nt_tstorms",4}
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
        return url.arg(m_apiKey).arg(m_language).replace("__LOC__","%1,%2");
    return QString();
}

void WeatherProviderWU::processLocation(const QString &locationName, const QByteArray &replyData)
{
    QJsonParseError jpe;
    QJsonDocument json = QJsonDocument::fromJson(replyData,&jpe);
    if(jpe.error == QJsonParseError::NoError && json.isObject()) {
        QJsonObject obj = json.object();
        if(obj.contains("response")
                && obj.value("response").toObject().value("features").toObject().value("conditions").toInt() == 1
                && obj.value("response").toObject().value("features").toObject().value("forecast10day").toInt() == 1
#ifndef CALCULATE_SUNRISE
                && obj.value("response").toObject().value("features").toObject().value("astronomy").toInt() == 1
#endif//CALCULATE_SUNRISE
                )
        {
            QJsonArray fdays = obj.value("forecast").toObject().value("simpleforecast").toObject().value("forecastday").toArray(),
                       tdays = obj.value("forecast").toObject().value("txt_forecast").toObject().value("forecastday").toArray();
            qDebug() << "Updating current conditions for" << locationName;
            QJsonObject location = obj.value("current_observation").toObject().value("display_location").toObject();
            // Update current conditions for all locations
            m_weatherApp->injectObservation(((locationName == m_weatherApp->locOrder().first())?location.value("city").toString():locationName),parseObs(obj.value("current_observation").toObject(),fdays));
            qDebug() << "Collecting forecast data for" << m_fcstDays << "days";
#ifdef CALCULATE_SUNRISE
            Sunrise s(location.value("latitude").toString().toDouble(),location.value("longitude").toString().toDouble(),location.value("elevation").toString().toDouble());
#else//CALCULATE_SUNRISE
            // Collect forecasts for all locations
            QJsonObject sunr=obj.value("sun_phase").toObject().value("sunrise").toObject(),
                        suns=obj.value("sun_phase").toObject().value("sunset").toObject();
#endif//CALCULATE_SUNRISE
            for(int i=0;i<qMin(fdays.size(),m_fcstDays);i++) {
                if(m_fcstsBuff.size()==i) {
                    m_fcstsBuff.append(WeatherApp::Forecast(m_weatherApp->locOrder()));
                }
                WeatherApp::Forecast::Data fcst = parseDay(fdays[i].toObject(),tdays[i*2].toObject(),tdays[i*2+1].toObject());
                if(locationName == m_weatherApp->locOrder().first()) {
#ifdef  CALCULATE_SUNRISE
                    QDate d = QDate::currentDate().addDays(i);
                    QTime rtime = s.sunrise(d),
                          stime = s.sunset(d);
                    QDateTime sunrise(d,rtime), sunset(d,stime);
                    if(sunrise.isDaylightTime()) {
                        sunrise.addSecs(3600);
                        sunset.addSecs(3600);
                    }
#else //CALCULATE_SUNRISE
                    QDateTime sunrise(QDate::currentDate().addDays(i),QTime(sunr.value("hour").toInt(),sunr.value("minute").toInt())),
                              sunset(QDate::currentDate().addDays(i),QTime(suns.value("hour").toInt(),suns.value("minute").toInt()));
#endif//CALCULATE_SUNRISE
                    QString day = WeatherApp::code2icon.value(icon2code.value(tdays[i*2].toObject().value("icon").toString(),6)),
                          night = WeatherApp::code2icon.value(icon2code.value(tdays[i*2+1].toObject().value("icon").toString(),6));
                    m_fcstsBuff[i].initLoc(fcst,sunrise,day,sunset,night,location.value("city").toString());
                } else {
                    m_fcstsBuff[i].addCity(locationName,fcst);
                }
            }
            // On last iteration - check if anything needs to be updated in the pebble
            if(!m_fcstsBuff.isEmpty() && m_fcstsBuff.first().isComplete()) {
                m_weatherApp->updateConfig();
                // Update and repopulate pins from shadow buffer
                qDebug() << "Parsing" << m_fcstsBuff.size() << "forecasts";
                m_weatherApp->updateForecasts(m_fcstsBuff,"The Weather Underground");
                m_fcstsBuff.clear();
            }
        } else if(obj.contains("response") && obj.value("response").toObject().contains("error")) {
            qWarning() << "Error in WU transaction:" << obj.value("response").toObject().value("error").toObject().value("description").toString();
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
        qRound(obj.value(TEMP_TUSFX).toDouble(SHRT_MAX)),
        obj.value("weather").toString(),
        {
            icon2code.value(obj.value("icon").toString(),6), // default is weather icon
            days.at(0).toObject().value("high").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt(),
            days.at(0).toObject().value("low").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt()
        },
        {
            icon2code.value(days.at(1).toObject().value("icon").toString(),6),
            days.at(1).toObject().value("high").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt(),
            days.at(1).toObject().value("low").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt()
        }
    );
}
#define TEXT_UNITS ((m_units=='m')?"fcttext_metric":"fcttext")
WeatherApp::Forecast::Data WeatherProviderWU::parseDay(const QJsonObject &fcst, const QJsonObject &day, const QJsonObject &night)
{
    WeatherApp::Forecast::Data data;
    data.min_temp = fcst.value("low").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt();
    data.max_temp = fcst.value("high").toObject().value(TEMP_UNITS).toString(QString::number(SHRT_MAX)).toInt();
    data.day_text = day.value(TEXT_UNITS).toString();
    data.night_text = night.value(TEXT_UNITS).toString();
    return data;
}
