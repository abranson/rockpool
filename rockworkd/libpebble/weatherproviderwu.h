#ifndef WEATHERPROVIDERWU_H
#define WEATHERPROVIDERWU_H

#include "weatherapp.h"
#include "webweatherprovider.h"

class WatchConnection;
class QJsonObject;

class WeatherProviderWU : public WebWeatherProvider
{
public:
    static const QString url;
    WeatherProviderWU(Pebble *pebble, WatchConnection *conneciton, WeatherApp *weatherApp);

protected slots:
    QString urlTemplate() const;
    void processLocation(const QString &locationName, const QByteArray &replyData);

private:
    WeatherApp::Observation parseObs(const QJsonObject &obj, const QJsonArray &days);
    WeatherApp::Forecast::Data parseDay(const QJsonObject &fcst, const QJsonObject &day, const QJsonObject &night);

    bool m_locUpdated = false;
    QList<WeatherApp::Forecast> m_fcstsBuff;
};

#endif // WEATHERPROVIDERWU_H
