#ifndef WEATHERPROVIDERTWC_H
#define WEATHERPROVIDERTWC_H

#include "weatherapp.h"

class WatchConnection;

class QNetworkAccessManager;
class QGeoPositionInfoSource;
class QGeoPositionInfo;
class QJsonObject;

class WeatherProviderTWC : public QObject, public WeatherProvider
{
    Q_OBJECT
public:
    static const quint32 refreshInterval = 60 * 60 * 1000;
    static const QString urlTWC;
    static const QHash<QChar,QString> code2units;
    static const QHash<int,QString> code2icon;

    WeatherProviderTWC(Pebble *pebble, WatchConnection *conneciton, WeatherApp *weatherApp);

    QChar getUnits() const;
    WeatherApp::Observation parseObs(const QJsonObject &obj, const QJsonArray &days);
    static WeatherApp::Forecast::Data parseDay(const QJsonObject &day);
    static void parseLoc(WeatherApp::Forecast &loc, const QJsonObject &day);

signals:

public slots:
    void setUnits(const QChar &u);
    void setApiKey(const QString &key);
    void setLanguage(const QString &lang);
    void refreshWeather();

protected slots:
    void timerEvent(QTimerEvent *) override;

private slots:
    void updateForecast();
    void initGPS();
    void gpsError(int);
    void gpsTimeout();
    void gotPosition(const QGeoPositionInfo &gpi);

    void watchConnected();

private:
    bool m_updateMissed = false;
    bool m_locUpdated = false;
    int m_fcstDays = 5;
    QList<WeatherApp::Forecast> m_fcstsBuff;
    QString m_language = "en-US";
    QChar m_units = 'm';

    QString m_apiKey;
    QNetworkAccessManager *m_nam;
    QGeoPositionInfoSource *m_gps;
    Pebble *m_pebble;
    WatchConnection *m_connection;
    WeatherApp *m_weatherApp;
};

#endif // WEATHERPROVIDERTWC_H
