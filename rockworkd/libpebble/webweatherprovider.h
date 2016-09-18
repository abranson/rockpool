#ifndef WEBWEATHERPROVIDER_H
#define WEBWEATHERPROVIDER_H

#include "weatherapp.h"

class WatchConnection;

class QNetworkAccessManager;
class QGeoPositionInfoSource;
class QGeoPositionInfo;

class WebWeatherProvider : public QObject, public WeatherProvider
{
    Q_OBJECT
public:
    static const quint32 refreshInterval = 60 * 60 * 1000;

    WebWeatherProvider(Pebble *pebble, WatchConnection *conneciton, WeatherApp *weatherApp);
    virtual ~WebWeatherProvider() override;

    QChar getUnits() const;
    QString getLanguage() const;

signals:

public slots:
    void setUnits(const QChar &u);
    void setApiKey(const QString &key);
    void setLanguage(const QString &lang);
    void refreshWeather();

protected slots:
    void timerEvent(QTimerEvent *) override;

    void initGPS();
    void gpsError(int);
    void gpsTimeout();
    void gotPosition(const QGeoPositionInfo &gpi);
    void updateForecast();
    void watchConnected();

    virtual QString urlTemplate() const = 0;
    virtual void processLocation(const QString &locationName, const QByteArray &replyData) = 0;

protected:
    QDateTime m_lastUpdated;
    bool m_updateMissed = false;
    int m_fcstDays = 5;
    QString m_language = "en-US";
    QChar m_units = 'm';

    QString m_apiKey;
    QNetworkAccessManager *m_nam;
    QGeoPositionInfoSource *m_gps;
    Pebble *m_pebble;
    WatchConnection *m_connection;
    WeatherApp *m_weatherApp;
};

#endif // WEBWEATHERPROVIDER_H
