#include "organizeradapter.h"
#include "libpebble/platforminterface.h"

#include <QDebug>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <extendedcalendar.h>
#include <extendedstorage.h>

#define MANAGER           "eds"
#define MANAGER_FALLBACK  "memory"

OrganizerAdapter::OrganizerAdapter(QObject *parent) : QObject(parent),
    _calendar(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone())),
    _calendarStorage(_calendar->defaultStorage(_calendar)),
    _refreshTimer(new QTimer(this))
{
    _refreshTimer->setSingleShot(true);
    _refreshTimer->setInterval(2000);
    connect(_refreshTimer, &QTimer::timeout, this, &OrganizerAdapter::refresh);

    _calendarStorage->registerObserver(this);
    if (_calendarStorage->open()) {
        refresh();
    } else {
        qWarning() << "Cannot open calendar database";
    }
}

OrganizerAdapter::~OrganizerAdapter()
{
    _calendarStorage->unregisterObserver(this);
}

QString OrganizerAdapter::normalizeCalendarName(QString name)
{
    if (name == "qtn_caln_personal_caln") {
        return tr("Personal");
    }

    return name;
}

void OrganizerAdapter::scheduleRefresh()
{
    setSchedule(2000);
}

void OrganizerAdapter::setSchedule(int interval)
{
    if (!_refreshTimer->isActive()) {
        if(_refreshTimer->interval() != interval)
            _refreshTimer->setInterval(interval);
        _refreshTimer->start();
    }
}

void OrganizerAdapter::reSync(quint32 end)
{
    m_track.clear();
    m_disabled = false;
    m_windowEnd = end;
    setSchedule(10);
}
void OrganizerAdapter::disable()
{
    m_disabled = true;
    _refreshTimer->stop();
}

void OrganizerAdapter::refresh()
{
    if(m_disabled)
        return;
    QStringList todel = m_track.keys();
    QDate startDate = QDate::currentDate().addDays(-m_windowStart);
    QDate endDate = QDate::currentDate().addDays(m_windowEnd);
    _calendarStorage->loadRecurringIncidences();
    _calendarStorage->load(startDate, endDate);

    //TODO: Didn't know about nemo-qml-plugin-calendar, we should probably use this instead of mKCal
    //We have to use it to detect which calendars have been turned off, and
    QSettings nemoSettings("nemo", "nemo-qml-plugin-calendar");

    auto events = _calendar->rawExpandedEvents(startDate, endDate, true, true);
    for (const auto &expanded : events) {
        const QDateTime &start = expanded.first.dtStart;
        KCalCore::Incidence::Ptr incidence = expanded.second;

        QJsonObject calPin,pinLayout,actSnooze,actOpen;
        QJsonArray reminders,actions;
        QStringList headings,paragraphs;

        mKCal::Notebook::Ptr notebook = _calendarStorage->notebook(_calendar->notebook(incidence));
        if (notebook) {
            if (nemoSettings.value("exclude/"+notebook->uid()).toBool()) {
                qDebug() << "Event " << incidence->summary() << " ignored because calendar " << notebook->name() << " excluded. ";
                continue;
            }
            pinLayout.insert("backgroundColor",nemoSettings.value("colors/"+notebook->uid(),"vividcerulean").toString());
            headings.append("Calendar");
            paragraphs.append(normalizeCalendarName(notebook->name()));
        }

        if (incidence->recurs()) {
            calPin.insert("id",incidence->uid() + QString::number(start.toUTC().toTime_t()));
            calPin.insert("guid",PlatformInterface::idToGuid(calPin.value("id").toString()).toString().mid(1,36));
            pinLayout.insert("displayRecurring",QString("recurring"));
        } else {
            calPin.insert("id",incidence->uid());
            calPin.insert("guid",incidence->uid());
        }
        if(m_track.contains(calPin.value("guid").toString())) {
            todel.removeAll(calPin.value("guid").toString());
            if(m_track.value(calPin.value("guid").toString()).toUtc() == incidence->lastModified().toUtc())
                continue;
        }
        m_track.insert(calPin.value("guid").toString(),incidence->lastModified());
        calPin.insert("createTime",incidence->created().toUtc().toString());
        calPin.insert("updateTime",incidence->lastModified().toUtc().toString());
        calPin.insert("time",start.toUTC().toString(Qt::ISODate));
        calPin.insert("dataSource",QString("calendarEvent:%1").arg(PlatformInterface::SysID));
        if(incidence->hasDuration())
            calPin.insert("duration",incidence->duration().asSeconds() / 60);
        if(incidence->allDay()) {
            calPin.insert("allDay",true);
            qDebug() << "Preparing all-day event, the reported duration is (sec)" << incidence->duration().asSeconds();
        }
        pinLayout.insert("type",QString("calendarPin"));
        pinLayout.insert("title",incidence->summary());
        if(!incidence->description().isEmpty())
            pinLayout.insert("body",incidence->description());

        if (!incidence->location().isEmpty()) {
            pinLayout.insert("locationName",incidence->location());
        }

        QStringList attendees;
        foreach (const QSharedPointer<KCalCore::Attendee> attendee, incidence->attendees()) {
            attendees.append(attendee->fullName());
        }
        if(!incidence->comments().isEmpty()) {
            headings.append("Comments");
            paragraphs.append(incidence->comments().join(";"));
        }
        if(!attendees.isEmpty()) {
            headings.append("Attendees");
            paragraphs.append(attendees.join(", "));
        }
        if(!headings.isEmpty()) {
            pinLayout.insert("headings",QJsonArray::fromStringList(headings));
            pinLayout.insert("paragraphs",QJsonArray::fromStringList(paragraphs));
        }
        calPin.insert("layout",pinLayout);

        foreach (const QSharedPointer<KCalCore::Alarm> alarm, incidence->alarms()) {
            if (alarm->enabled()) {
                QDateTime reminderTime = alarm->nextTime(KDateTime::currentDateTime(KDateTime::Spec::LocalZone()), false).dateTime();
                qDebug() << "Alarm enabled for " << incidence->summary() << " at " << reminderTime;
                QJsonObject rem,rLy;
                rem.insert("time",reminderTime.toString());
                rLy.insert("type",QString("genericReminder"));
                rLy.insert("title",pinLayout.value("title"));
                if(pinLayout.contains("locationName"))
                    rLy.insert("locationName",pinLayout.value("locationName"));
                rLy.insert("tinyIcon",QString("system://images/NOTIFICATION_REMINDER"));
                rem.insert("layout",rLy);
                reminders.append(rem);
                if(reminders.count()==3)
                    break;
            }
        }
        if(reminders.count()>0)
            calPin.insert("reminders",reminders);

        actOpen.insert("type",QString("open"));
        actOpen.insert("title",QString("Open"));
        actions.append(actOpen);
        actSnooze.insert("type",QString("snooze"));
        actSnooze.insert("title",QString("Snooze"));
        actions.append(actSnooze);
        calPin.insert("actions",actions);

        emit newTimelinePin(calPin);
    }

    if(!todel.isEmpty()) {
        foreach(const QString &key,todel) {
            m_track.remove(key);
            emit delTimelinePin(key);
        }
    }
}

void OrganizerAdapter::storageModified(mKCal::ExtendedStorage *storage, const QString &info)
{
    Q_UNUSED(storage);
    qDebug() << "Storage modified:" << info;
    scheduleRefresh();
}

void OrganizerAdapter::storageProgress(mKCal::ExtendedStorage *storage, const QString &info)
{
    Q_UNUSED(storage);
    Q_UNUSED(info);
    // Nothing to do
}

void OrganizerAdapter::storageFinished(mKCal::ExtendedStorage *storage, bool error, const QString &info)
{
    Q_UNUSED(storage);
    Q_UNUSED(error);
    Q_UNUSED(info);
    // Nothing to do
}
