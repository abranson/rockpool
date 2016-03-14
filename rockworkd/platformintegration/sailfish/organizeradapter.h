#ifndef ORGANIZERADAPTER_H
#define ORGANIZERADAPTER_H

#include "libpebble/calendarevent.h"
#include <QObject>
#include <QTimer>
#include <extendedcalendar.h>
#include <extendedstorage.h>

struct CalendarInfo
{
    QString name;
    QString notebookUID;

    CalendarInfo(const QString &name, const QString &notebookUID = QString())
        : name(name), notebookUID(notebookUID) {}
};

class OrganizerAdapter : public QObject, public mKCal::ExtendedStorageObserver
{
    Q_OBJECT
public:
    explicit OrganizerAdapter(QObject *parent = 0);
    ~OrganizerAdapter();
    QList<CalendarEvent> items() const;
    QString normalizeCalendarName(QString name);

public slots:
    void scheduleRefresh();

protected:
    void storageModified(mKCal::ExtendedStorage *storage, const QString &info) Q_DECL_OVERRIDE;
    void storageProgress(mKCal::ExtendedStorage *storage, const QString &info) Q_DECL_OVERRIDE;
    void storageFinished(mKCal::ExtendedStorage *storage, bool error, const QString &info) Q_DECL_OVERRIDE;

private slots:
    void refresh();

signals:
    void itemsChanged(const QList<CalendarEvent> &items);

private:
    QList<CalendarEvent> m_items;
    mKCal::ExtendedCalendar::Ptr _calendar;
    mKCal::ExtendedStorage::Ptr _calendarStorage;
    QTimer *_refreshTimer;
};

#endif // ORGANIZERADAPTER_H
