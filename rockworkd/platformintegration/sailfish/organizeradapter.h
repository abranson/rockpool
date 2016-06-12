#ifndef ORGANIZERADAPTER_H
#define ORGANIZERADAPTER_H

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

public slots:
    void scheduleRefresh();
    void reSync(quint32 end);
    void disable();

protected:
    void storageModified(mKCal::ExtendedStorage *storage, const QString &info) Q_DECL_OVERRIDE;
    void storageProgress(mKCal::ExtendedStorage *storage, const QString &info) Q_DECL_OVERRIDE;
    void storageFinished(mKCal::ExtendedStorage *storage, bool error, const QString &info) Q_DECL_OVERRIDE;

private slots:
    void refresh();

signals:
    void newTimelinePin(const QJsonObject &pin);
    void delTimelinePin(const QString &guid);

private:
    QString normalizeCalendarName(QString name);
    void setSchedule(int interval);
    QHash<QString,KDateTime> m_track;
    bool m_disabled = false;
    mKCal::ExtendedCalendar::Ptr _calendar;
    mKCal::ExtendedStorage::Ptr _calendarStorage;
    QTimer *_refreshTimer;
    quint32 m_windowStart = 2;
    quint32 m_windowEnd = 7;
};

#endif // ORGANIZERADAPTER_H
