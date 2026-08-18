#ifndef TITANAI_SUGGESTION_ENGINE_HPP
#define TITANAI_SUGGESTION_ENGINE_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>

#include "learning/task_tracker.hpp"
#include "learning/activity_analyzer.hpp"

struct Suggestion {
    QString title;
    QString description;
    QString category;
    int priority{0};
    QString launchCommand;
};

class SuggestionEngine : public QObject {
    Q_OBJECT

public:
    explicit SuggestionEngine(QObject *parent = nullptr);
    ~SuggestionEngine() override = default;

    void initialize(TaskTracker *tracker, ActivityAnalyzer *analyzer);

    [[nodiscard]] QList<Suggestion> generateStartupSuggestions();
    [[nodiscard]] QString formatSuggestions(const QList<Suggestion> &suggestions) const;

    void setMaxSuggestions(int max);
    [[nodiscard]] int maxSuggestions() const;

    void setMinFrequencyThreshold(int threshold);
    [[nodiscard]] int minFrequencyThreshold() const;

signals:
    void suggestionsGenerated(const QList<Suggestion> &suggestions);

private:
    QList<Suggestion> suggestByTimeOfDay() const;
    QList<Suggestion> suggestByFrequency() const;
    QList<Suggestion> suggestByCategory() const;
    QList<Suggestion> suggestRecentWorkflows() const;
    Suggestion createAppSuggestion(const QString &appName, const QString &reason) const;
    QString friendlyAppName(const QString &processName) const;
    QString timeDescription(int hour) const;
    bool isCurrentlyRunning(const QString &processName) const;

    TaskTracker *m_tracker{nullptr};
    ActivityAnalyzer *m_analyzer{nullptr};
    int m_maxSuggestions{8};
    int m_minFrequency{3};
};

#endif // TITANAI_SUGGESTION_ENGINE_HPP
