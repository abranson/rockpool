#ifndef ORGANIZERADAPTER_H
#define ORGANIZERADAPTER_H

#include "libpebble/calendarevent.h"

#include <QObject>

#include <QOrganizerManager>
#include <QOrganizerAbstractRequest>
#include <QOrganizerEvent>

QTORGANIZER_USE_NAMESPACE

class OrganizerAdapter : public QObject
{
    Q_OBJECT
public:
    explicit OrganizerAdapter(QObject *parent = 0);

    QList<CalendarEvent> items() const;

public slots:
    void refresh();

signals:
    void itemsChanged(const QList<CalendarEvent> &items);

private:
    QOrganizerManager *m_manager;
    QList<CalendarEvent> m_items;
};

#endif // ORGANIZERADAPTER_H
