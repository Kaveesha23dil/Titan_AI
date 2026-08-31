#include "calendar/calendar_manager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDateTime>

CalendarManager::CalendarManager(QObject *parent)
    : QObject(parent)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_dataPath = dataDir + QStringLiteral("/calendars.json");

    connect(&m_refreshTimer, &QTimer::timeout, this, &CalendarManager::onRefreshTimer);
}

void CalendarManager::addCalendarFile(const QString &filePath)
{
    if (filePath.isEmpty() || m_calendarFiles.contains(filePath)) {
        return;
    }

    if (!QFile::exists(filePath)) {
        emit calendarError(QStringLiteral("File not found: %1").arg(filePath));
        return;
    }

    m_calendarFiles.append(filePath);
    loadIcsFile(filePath);
    saveCalendarConfig();
    emit calendarAdded(filePath);
    emit eventsUpdated();
}

void CalendarManager::removeCalendarFile(const QString &filePath)
{
    if (!m_calendarFiles.contains(filePath)) {
        return;
    }

    m_calendarFiles.removeAll(filePath);

    m_events.erase(
        std::remove_if(m_events.begin(), m_events.end(),
                        [&filePath](const CalendarEvent &e) { return e.sourceFile == filePath; }),
        m_events.end());

    saveCalendarConfig();
    emit calendarRemoved(filePath);
    emit eventsUpdated();
}

QStringList CalendarManager::calendarFiles() const
{
    return m_calendarFiles;
}

void CalendarManager::loadAllCalendars()
{
    loadCalendarConfig();
    m_events.clear();

    for (const QString &file : m_calendarFiles) {
        if (QFile::exists(file)) {
            loadIcsFile(file);
        }
    }

    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start(m_refreshMinutes * 60 * 1000);
    }

    emit eventsUpdated();
}

void CalendarManager::refresh()
{
    m_events.clear();

    for (const QString &file : m_calendarFiles) {
        if (QFile::exists(file)) {
            loadIcsFile(file);
        } else {
            emit calendarError(QStringLiteral("Calendar file not found: %1").arg(file));
        }
    }

    emit eventsUpdated();
}

QList<CalendarEvent> CalendarManager::allEvents() const
{
    return m_events;
}

QList<CalendarEvent> CalendarManager::getTodaysEvents() const
{
    QList<CalendarEvent> today;
    const QDate currentDate = QDate::currentDate();

    for (const CalendarEvent &event : m_events) {
        if (event.start.date() == currentDate ||
            (event.allDay && event.start.date() <= currentDate && event.end.date() >= currentDate)) {
            today.append(event);
        }
    }

    std::sort(today.begin(), today.end());
    return today;
}

QList<CalendarEvent> CalendarManager::getUpcomingEvents(int hoursAhead) const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime cutoff = now.addSecs(hoursAhead * 3600);
    QList<CalendarEvent> upcoming;

    for (const CalendarEvent &event : m_events) {
        if (event.start >= now && event.start <= cutoff) {
            upcoming.append(event);
        }
    }

    std::sort(upcoming.begin(), upcoming.end());
    return upcoming;
}

QList<CalendarEvent> CalendarManager::getActiveEvents() const
{
    QList<CalendarEvent> active;
    for (const CalendarEvent &event : m_events) {
        if (event.isActive()) {
            active.append(event);
        }
    }
    return active;
}

CalendarEvent CalendarManager::getNextEvent() const
{
    const QDateTime now = QDateTime::currentDateTime();
    CalendarEvent next;
    bool found = false;

    for (const CalendarEvent &event : m_events) {
        if (event.start > now && (!found || event.start < next.start)) {
            next = event;
            found = true;
        }
    }

    return next;
}

QString CalendarManager::formatEventList(const QList<CalendarEvent> &events) const
{
    if (events.isEmpty()) {
        return QStringLiteral("No events found.");
    }

    QString result;
    for (int i = 0; i < events.size(); ++i) {
        const CalendarEvent &e = events.at(i);
        result += QStringLiteral("**%1. %2**\n").arg(i + 1).arg(e.summary);
        result += QStringLiteral("   Time: %1\n").arg(e.formatTimeRange());
        if (!e.location.isEmpty()) {
            result += QStringLiteral("   Location: %1\n").arg(e.location);
        }
        if (!e.description.isEmpty()) {
            result += QStringLiteral("   Details: %1\n").arg(e.description);
        }
        if (e.isUpcoming()) {
            result += QStringLiteral("   Status: %1\n").arg(e.timeUntilString());
        }
        result += QStringLiteral("\n");
    }

    return result;
}

QString CalendarManager::formatUpcomingSummary() const
{
    const QList<CalendarEvent> upcoming = getUpcomingEvents(24);

    if (upcoming.isEmpty()) {
        return QStringLiteral("No upcoming events in the next 24 hours.");
    }

    QString result = QStringLiteral("**Upcoming Events (next 24 hours):**\n\n");

    for (int i = 0; i < upcoming.size() && i < 5; ++i) {
        const CalendarEvent &e = upcoming.at(i);
        result += QStringLiteral("- **%1** %2\n")
                      .arg(e.summary, e.timeUntilString());
        if (!e.location.isEmpty()) {
            result += QStringLiteral("  Location: %1\n").arg(e.location);
        }
    }

    if (upcoming.size() > 5) {
        result += QStringLiteral("\n... and %1 more events.\n").arg(upcoming.size() - 5);
    }

    return result;
}

void CalendarManager::setAutoRefresh(bool enabled)
{
    if (enabled) {
        if (!m_refreshTimer.isActive()) {
            m_refreshTimer.start(m_refreshMinutes * 60 * 1000);
        }
    } else {
        m_refreshTimer.stop();
    }
}

void CalendarManager::setRefreshInterval(int minutes)
{
    m_refreshMinutes = qMax(1, minutes);
    if (m_refreshTimer.isActive()) {
        m_refreshTimer.setInterval(m_refreshMinutes * 60 * 1000);
    }
}

void CalendarManager::onRefreshTimer()
{
    refresh();
}

void CalendarManager::loadIcsFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit calendarError(QStringLiteral("Failed to open: %1").arg(filePath));
        return;
    }

    QByteArray content = file.readAll();
    file.close();

    const QList<CalendarEvent> events = parseIcsContent(content, filePath);
    for (const CalendarEvent &event : events) {
        if (!isEventDuplicate(event)) {
            m_events.append(event);
        }
    }
}

QList<CalendarEvent> CalendarManager::parseIcsContent(const QByteArray &content,
                                                       const QString &sourceFile) const
{
    QList<CalendarEvent> events;
    QString text = QString::fromUtf8(content);

    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\r"), QStringLiteral("\n"));

    // RFC 5545 line unfolding: a line that begins with a space or tab is a
    // continuation of the previous logical line (long property values are
    // folded this way). Without unfolding, long SUMMARY/DESCRIPTION values
    // would be silently truncated.
    {
        const QStringList rawLines = text.split(QLatin1Char('\n'));
        QStringList unfolded;
        unfolded.reserve(rawLines.size());
        for (const QString &line : rawLines) {
            if (!line.isEmpty() && (line.at(0) == QLatin1Char(' ') || line.at(0) == QLatin1Char('\t')) &&
                !unfolded.isEmpty()) {
                unfolded.last().append(line.mid(1));
            } else {
                unfolded.append(line);
            }
        }
        text = unfolded.join(QLatin1Char('\n'));
    }

    QRegularExpression veventRegex(
        QStringLiteral("BEGIN:VEVENT\\s+(.*?)END:VEVENT"),
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = veventRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        const QString block = match.captured(1);
        CalendarEvent event = parseVevent(block, sourceFile);
        if (!event.summary.isEmpty() && event.start.isValid()) {
            events.append(event);
            generateOccurrences(event, QDateTime::currentDateTime().addDays(30), events);
        }
    }

    return events;
}

CalendarEvent CalendarManager::parseVevent(const QString &veventBlock,
                                            const QString &sourceFile) const
{
    CalendarEvent event;
    event.sourceFile = sourceFile;

    auto extractField = [&veventBlock](const QString &fieldName) -> QString {
        QRegularExpression re(QStringLiteral("^%1(?:;[^:]*)?:(.+)$")
                                  .arg(fieldName),
                              QRegularExpression::MultilineOption);
        QRegularExpressionMatch match = re.match(veventBlock);
        if (match.hasMatch()) {
            return match.captured(1).trimmed();
        }
        return QString();
    };

    event.uid = extractField(QStringLiteral("UID"));
    event.summary = extractField(QStringLiteral("SUMMARY"));
    event.description = extractField(QStringLiteral("DESCRIPTION"));
    event.description.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    event.description.replace(QStringLiteral("\\,"), QStringLiteral(","));
    event.location = extractField(QStringLiteral("LOCATION"));
    event.location.replace(QStringLiteral("\\,"), QStringLiteral(","));
    event.recurrenceRule = extractField(QStringLiteral("RRULE"));

    const QString dtstart = extractField(QStringLiteral("DTSTART"));
    const QString dtend = extractField(QStringLiteral("DTEND"));

    event.start = parseIcsDateTime(dtstart);
    event.end = parseIcsDateTime(dtend);

    if (dtstart.contains(QStringLiteral("VALUE=DATE")) || dtstart.length() == 8) {
        event.allDay = true;
    }

    if (!event.end.isValid() && event.start.isValid()) {
        event.end = event.start.addSecs(3600);
    }

    if (event.uid.isEmpty()) {
        event.uid = QStringLiteral("%1_%2")
                        .arg(event.summary)
                        .arg(event.start.toMSecsSinceEpoch());
    }

    return event;
}

QDateTime CalendarManager::parseIcsDateTime(const QString &dtString) const
{
    if (dtString.isEmpty()) {
        return QDateTime();
    }

    QString clean = dtString.trimmed();

    if (clean.endsWith(QLatin1Char('Z'))) {
        clean.chop(1);
    }

    static const QStringList formats = {
        QStringLiteral("yyyyMMdd'T'HHmmss"),
        QStringLiteral("yyyyMMddTHHmmss"),
        QStringLiteral("yyyyMMdd"),
    };

    for (const QString &fmt : formats) {
        QDateTime dt = QDateTime::fromString(clean, fmt);
        if (dt.isValid()) {
            if (dtString.endsWith(QLatin1Char('Z'))) {
                dt.setTimeZone(QTimeZone::utc());
            }
            return dt;
        }
    }

    return QDateTime::fromString(clean, Qt::ISODate);
}

void CalendarManager::generateOccurrences(const CalendarEvent &event,
                                           const QDateTime &rangeEnd,
                                           QList<CalendarEvent> &output) const
{
    if (event.recurrenceRule.isEmpty()) {
        return;
    }

    QRegularExpression freqRe(QStringLiteral("FREQ=(\\w+)"));
    QRegularExpressionMatch freqMatch = freqRe.match(event.recurrenceRule);
    if (!freqMatch.hasMatch()) {
        return;
    }

    const QString freq = freqMatch.captured(1);

    QRegularExpression countRe(QStringLiteral("COUNT=(\\d+)"));
    QRegularExpressionMatch countMatch = countRe.match(event.recurrenceRule);
    const int maxOccurrences = countMatch.hasMatch() ? countMatch.captured(1).toInt() : 90;

    QRegularExpression untilRe(QStringLiteral("UNTIL=(\\d{8}T\\d{6})"));
    QRegularExpressionMatch untilMatch = untilRe.match(event.recurrenceRule);
    const QDateTime untilDate = untilMatch.hasMatch()
                                    ? parseIcsDateTime(untilMatch.captured(1))
                                    : rangeEnd;

    const QDateTime effectiveEnd = untilDate < rangeEnd ? untilDate : rangeEnd;

    QDateTime currentStart = event.start;
    const qint64 duration = event.start.secsTo(event.end);
    int count = 0;

    while (currentStart < effectiveEnd && count < maxOccurrences) {
        currentStart = currentStart.addDays(1);

        if (freq == QStringLiteral("WEEKLY")) {
            currentStart = event.start.addDays(7 * (count + 1));
        } else if (freq == QStringLiteral("MONTHLY")) {
            currentStart = event.start.addMonths(count + 1);
        } else if (freq == QStringLiteral("YEARLY")) {
            currentStart = event.start.addYears(count + 1);
        }

        if (currentStart > effectiveEnd) {
            break;
        }

        CalendarEvent occurrence = event;
        occurrence.start = currentStart;
        occurrence.end = currentStart.addSecs(duration);
        occurrence.uid = QStringLiteral("%1_rec_%2").arg(event.uid).arg(count);
        occurrence.recurrenceRule.clear();

        output.append(occurrence);
        ++count;
    }
}

void CalendarManager::saveCalendarConfig()
{
    QJsonObject config;
    QJsonArray filesArray;
    for (const QString &file : m_calendarFiles) {
        filesArray.append(file);
    }
    config[QStringLiteral("calendarFiles")] = filesArray;
    config[QStringLiteral("refreshMinutes")] = m_refreshMinutes;

    QFile file(m_dataPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
        file.close();
    }
}

void CalendarManager::loadCalendarConfig()
{
    QFile file(m_dataPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject config = doc.object();
    const QJsonArray filesArray = config[QStringLiteral("calendarFiles")].toArray();

    m_calendarFiles.clear();
    for (const QJsonValue &val : filesArray) {
        m_calendarFiles.append(val.toString());
    }

    m_refreshMinutes = config[QStringLiteral("refreshMinutes")].toInt(15);
}

bool CalendarManager::isEventDuplicate(const CalendarEvent &event) const
{
    for (const CalendarEvent &existing : m_events) {
        if (existing.uid == event.uid && existing.start == event.start) {
            return true;
        }
    }
    return false;
}
