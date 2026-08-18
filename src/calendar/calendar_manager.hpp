#ifndef TITANAI_CALENDAR_MANAGER_HPP
#define TITANAI_CALENDAR_MANAGER_HPP

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>

#include "calendar/calendar_event.hpp"

class CalendarManager : public QObject {
    Q_OBJECT

public:
    explicit CalendarManager(QObject *parent = nullptr);
    ~CalendarManager() override = default;

    void addCalendarFile(const QString &filePath);
    void removeCalendarFile(const QString &filePath);
    [[nodiscard]] QStringList calendarFiles() const;
    void loadAllCalendars();
    void refresh();

    [[nodiscard]] QList<CalendarEvent> allEvents() const;
    [[nodiscard]] QList<CalendarEvent> getTodaysEvents() const;
    [[nodiscard]] QList<CalendarEvent> getUpcomingEvents(int hoursAhead = 24) const;
    [[nodiscard]] QList<CalendarEvent> getActiveEvents() const;
    [[nodiscard]] CalendarEvent getNextEvent() const;
    [[nodiscard]] QString formatEventList(const QList<CalendarEvent> &events) const;
    [[nodiscard]] QString formatUpcomingSummary() const;

    void setAutoRefresh(bool enabled);
    void setRefreshInterval(int minutes);

signals:
    void eventsUpdated();
    void upcomingEventDetected(const CalendarEvent &event);
    void calendarAdded(const QString &filePath);
    void calendarRemoved(const QString &filePath);
    void calendarError(const QString &error);

private slots:
    void onRefreshTimer();

private:
    void loadIcsFile(const QString &filePath);
    QList<CalendarEvent> parseIcsContent(const QByteArray &content, const QString &sourceFile) const;
    QList<CalendarEvent> expandRecurrence(const CalendarEvent &event, const QDateTime &rangeEnd) const;
    CalendarEvent parseVevent(const QString &veventBlock, const QString &sourceFile) const;
    QDateTime parseIcsDateTime(const QString &dtString) const;
    void generateOccurrences(const CalendarEvent &event, const QDateTime &rangeEnd,
                             QList<CalendarEvent> &output) const;
    void saveCalendarConfig();
    void loadCalendarConfig();
    bool isEventDuplicate(const CalendarEvent &event) const;

    QList<CalendarEvent> m_events;
    QStringList m_calendarFiles;
    QTimer m_refreshTimer;
    int m_refreshMinutes{15};
    QString m_dataPath;
};

#endif // TITANAI_CALENDAR_MANAGER_HPP
