#include "weatherapp.h"

#include "blobdb.h"
#include "watchdatawriter.h"
#include "platforminterface.h"

#include <QJsonArray>

#include <libintl.h>

/**
 * @brief The WeatherConfigurations class is used to store weather condition references
 * to BlobDB. It just lists the BlobDB Keys (up to 6) represented by UUIDs.
 */
class WeatherConfigurations: public BlobDbItem
{
public:
    static const QByteArray blobKey;
    static const quint16 blobLength = 0x61;

    WeatherConfigurations(const QList<QUuid> &configs)
    {m_confs = configs.length()>6 ? configs.mid(0,6):configs;}

    QByteArray itemKey() const;
    QByteArray serialize() const;

private:
    QList<QUuid> m_confs;
};
const QByteArray WeatherConfigurations::blobKey = QByteArray("weatherApp",10);

QByteArray WeatherConfigurations::itemKey() const
{
    return blobKey;
}

QByteArray WeatherConfigurations::serialize() const
{
    QByteArray ret;
    ret.append(m_confs.length());
    foreach(const QUuid &uuid,m_confs) {
        ret.append(uuid.toRfc4122());
    }
    if(m_confs.length()<6)
        ret.append(QByteArray(16*(6-m_confs.length()),'\0'));
    return ret;
}
/**
 * @brief The WeatherObservation class used to store current weather condition into BlobDB.
 * Stored data is then used by WeatherApp to show the condition and icon. BlobDB Key MUST
 * be referenced in WeatherConfiguration section.
 */
class WeatherObservation: public BlobDbItem
{
public:
    WeatherObservation(const WeatherApp::Location &loc, const WeatherApp::Observation &obs);

    struct Condition {
        QString location;
        QString condition;
    };

    QByteArray itemKey() const;
    QByteArray serialize() const;

    const QUuid & key() const {return m_configKey;}

private:

    QUuid m_configKey;
    quint8 m_type = 0x3;
    qint16 m_current_temp = SHRT_MAX;
    quint8 m_today_cond = 0;
    qint16 m_today_temp_hi = SHRT_MAX;
    qint16 m_today_temp_low = SHRT_MAX;
    quint8 m_tomorrow_cond = 0;
    qint16 m_tomorrow_temp_hi = SHRT_MAX;
    qint16 m_tomorrow_temp_low = SHRT_MAX;
    time_t m_sample_time = 0;
    Condition m_condition;
    // Should probably be like this, but...
    //QList<Condition> m_conditions;
};

WeatherObservation::WeatherObservation(const WeatherApp::Location &loc, const WeatherApp::Observation &obs):
    BlobDbItem(),
    m_configKey(loc.uuid),
    m_current_temp(obs.temp_now),
    m_today_cond(obs.today.condition),
    m_today_temp_hi(obs.today.temp_hi),
    m_today_temp_low(obs.today.temp_low),
    m_tomorrow_cond(obs.tomorrow.condition),
    m_tomorrow_temp_hi(obs.tomorrow.temp_hi),
    m_tomorrow_temp_low(obs.tomorrow.temp_low)
{
    m_condition.location = loc.name;
    m_condition.condition = obs.text;
    m_sample_time = obs.timestamp.toUTC().toTime_t();
}

QByteArray WeatherObservation::itemKey() const
{
    return m_configKey.toRfc4122();
}
QByteArray WeatherObservation::serialize() const
{
    QByteArray ret,str;
    WatchDataWriter w(&ret),s(&str);
    ret.append(m_type);
    w.writeLE<qint16>(m_current_temp);
    ret.append(m_today_cond);
    w.writeLE<qint16>(m_today_temp_hi);
    w.writeLE<qint16>(m_today_temp_low);
    ret.append(m_tomorrow_cond);
    w.writeLE<qint16>(m_tomorrow_temp_hi);
    w.writeLE<qint16>(m_tomorrow_temp_low);
    w.writeLE<quint32>(m_sample_time);
    //ret.append(m_conditions.length());
    ret.append(1); // so far i didn't see more than 1, let's not overcomplicate
    //for(int i=0;i<m_conditions.length();i++) {
        //QByteArray b = m_conditions.at(i).location.toUtf8();
        QByteArray b = m_condition.location.toUtf8();
        s.writeLE<quint16>(b.length());
        s.writeBytes(b.length(),b);
        //b = m_conditions.at(i).condition.toUtf8();
        b = m_condition.condition.toUtf8();
        s.writeLE<quint16>(b.length());
        s.writeBytes(b.length(),b);
    //}
    w.writeLE<quint16>(str.length());
    ret.append(str);
    return ret;
}

/**
 * @brief WeatherApp::Observation::Observation
 */
WeatherApp::Observation::Observation(int temp, const QString &text, const Data &today, const Data &tomorrow):
    temp_now(temp),
    today(today),
    tomorrow(tomorrow),
    text(text)
{
    timestamp = QDateTime::currentDateTime();
}
WeatherApp::Observation::Observation()
{
    temp_now = SHRT_MAX;
    today = {0,SHRT_MAX,SHRT_MAX};
    tomorrow = today;
    text = "";
}

bool WeatherApp::Observation::operator==(const WeatherApp::Observation &that) const
{
    if(temp_now!=that.temp_now) return false;
    if(today.condition != that.today.condition || today.temp_hi != that.today.temp_hi || today.temp_low != that.today.temp_low) return false;
    if(tomorrow.condition != that.tomorrow.condition || tomorrow.temp_hi != that.tomorrow.temp_hi || tomorrow.temp_low != tomorrow.temp_low) return false;
    if(text != that.text) return false;
    return true;
}
bool WeatherApp::Observation::operator !=(const WeatherApp::Observation &that) const
{
    return !(*this == that);
}

/**
 * @brief WeatherApp::Forecast::Forecast Constructs empty Forecast element
 * @param locOrder List of strings with locations order used to produce TimelinePin.
 */
WeatherApp::Forecast::Forecast(const QStringList &locOrder):
    m_locOrder(locOrder)
{
}
WeatherApp::Forecast::Forecast(const QStringList &locOrder, const Data &day, const QDateTime &sunrise, const QString &day_icon, const QDateTime &sunset, const QString &night_icon):
    m_here(day),
    m_locOrder(locOrder),
    sunrise(sunrise),
    sunset(sunset),
    day_icon(day_icon),
    night_icon(night_icon)
{
}

void WeatherApp::Forecast::initLoc(const Data &day, const QDateTime &_sunrise, const QString &_day_icon, const QDateTime &_sunset, const QString &_night_icon)
{
    m_here = day;
    sunrise = _sunrise;
    sunset = _sunset;
    day_icon = _day_icon;
    night_icon = _night_icon;
}

void WeatherApp::Forecast::addCity(const QString &name, const Data &day)
{
    m_cities.insert(name,day);
}

void WeatherApp::Forecast::reorder(const QStringList &newOrder)
{
    m_locOrder = newOrder;
}

bool WeatherApp::Forecast::equal(const Forecast &that, int daypart) const
{
    if(sunrise != that.sunrise || sunset != that.sunset) return false;
    if(m_here.min_temp != that.m_here.min_temp || m_here.max_temp != that.m_here.max_temp) return false;
    if(m_cities.size() != that.m_cities.size()) return false;
    if(m_locOrder != that.m_locOrder) return false;
    foreach(const QString &key,m_cities.keys()) {
        if(!that.m_cities.contains(key)) return false;
        if(m_cities.value(key).min_temp != that.m_cities.value(key).min_temp) return false;
        if(m_cities.value(key).max_temp != that.m_cities.value(key).max_temp) return false;
        if((daypart < 0 || daypart == 0) && m_cities.value(key).day_text != that.m_cities.value(key).day_text) return false;
        if((daypart < 0 || daypart < 0) && m_cities.value(key).night_text != that.m_cities.value(key).night_text) return false;
    }
    if(daypart < 0 || daypart == 0) {
        if(day_icon != that.day_icon) return false;
        if(m_here.day_text != that.m_here.day_text) return false;
    }
    if(daypart < 0 || daypart > 0) {
        if(night_icon != that.night_icon) return false;
        if(m_here.night_text != that.m_here.night_text) return false;
    }
    return true;
}
bool WeatherApp::Forecast::isValid(bool night) const
{
    if(night) {
        //return (night_icon>0 && !m_here.night_text.isEmpty());
        return !m_here.night_text.isEmpty(); // not all icons collected yet
    } else {
        //return (day_icon>0 && !m_here.day_text.isEmpty());
        return !m_here.day_text.isEmpty();
    }
}

bool WeatherApp::Forecast::isComplete() const
{
    return (sunrise.isValid() && sunset.isValid() && m_cities.keys().toSet() == m_locOrder.mid(1).toSet());
}

QString WeatherApp::Forecast::fmtTmp(int tmp)
{
    if(tmp == SHRT_MAX) return "-";
    return QString("%1\u00b0").arg(QString::number(tmp));
}

QJsonObject WeatherApp::Forecast::getPin(int daynum, bool night, const QString &provider) const
{
    QJsonObject ret,layout;
    QStringList hs,ps;

    QByteArray uuid = WeatherApp::appUUID.toRfc4122();
    uuid[15] = (daynum*2)+(night?2:1);
    QUuid guid = QUuid::fromRfc4122(uuid);
    ret.insert("id",QString("weather-day%1-%2").arg(QString::number(daynum),night?"night":"noon"));
    ret.insert("guid",guid.toString().mid(1,36));
    ret.insert("time",(night?sunset:sunrise).toUTC().toString(Qt::ISODate));
    ret.insert("updateTime",QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    ret.insert("createTime",ret.value("updateTime"));
    ret.insert("dataSource",QString("uuid:%1").arg(WeatherApp::appUUID.toString().mid(1,36)));
    ret.insert("source",QString::fromLocal8Bit(WeatherApp::appConfigKey));
    layout.insert("type",QString("weatherPin"));
    layout.insert("title",QString::fromLocal8Bit(gettext(night?"Sunset":"Sunrise")));
    layout.insert("subtitle",QString("%1/%2").arg(fmtTmp(m_here.max_temp)).arg(fmtTmp(m_here.min_temp)));
    layout.insert("body",night?m_here.night_text:m_here.day_text);
    layout.insert("tinyIcon", night ? night_icon : day_icon);
    layout.insert("largeIcon",layout.value("tinyIcon"));
    layout.insert("locationName",QString::fromLocal8Bit(gettext("Current Location")));
    foreach(const QString &city, m_locOrder) {
        if(m_cities.contains(city)) {
            hs.append("");
            ps.append(WeatherApp::cityFcast.arg(city).arg(fmtTmp(m_cities.value(city).max_temp),fmtTmp(m_cities.value(city).min_temp)).arg(night?m_cities.value(city).night_text:m_cities.value(city).day_text));
        }
    }
    if(!provider.isEmpty()) {
        hs.append(QString::fromLocal8Bit(gettext("Provided by:")));
        ps.append(provider);
    }
    layout.insert("headings",QJsonArray::fromStringList(hs));
    layout.insert("paragraphs",QJsonArray::fromStringList(ps));
    layout.insert("lastUpdated",ret.value("updateTime"));
    ret.insert("layout",layout);
    return ret;
}

const QString WeatherApp::cityFcast = "\u2014\n%1\n%2/%3, %4";
const QUuid WeatherApp::currentLocationUUID = QUuid("5f63c159-1c67-455f-897e-125d70664c4f");
const QUuid WeatherApp::appUUID = QUuid("61b22bc8-1e29-460d-a236-3fe409a439ff");
const QByteArray WeatherApp::appConfigKey = QByteArray("weatherApp",10);
/**
 * @brief WeatherApp::WeatherApp Creates WeatherApp instance for given Pebble.
 * @param pebble parent Pebble instance to provide BlobDB API.
 * @param locations initial location list used to pre-populate locations which are supposed to be
 * already stored in Pebble's BlobDB. Does not trigger Blob update, instead used to calculate
 * whether update is required on consequent updates.
 */
WeatherApp::WeatherApp(Pebble *pebble, const QVariantList &locations) :
    QObject(pebble),
    m_pebble(pebble),
    m_provider(nullptr)
{
    connect(m_pebble->blobdb(), &BlobDB::blobCommandResult, this, &WeatherApp::blobdbAckHandler);

    setLocations(locations,false);
}

void WeatherApp::setProvider(WeatherProvider *provider)
{
    m_provider = provider;
}

/**
 * @brief WeatherApp::updateForecasts repopulates forecasts into Timeline.
 * @param fcstBuf list of forecasts, should be presented in correct order of days, from
 * today to WeatherApp::m_fcstDays days after today. Pin is repopulated only if it
 * does not equal to the one already cached in memory (eg. forecast changed).
 * @param provider optional string which should reflect Weather Provider name
 * (eg. "The Weather Channel")
 */
void WeatherApp::updateForecasts(const QList<Forecast> &fcstBuf, const QString &provider)
{
    for(int i=0;i<fcstBuf.size();i++) {
        if(m_forecasts.size()>i && fcstBuf.at(i).equal(m_forecasts.at(i))) {
            continue;
        }
        if(i>0 || fcstBuf.at(0).isValid())
            m_pebble->insertPin(fcstBuf.at(i).getPin(i,false,provider));
        m_pebble->insertPin(fcstBuf.at(i).getPin(i,true,provider));
        if(m_forecasts.size()<=i) {
            m_forecasts.append(fcstBuf.at(i));
        } else {
            m_forecasts.replace(i,fcstBuf.at(i));
        }
    }
}

QVariantList WeatherApp::getLocations() const
{
    QVariantList ret;
    foreach(const QString &l, m_locOrder) {
        QStringList city;
        Location loc = m_locations.value(l);
        city.append(loc.name);
        city.append(loc.lat);
        city.append(loc.lng);
        ret.append(city);
    }
    return ret;
}

void WeatherApp::setLocations(const QVariantList &locs, bool push)
{
    qDebug() << "Entering locations" << locs << push;
    if(locs.isEmpty()) {
        m_locOrder.clear();
        m_locations.clear();
        if(push)
            updateConfig();
        return;
    }
    QStringList locOrder;
    foreach(const QVariant &l, locs) {
        Location loc;
        QStringList city = l.toStringList();
        if(city.length()<3) continue;
        loc.name = city.at(0);
        locOrder.append(loc.name);
        if(l == locs.first())
            loc.uuid = WeatherApp::currentLocationUUID;
        else
            loc.uuid = PlatformInterface::idToGuid(loc.name);
        loc.lat = city.at(1);
        loc.lng = city.at(2);
        if(m_locations.contains(loc.name) && m_locations.value(loc.name) == loc)
            continue;
        m_locations.insert(loc.name,loc);
        m_locUpdated = push;
        qDebug() << "New location" << loc.name << loc.lat << loc.lng << loc.uuid;
    }
    if(m_locOrder != locOrder) {
        qDebug() << "Locations updated" << m_locUpdated << "applying changes for" << locOrder;
        m_locOrder = locOrder;
        if(m_locUpdated) {
            // Initiate full refresh
            if(m_provider == nullptr)
                emit requestUpdate();
            else
                m_provider->refreshWeather();
        } else if(push) {
            // Mere reorder, keep the data as is
            for(int i=0;i<m_forecasts.size();i++) {
                Forecast &f = m_forecasts[i];
                f.reorder(locOrder);
                if(f.isValid(i/2==1))
                    m_pebble->insertPin(f.getPin(i/2,i%2==1));
            }
            m_locUpdated = true; // to allow update
            updateConfig();
        }
    }
}

void WeatherApp::updateConfig()
{
    QList<QUuid> cfg;
    if(!m_locUpdated) return;
    foreach(const QString &l, m_locOrder) {
        cfg.append(m_locations.value(l).uuid);
    }
    m_pebble->blobdb()->insert(BlobDB::BlobDBIdAppConfigs,WeatherConfigurations(cfg));
    m_locUpdated = false;
}

void WeatherApp::injectObservation(const QString &l, const Observation &obs, bool force)
{
    if(m_locations.contains(l)) {
        Location loc = m_locations.value(l);
        if(force || !m_obss.contains(loc.uuid) || obs != m_obss.value(loc.uuid)) {
            WeatherObservation wo(loc,obs);
            qDebug() << "Updating observation data for" << l << wo.serialize().toHex();
            m_pebble->blobdb()->insert(BlobDB::BlobDBIdWeatherData,wo);
            m_obss.insert(loc.uuid,obs);
        } else
            qDebug() << "Ignoring observation data for" << l;
    } else {
        qWarning() << "Non-existing location" << l;
    }
}

void WeatherApp::blobdbAckHandler(quint8 db, quint8 cmd, const QByteArray &key, quint8 ack)
{
    switch(db) {
    case BlobDB::BlobDBIdAppConfigs:
    case BlobDB::BlobDBIdWeatherData:
        break;
    default:
        return;
    }
    if(db == BlobDB::BlobDBIdAppConfigs) {
        if(key != appConfigKey) return;
        if(cmd == BlobDB::OperationInsert) {
            if(ack == BlobDB::StatusSuccess) {
                emit locationsChanged();
            } else
                qDebug() << "Failed to save config" << ack;
        }
    } else {
        if(ack != BlobDB::StatusSuccess)
            qDebug() << "Failed to set forecast" << key.toHex() << ack;
    }
    qDebug() << "BlobDB response" << ((ack==BlobDB::StatusSuccess)?"ACK":"NACK") << ((cmd==BlobDB::OperationInsert)?"Insert":"Delete") << ((db==BlobDB::BlobDBIdAppConfigs)?"Locations":"Weather") << key.toHex();
}
