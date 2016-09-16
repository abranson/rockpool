#ifndef WEATHERAPP_H
#define WEATHERAPP_H

#include <QObject>
#include <QDateTime>
#include <QStringList>
#include <QHash>
#include <QUuid>

#include <limits.h>

class Pebble;

class WeatherProvider
{
public:
    virtual void setUnits(const QChar &u) = 0;
    virtual QChar getUnits() const = 0;
    virtual void setLanguage(const QString &lang) = 0;
    virtual void refreshWeather() = 0;
};

class WeatherApp : public QObject
{
    Q_OBJECT
public:
    static const QUuid currentLocationUUID;
    static const QUuid appUUID;
    static const QByteArray appConfigKey;
    static const QString cityFcast;
    static const QStringList code2icon;

    WeatherApp(Pebble *pebble, const QVariantList &locations);

    struct Location {
        QString name;
        QString lat;
        QString lng;
        QUuid uuid;
        bool operator==(const Location &that) const {
            return (uuid==currentLocationUUID)?(that.uuid==currentLocationUUID):(uuid == that.uuid && lat == that.lat && lng == that.lng);
        }
    };

    class Observation {
    public:
        struct Data {
            // 0 P Cloudy
            // 1 Cloudy
            // 2 L Snow
            // 3 Rain
            // 4 Shower
            // 5 Snow
            // 6 P Cloudy + L Rain
            // 7 Sunny
            // 8 Snow + Rain
            int condition;
            int temp_hi;
            int temp_low;
        };
        Observation();
        Observation(int tmp, const QString &text, const Data &today, const Data &tomorrow);
        bool operator!=(const Observation &that) const;
        bool operator==(const Observation &that) const;

        int temp_now;
        Data today;
        Data tomorrow;
        QDateTime timestamp;
        QString text;
    };

    class Forecast {
    public:
        struct Data {
            int min_temp = SHRT_MAX;
            int max_temp = SHRT_MAX;
            QString   day_text;
            QString night_text;
        };
        Forecast(const QStringList &locOrder);
        Forecast(const QStringList &locOrder, const Data &day, const QDateTime &sunrise, const QString &day_icon, const QDateTime &sunset, const QString &night_icon);

        void initLoc(const Data &day, const QDateTime &sunrise, const QString &day_icon, const QDateTime &sunset, const QString &night_icon);
        void addCity(const QString &name, const Data &day);
        void reorder(const QStringList &newOrder);
        QJsonObject getPin(int daynum, bool night=false, const QString &provider=QString()) const;
        bool equal(const Forecast &that, int daypart=-1) const;
        bool isValid(bool night=false) const;
        bool isComplete() const;

    private:
        static QString fmtTmp(int tmp);

        Data m_here;
        QHash<QString,WeatherApp::Forecast::Data> m_cities;
        QStringList m_locOrder;
        QDateTime sunrise;
        QDateTime sunset;
        QString   day_icon;
        QString night_icon;
    };

    void setProvider(WeatherProvider *provider);

    QVariantList getLocations() const;
    const QStringList & locOrder() const { return m_locOrder; }
    Location getLocation(const QString &loc) const { return m_locations.value(loc); }
    Location & location(const QString &loc) { return m_locations[loc]; }

signals:
    void locationsChanged();
    void requestUpdate();

public slots:
    void updateConfig();
    void setLocations(const QVariantList &locs, bool push=true);
    void updateForecasts(const QList<Forecast> &fcstBuf, const QString &provider = QString());
    void injectObservation(const QString &l, const Observation &obs, bool force=false);

private slots:
    void blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack);

private:
    QStringList m_locOrder;
    QHash<QString,Location> m_locations;
    bool m_locUpdated = false;
    int m_fcstDays = 5;
    QHash<QUuid,Observation> m_obss;
    QList<Forecast> m_forecasts;
    Pebble *m_pebble;
    WeatherProvider *m_provider;
};

#endif // WEATHERAPP_H
