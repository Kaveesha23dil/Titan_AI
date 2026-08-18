#ifndef TITANAI_CALENDAR_EVENT_HPP
#define TITANAI_CALENDAR_EVENT_HPP

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

struct CalendarEvent {
    QString uid;
    QString summary;
    QString description;
    QString location;
    QDateTime start;
    QDateTime end;
    bool allDay{false};
    QString recurrenceRule;
    QString sourceFile;

    [[nodiscard]] bool isAllDay() const { return allDay; }

    [[nodiscard]] bool isToday() const {
        return start.date() == QDate::currentDate();
    }

    [[nodiscard]] bool isUpcoming() const {
        return start > QDateTime::currentDateTime();
    }

    [[nodiscard]] bool isActive() const {
        const QDateTime now = QDateTime::currentDateTime();
        return start <= now && end >= now;
    }

    [[nodiscard]] bool isPast() const {
        return end < QDateTime::currentDateTime();
    }

    [[nodiscard]] int minutesUntilStart() const {
        return static_cast<int>(QDateTime::currentDateTime().secsTo(start) / 60);
    }

    [[nodiscard]] int minutesSinceEnd() const {
        return static_cast<int>(end.secsTo(QDateTime::currentDateTime()) / 60);
    }

    [[nodiscard]] QString formatTimeRange() const {
        if (allDay) {
            if (start.date() == end.date()) {
                return start.date().toString(QStringLiteral("dddd, MMMM d, yyyy"));
            }
            return QStringLiteral("%1 to %2")
                .arg(start.date().toString(QStringLiteral("MMM d")))
                .arg(end.date().toString(QStringLiteral("MMM d, yyyy")));
        }

        if (start.date() == end.date()) {
            return QStringLiteral("%1, %2 - %3")
                .arg(start.date().toString(QStringLiteral("dddd, MMM d")))
                .arg(start.time().toString(QStringLiteral("h:mm AP")))
                .arg(end.time().toString(QStringLiteral("h:mm AP")));
        }

        return QStringLiteral("%1 - %2")
            .arg(start.toString(QStringLiteral("MMM d, h:mm AP")))
            .arg(end.toString(QStringLiteral("MMM d, h:mm AP")));
    }

    [[nodiscard]] QString timeUntilString() const {
        const int mins = minutesUntilStart();
        if (mins < 0) {
            return QStringLiteral("happening now");
        } else if (mins < 60) {
            return QStringLiteral("in %1 min").arg(mins);
        } else {
            const int hours = mins / 60;
            const int remainMins = mins % 60;
            if (remainMins == 0) {
                return QStringLiteral("in %1 hr").arg(hours);
            }
            return QStringLiteral("in %1 hr %2 min").arg(hours).arg(remainMins);
        }
    }

    [[nodiscard]] QJsonObject toJson() const {
        QJsonObject obj;
        obj[QStringLiteral("uid")] = uid;
        obj[QStringLiteral("summary")] = summary;
        obj[QStringLiteral("description")] = description;
        obj[QStringLiteral("location")] = location;
        obj[QStringLiteral("start")] = start.toString(Qt::ISODate);
        obj[QStringLiteral("end")] = end.toString(Qt::ISODate);
        obj[QStringLiteral("allDay")] = allDay;
        obj[QStringLiteral("recurrence")] = recurrenceRule;
        obj[QStringLiteral("source")] = sourceFile;
        return obj;
    }

    [[nodiscard]] static CalendarEvent fromJson(const QJsonObject &obj) {
        CalendarEvent event;
        event.uid = obj[QStringLiteral("uid")].toString();
        event.summary = obj[QStringLiteral("summary")].toString();
        event.description = obj[QStringLiteral("description")].toString();
        event.location = obj[QStringLiteral("location")].toString();
        event.start = QDateTime::fromString(obj[QStringLiteral("start")].toString(), Qt::ISODate);
        event.end = QDateTime::fromString(obj[QStringLiteral("end")].toString(), Qt::ISODate);
        event.allDay = obj[QStringLiteral("allDay")].toBool();
        event.recurrenceRule = obj[QStringLiteral("recurrence")].toString();
        event.sourceFile = obj[QStringLiteral("source")].toString();
        return event;
    }

    [[nodiscard]] bool operator==(const CalendarEvent &other) const {
        return uid == other.uid && start == other.start;
    }

    [[nodiscard]] bool operator<(const CalendarEvent &other) const {
        return start < other.start;
    }
};

#endif // TITANAI_CALENDAR_EVENT_HPP
