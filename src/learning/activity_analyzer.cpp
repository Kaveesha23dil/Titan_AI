#include "learning/activity_analyzer.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <algorithm>

ActivityAnalyzer::ActivityAnalyzer(QObject *parent)
    : QObject(parent)
{
}

void ActivityAnalyzer::analyze(const QList<TaskEntry> &entries)
{
    if (entries.isEmpty()) {
        return;
    }

    buildCategorySummary(entries);
    buildHourlyBreakdown(entries);
    detectTimePatterns(entries);
    detectFrequentApps(entries);

    emit analysisUpdated();
}

void ActivityAnalyzer::clearAnalysis()
{
    m_patterns.clear();
    m_categoryUsage.clear();
    m_hourlyBreakdown.clear();
    m_weekdayApps.clear();
    m_appFrequency.clear();
}

QList<ActivityPattern> ActivityAnalyzer::detectedPatterns() const
{
    return m_patterns;
}

QMap<QString, int> ActivityAnalyzer::categoryUsageSummary() const
{
    return m_categoryUsage;
}

QList<TimeSlotUsage> ActivityAnalyzer::hourlyBreakdown() const
{
    return m_hourlyBreakdown;
}

QList<QString> ActivityAnalyzer::mostUsedApplications(int topN) const
{
    QList<QString> result;
    const int limit = qMin(topN, m_appFrequency.size());
    for (int i = 0; i < limit; ++i) {
        result.append(m_appFrequency.at(i).first);
    }
    return result;
}

QMap<int, QList<QString>> ActivityAnalyzer::weekdayActivity() const
{
    return m_weekdayApps;
}

void ActivityAnalyzer::buildCategorySummary(const QList<TaskEntry> &entries)
{
    m_categoryUsage.clear();

    for (const TaskEntry &entry : entries) {
        QString cat = entry.category;
        if (cat.isEmpty()) {
            cat = QStringLiteral("other");
        }
        m_categoryUsage[cat] += 1;
    }
}

void ActivityAnalyzer::buildHourlyBreakdown(const QList<TaskEntry> &entries)
{
    m_hourlyBreakdown.resize(24);
    for (int i = 0; i < 24; ++i) {
        m_hourlyBreakdown[i].hour = i;
    }

    QMap<QString, int> hourlyTotals[24];

    for (const TaskEntry &entry : entries) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(entry.timestampMs);
        int hour = dt.time().hour();

        QString cat = entry.category;
        if (cat.isEmpty()) {
            cat = QStringLiteral("other");
        }

        m_hourlyBreakdown[hour].categoryFrequency[cat] += 1;
    }

    for (int i = 0; i < 24; ++i) {
        TimeSlotUsage &slot = m_hourlyBreakdown[i];
        int maxFreq = 0;
        for (auto it = slot.categoryFrequency.constBegin();
             it != slot.categoryFrequency.constEnd(); ++it) {
            if (it.value() > maxFreq) {
                maxFreq = it.value();
                slot.dominantCategory = it.key();
            }
        }
    }
}

void ActivityAnalyzer::detectTimePatterns(const QList<TaskEntry> &entries)
{
    m_patterns.clear();
    m_weekdayApps.clear();

    QMap<QString, QMap<int, int>> processByHour;
    QMap<QString, QMap<int, int>> processByDay;

    for (const TaskEntry &entry : entries) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(entry.timestampMs);
        int hour = dt.time().hour();
        int day = dt.date().dayOfWeek();

        processByHour[entry.processName][hour] += 1;
        processByDay[entry.processName][day] += 1;

        if (!m_weekdayApps[day].contains(entry.processName)) {
            m_weekdayApps[day].append(entry.processName);
        }
    }

    for (auto it = processByHour.constBegin(); it != processByHour.constEnd(); ++it) {
        const QString &process = it.key();
        const QMap<int, int> &hours = it.value();

        int maxHour = -1;
        int maxCount = 0;
        for (auto hIt = hours.constBegin(); hIt != hours.constEnd(); ++hIt) {
            if (hIt.value() > maxCount) {
                maxCount = hIt.value();
                maxHour = hIt.key();
            }
        }

        if (maxCount >= 3 && maxHour >= 0) {
            ActivityPattern pattern;
            pattern.processName = process;
            pattern.hourOfDay = maxHour;
            pattern.frequency = maxCount;
            pattern.description = QStringLiteral(
                "%1 is frequently used around %2:00")
                .arg(process)
                .arg(maxHour, 2, 10, QLatin1Char('0'));
            m_patterns.append(pattern);
        }
    }

    std::sort(m_patterns.begin(), m_patterns.end(),
              [](const ActivityPattern &a, const ActivityPattern &b) {
                  return a.frequency > b.frequency;
              });
}

void ActivityAnalyzer::detectFrequentApps(const QList<TaskEntry> &entries)
{
    QMap<QString, int> appCount;
    for (const TaskEntry &entry : entries) {
        appCount[entry.processName] += 1;
    }

    m_appFrequency.clear();
    for (auto it = appCount.constBegin(); it != appCount.constEnd(); ++it) {
        m_appFrequency.append({it.key(), it.value()});
    }

    std::sort(m_appFrequency.begin(), m_appFrequency.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.second > b.second;
              });
}

QJsonObject ActivityAnalyzer::exportAnalysis() const
{
    QJsonObject root;

    QJsonObject categories;
    for (auto it = m_categoryUsage.constBegin(); it != m_categoryUsage.constEnd(); ++it) {
        categories[it.key()] = it.value();
    }
    root[QStringLiteral("categories")] = categories;

    QJsonArray apps;
    for (const auto &[name, count] : m_appFrequency) {
        QJsonObject appObj;
        appObj[QStringLiteral("name")] = name;
        appObj[QStringLiteral("count")] = count;
        apps.append(appObj);
    }
    root[QStringLiteral("applications")] = apps;

    QJsonArray patterns;
    for (const ActivityPattern &p : m_patterns) {
        QJsonObject pObj;
        pObj[QStringLiteral("process")] = p.processName;
        pObj[QStringLiteral("hour")] = p.hourOfDay;
        pObj[QStringLiteral("frequency")] = p.frequency;
        pObj[QStringLiteral("description")] = p.description;
        patterns.append(pObj);
    }
    root[QStringLiteral("patterns")] = patterns;

    return root;
}

void ActivityAnalyzer::importAnalysis(const QJsonObject &data)
{
    m_categoryUsage.clear();
    QJsonObject categories = data[QStringLiteral("categories")].toObject();
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        m_categoryUsage[it.key()] = it.value().toInt();
    }

    m_appFrequency.clear();
    QJsonArray apps = data[QStringLiteral("applications")].toArray();
    for (const QJsonValue &val : apps) {
        QJsonObject appObj = val.toObject();
        m_appFrequency.append({
            appObj[QStringLiteral("name")].toString(),
            appObj[QStringLiteral("count")].toInt()
        });
    }

    m_patterns.clear();
    QJsonArray patterns = data[QStringLiteral("patterns")].toArray();
    for (const QJsonValue &val : patterns) {
        QJsonObject pObj = val.toObject();
        ActivityPattern p;
        p.processName = pObj[QStringLiteral("process")].toString();
        p.hourOfDay = pObj[QStringLiteral("hour")].toInt();
        p.frequency = pObj[QStringLiteral("frequency")].toInt();
        p.description = pObj[QStringLiteral("description")].toString();
        m_patterns.append(p);
    }
}
