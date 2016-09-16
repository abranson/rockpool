#ifndef WEATHERPROVIDERTWC_H
#define WEATHERPROVIDERTWC_H

#include "weatherapp.h"
#include "webweatherprovider.h"

class WatchConnection;

class QJsonObject;

class WeatherProviderTWC : public WebWeatherProvider
{
public:
    static const QString urlTWC;
    static const QHash<QChar,QString> code2units;

    WeatherProviderTWC(Pebble *pebble, WatchConnection *conneciton, WeatherApp *weatherApp);

protected slots:
    QString urlTemplate() const;
    void processLocation(const QString &locationName, const QByteArray &replyData);

private:
    WeatherApp::Observation parseObs(const QJsonObject &obj, const QJsonArray &days);
    static WeatherApp::Forecast::Data parseDay(const QJsonObject &day);
    static void parseLoc(WeatherApp::Forecast &loc, const QJsonObject &day);

    bool m_locUpdated = false;
    QList<WeatherApp::Forecast> m_fcstsBuff;
};

#endif // WEATHERPROVIDERTWC_H
