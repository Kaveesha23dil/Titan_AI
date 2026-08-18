#ifndef TITANAI_ACTIVITY_ANALYZER_HPP
#define TITANAI_ACTIVITY_ANALYZER_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

#include "learning/task_tracker.hpp"

struct ActivityPattern {
    QString processName;
    QString category;
    int dayOfWeek{-1};
    int hourOfDay{-1};
    int frequency{0};
    int averageDurationMs{0};
    QString description;
};

struct TimeSlotUsage {
    int hour{-1};
    QMap<QString, int> categoryFrequency;
    QString dominantCategory;
};

class ActivityAnalyzer : public QObject {
    Q_OBJECT

public:
    explicit ActivityAnalyzer(QObject *parent = nullptr);
    ~ActivityAnalyzer() override = default;

    void analyze(const QList<TaskEntry> &entries);
    void clearAnalysis();

    [[nodiscard]] QList<ActivityPattern> detectedPatterns() const;
    [[nodiscard]] QMap<QString, int> categoryUsageSummary() const;
    [[nodiscard]] QList<TimeSlotUsage> hourlyBreakdown() const;
    [[nodiscard]] QList<QString> mostUsedApplications(int topN = 10) const;
    [[nodiscard]] QMap<int, QList<QString>> weekdayActivity() const;
    [[nodiscard]] QJsonObject exportAnalysis() const;
    void importAnalysis(const QJsonObject &data);

signals:
    void analysisUpdated();

private:
    void buildCategorySummary(const QList<TaskEntry> &entries);
    void buildHourlyBreakdown(const QList<TaskEntry> &entries);
    void detectTimePatterns(const QList<TaskEntry> &entries);
    void detectFrequentApps(const QList<TaskEntry> &entries);

    QList<ActivityPattern> m_patterns;
    QMap<QString, int> m_categoryUsage;
    QList<TimeSlotUsage> m_hourlyBreakdown;
    QMap<int, QList<QString>> m_weekdayApps;
    QList<QPair<QString, int>> m_appFrequency;
};

#endif // TITANAI_ACTIVITY_ANALYZER_HPP
