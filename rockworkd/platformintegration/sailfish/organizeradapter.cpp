#include "organizeradapter.h"
#include "libpebble/platforminterface.h"

#include <QDebug>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <KCalendarCore/OccurrenceIterator>

#include <libintl.h>

#define MANAGER           "eds"
#define MANAGER_FALLBACK  "memory"

OrganizerAdapter::OrganizerAdapter(QObject *parent) : QObject(parent),
    _calendar(new mKCal::ExtendedCalendar(QTimeZone::systemTimeZone())),
    _calendarStorage(_calendar->defaultStorage(_calendar)),
    _refreshTimer(new QTimer(this))
{
    _refreshTimer->setSingleShot(true);
    _refreshTimer->setInterval(2000);
    connect(_refreshTimer, &QTimer::timeout, this, &OrganizerAdapter::refresh);

    _calendarStorage->registerObserver(this);
    if (_calendarStorage->open()) {
        refresh();
        startTimer(24 * 60 * 60 * 1000); // I didn't find any other periodic (daily) refresh
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

void OrganizerAdapter::reSync(qint32 end)
{
    m_track.clear();
    m_disabled = false;
    m_windowEnd = end;
    setSchedule(10);
    qDebug() << "Resyncing organizer for next" << m_windowEnd << "days";
}
void OrganizerAdapter::disable()
{
    m_disabled = true;
    _refreshTimer->stop();
}

void OrganizerAdapter::timerEvent(QTimerEvent *event)
{
    if(event)
        refresh();
}

void OrganizerAdapter::refresh()
{
    if(m_disabled)
        return;
    QStringList todel = m_track.keys();
    QDateTime startDate = QDateTime::currentDateTime().addDays(m_windowStart);
    QDateTime endDate = QDateTime::currentDateTime().addDays(m_windowEnd);
    _calendarStorage->loadRecurringIncidences();
    _calendarStorage->load(startDate.date(), endDate.date());
    qDebug() << "Refreshing organizer from" << startDate.toString() << "to" << endDate.toString();

    QSettings nemoSettings("nemo", "nemo-qml-plugin-calendar");

    KCalendarCore::OccurrenceIterator it(*_calendar, startDate, endDate);
    while (it.hasNext()) {
        it.next();
        if (!_calendar->isVisible(it.incidence())
                        || it.incidence()->type() != KCalendarCore::IncidenceBase::TypeEvent) {
            continue;
        }
        const QDateTime start = it.occurrenceStartDate();
        
        QJsonObject calPin,pinLayout,actSnooze,actOpen;
        QJsonArray reminders,actions;
        QStringList headings,paragraphs;

        mKCal::Notebook::Ptr notebook = _calendarStorage->notebook(_calendar->notebook(it.incidence()));
        if (notebook) {
            if (nemoSettings.value("exclude/"+notebook->uid()).toBool()) {
                qDebug() << "Event " << it.incidence()->summary() << " ignored because calendar " << notebook->name() << " excluded. ";
                continue;
            }
            pinLayout.insert("backgroundColor",nemoSettings.value("colors/"+notebook->uid(),"vividcerulean").toString());
            headings.append("Calendar");
            paragraphs.append(normalizeCalendarName(notebook->name()));
        }

        if (it.incidence()->recurs()) {
            calPin.insert("id",it.incidence()->uid() + QString::number(start.toUTC().toTime_t()));
            calPin.insert("guid",PlatformInterface::idToGuid(calPin.value("id").toString()).toString().mid(1,36));
            pinLayout.insert("displayRecurring",QString("recurring"));
        } else {
            calPin.insert("id",it.incidence()->uid());
            calPin.insert("guid",it.incidence()->uid());
        }
        if(m_track.contains(calPin.value("guid").toString())) {
            todel.removeAll(calPin.value("guid").toString());
            if(m_track.value(calPin.value("guid").toString()).toUTC() == it.incidence()->lastModified().toUTC())
                continue;
        }
        m_track.insert(calPin.value("guid").toString(),it.incidence()->lastModified());
        calPin.insert("createTime",it.incidence()->created().toUTC().toString());
        calPin.insert("updateTime",it.incidence()->lastModified().toUTC().toString());
        calPin.insert("time",start.toUTC().toString(Qt::ISODate));
        calPin.insert("dataSource",QString("calendarEvent:%1").arg(PlatformInterface::SysID));
        if(it.incidence()->hasDuration())
            calPin.insert("duration",it.incidence()->duration().asSeconds() / 60);
        if(it.incidence()->allDay()) {
            calPin.insert("allDay",true);
            qDebug() << "Preparing all-day event, the reported duration is (sec)" << it.incidence()->duration().asSeconds();
        }
        pinLayout.insert("type",QString("calendarPin"));
        pinLayout.insert("title",it.incidence()->summary());
        if(!it.incidence()->description().isEmpty())
            pinLayout.insert("body",it.incidence()->description());

        if (!it.incidence()->location().isEmpty()) {
            pinLayout.insert("locationName",it.incidence()->location());
        }

        QStringList attendees;
        foreach (const KCalendarCore::Attendee attendee, it.incidence()->attendees()) {
            attendees.append(attendee.fullName());
        }
        if(!it.incidence()->comments().isEmpty()) {
            headings.append("Comments");
            paragraphs.append(it.incidence()->comments().join(";"));
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

        foreach (const QSharedPointer<KCalendarCore::Alarm> alarm, it.incidence()->alarms()) {
            if (alarm->enabled()) {
                QString reminderTime;
                if(alarm->hasStartOffset()) {
                    reminderTime = start.toUTC().addSecs(alarm->startOffset().asSeconds()).toString(Qt::ISODate);
                } else if(alarm->hasTime()) {
                    reminderTime = it.incidence()->recurs() ?
                        alarm->nextTime(QDateTime::currentDateTime(), false).toUTC().toString()
                        : alarm->time().toUTC().toString();
                } else {
                    qDebug() << "Skipping reminder, has no time";
                    continue;
                }
                qDebug() << "Alarm enabled for " << it.incidence()->summary() << " at " << reminderTime;
                QJsonObject rem,rLy;
                rem.insert("time",reminderTime);
                rLy.insert("type",QString("genericReminder"));
                rLy.insert("title",(alarm->type() == KCalendarCore::Alarm::Display) ? alarm->text() : pinLayout.value("title"));
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
