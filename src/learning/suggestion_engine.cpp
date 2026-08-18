#include "learning/suggestion_engine.hpp"

#include <QDateTime>
#include <QMap>
#include <algorithm>

SuggestionEngine::SuggestionEngine(QObject *parent)
    : QObject(parent)
{
}

void SuggestionEngine::initialize(TaskTracker *tracker, ActivityAnalyzer *analyzer)
{
    m_tracker = tracker;
    m_analyzer = analyzer;
}

QList<Suggestion> SuggestionEngine::generateStartupSuggestions()
{
    QList<Suggestion> allSuggestions;

    allSuggestions.append(suggestByTimeOfDay());
    allSuggestions.append(suggestByFrequency());
    allSuggestions.append(suggestByCategory());
    allSuggestions.append(suggestRecentWorkflows());

    QList<QString> seenTitles;
    QList<Suggestion> uniqueSuggestions;
    for (const Suggestion &s : allSuggestions) {
        if (!seenTitles.contains(s.title)) {
            seenTitles.append(s.title);
            uniqueSuggestions.append(s);
        }
    }

    std::sort(uniqueSuggestions.begin(), uniqueSuggestions.end(),
              [](const Suggestion &a, const Suggestion &b) {
                  return a.priority > b.priority;
              });

    if (uniqueSuggestions.size() > m_maxSuggestions) {
        uniqueSuggestions = uniqueSuggestions.mid(0, m_maxSuggestions);
    }

    emit suggestionsGenerated(uniqueSuggestions);
    return uniqueSuggestions;
}

QString SuggestionEngine::formatSuggestions(const QList<Suggestion> &suggestions) const
{
    if (suggestions.isEmpty()) {
        return QStringLiteral(
            "No suggestions available yet. I'm still learning your habits. "
            "Keep using your device and I'll have personalized suggestions ready next time!");
    }

    QString result = QStringLiteral("**Here's what I think you might want to do:**\n\n");

    for (int i = 0; i < suggestions.size(); ++i) {
        const Suggestion &s = suggestions.at(i);
        result += QStringLiteral("**%1. %2**\n   %3\n")
                      .arg(i + 1)
                      .arg(s.title, s.description);
    }

    result += QStringLiteral(
        "\n_Tip: These suggestions are based on your activity patterns. "
        "The more you use your device, the better they get!_");

    return result;
}

void SuggestionEngine::setMaxSuggestions(int max)
{
    m_maxSuggestions = qMax(1, max);
}

int SuggestionEngine::maxSuggestions() const
{
    return m_maxSuggestions;
}

void SuggestionEngine::setMinFrequencyThreshold(int threshold)
{
    m_minFrequency = qMax(1, threshold);
}

int SuggestionEngine::minFrequencyThreshold() const
{
    return m_minFrequency;
}

QList<Suggestion> SuggestionEngine::suggestByTimeOfDay() const
{
    QList<Suggestion> suggestions;
    if (!m_analyzer) {
        return suggestions;
    }

    const int currentHour = QTime::currentTime().hour();
    const QList<TimeSlotUsage> hourly = m_analyzer->hourlyBreakdown();

    if (currentHour >= 0 && currentHour < hourly.size()) {
        const TimeSlotUsage &slot = hourly.at(currentHour);

        if (!slot.dominantCategory.isEmpty() && slot.dominantCategory != QStringLiteral("other")) {
            const QMap<QString, int> &cats = slot.categoryFrequency;
            for (auto it = cats.constBegin(); it != cats.constEnd(); ++it) {
                if (it.key() == slot.dominantCategory && it.value() >= 2) {
                    Suggestion s;
                    s.category = it.key();
                    s.priority = 10 + it.value();
                    s.title = QStringLiteral("Continue %1 tasks").arg(it.key());
                    s.description = QStringLiteral(
                        "You usually do %1 work around this time (%2).")
                        .arg(it.key(), timeDescription(currentHour));
                    suggestions.append(s);
                    break;
                }
            }
        }
    }

    const QList<ActivityPattern> patterns = m_analyzer->detectedPatterns();
    for (const ActivityPattern &p : patterns) {
        if (p.hourOfDay == currentHour || p.hourOfDay == currentHour - 1 ||
            p.hourOfDay == currentHour + 1) {
            if (!isCurrentlyRunning(p.processName)) {
                Suggestion s = createAppSuggestion(
                    p.processName,
                    QStringLiteral("You often use this around %1:00")
                        .arg(p.hourOfDay, 2, 10, QLatin1Char('0')));
                s.priority = 8 + (p.frequency > 10 ? 5 : p.frequency);
                suggestions.append(s);
            }
        }
    }

    return suggestions;
}

QList<Suggestion> SuggestionEngine::suggestByFrequency() const
{
    QList<Suggestion> suggestions;
    if (!m_analyzer) {
        return suggestions;
    }

    const QList<QString> topApps = m_analyzer->mostUsedApplications(5);

    for (const QString &app : topApps) {
        if (!isCurrentlyRunning(app)) {
            Suggestion s = createAppSuggestion(
                app, QStringLiteral("One of your most-used applications"));
            s.priority = 7;
            suggestions.append(s);
        }
    }

    return suggestions;
}

QList<Suggestion> SuggestionEngine::suggestByCategory() const
{
    QList<Suggestion> suggestions;
    if (!m_analyzer) {
        return suggestions;
    }

    const QMap<QString, int> categories = m_analyzer->categoryUsageSummary();
    int totalActivities = 0;
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        totalActivities += it.value();
    }

    if (totalActivities == 0) {
        return suggestions;
    }

    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
        double percentage = static_cast<double>(it.value()) / totalActivities * 100.0;
        if (percentage > 25.0) {
            Suggestion s;
            s.category = it.key();
            s.priority = static_cast<int>(percentage / 10);
            s.title = QStringLiteral("Your %1 workflow").arg(it.key());
            s.description = QStringLiteral(
                "About %1%% of your activity is %2-related. Need help with that?")
                .arg(QString::number(percentage, 'f', 0), it.key());
            suggestions.append(s);
        }
    }

    return suggestions;
}

QList<Suggestion> SuggestionEngine::suggestRecentWorkflows() const
{
    QList<Suggestion> suggestions;
    if (!m_tracker) {
        return suggestions;
    }

    const QList<TaskEntry> recent = m_tracker->recentEntries(30);

    QMap<QString, int> recentCategories;
    for (const TaskEntry &entry : recent) {
        recentCategories[entry.category] += 1;
    }

    QString dominantRecentCat;
    int maxCount = 0;
    for (auto it = recentCategories.constBegin(); it != recentCategories.constEnd(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            dominantRecentCat = it.key();
        }
    }

    if (!dominantRecentCat.isEmpty() && dominantRecentCat != QStringLiteral("other") &&
        dominantRecentCat != QStringLiteral("system")) {
        Suggestion s;
        s.category = dominantRecentCat;
        s.priority = 6;
        s.title = QStringLiteral("Resume %1 activity").arg(dominantRecentCat);
        s.description = QStringLiteral(
            "Your recent activity shows heavy %1 usage. Want to continue?")
            .arg(dominantRecentCat);
        suggestions.append(s);
    }

    QList<QString> recentProcesses;
    for (const TaskEntry &entry : recent) {
        if (!recentProcesses.contains(entry.processName)) {
            recentProcesses.append(entry.processName);
        }
    }

    if (recentProcesses.size() >= 2) {
        QString apps;
        const int showCount = qMin(3, recentProcesses.size());
        for (int i = 0; i < showCount; ++i) {
            if (i > 0) {
                apps += QStringLiteral(", ");
            }
            apps += friendlyAppName(recentProcesses.at(i));
        }

        Suggestion s;
        s.priority = 5;
        s.title = QStringLiteral("Recent apps");
        s.description = QStringLiteral(
            "You were recently using: %1. Want to pick up where you left off?")
            .arg(apps);
        suggestions.append(s);
    }

    return suggestions;
}

Suggestion SuggestionEngine::createAppSuggestion(const QString &appName,
                                                  const QString &reason) const
{
    Suggestion s;
    s.title = QStringLiteral("Open %1").arg(friendlyAppName(appName));
    s.description = reason;
    s.category = QStringLiteral("app");
    s.launchCommand = appName;
    return s;
}

QString SuggestionEngine::friendlyAppName(const QString &processName) const
{
    static const QMap<QString, QString> nameMap = {
        {QStringLiteral("code"), QStringLiteral("VS Code")},
        {QStringLiteral("firefox"), QStringLiteral("Firefox")},
        {QStringLiteral("chromium"), QStringLiteral("Chromium")},
        {QStringLiteral("chrome"), QStringLiteral("Google Chrome")},
        {QStringLiteral("brave"), QStringLiteral("Brave Browser")},
        {QStringLiteral("vim"), QStringLiteral("Vim")},
        {QStringLiteral("nvim"), QStringLiteral("Neovim")},
        {QStringLiteral("emacs"), QStringLiteral("Emacs")},
        {QStringLiteral("sublime_text"), QStringLiteral("Sublime Text")},
        {QStringLiteral("qtcreator"), QStringLiteral("Qt Creator")},
        {QStringLiteral("clion"), QStringLiteral("CLion")},
        {QStringLiteral("telegram-desktop"), QStringLiteral("Telegram")},
        {QStringLiteral("discord"), QStringLiteral("Discord")},
        {QStringLiteral("slack"), QStringLiteral("Slack")},
        {QStringLiteral("spotify"), QStringLiteral("Spotify")},
        {QStringLiteral("mpv"), QStringLiteral("MPV Player")},
        {QStringLiteral("vlc"), QStringLiteral("VLC")},
        {QStringLiteral("obsidian"), QStringLiteral("Obsidian")},
        {QStringLiteral("nautilus"), QStringLiteral("Files")},
        {QStringLiteral("dolphin"), QStringLiteral("Dolphin")},
        {QStringLiteral("thunar"), QStringLiteral("Thunar")},
        {QStringLiteral("alacritty"), QStringLiteral("Alacritty")},
        {QStringLiteral("kitty"), QStringLiteral("Kitty")},
        {QStringLiteral("konsole"), QStringLiteral("Konsole")},
    };

    const QString lower = processName.toLower();
    auto it = nameMap.find(lower);
    if (it != nameMap.end()) {
        return it.value();
    }

    QString friendly = processName;
    if (!friendly.isEmpty()) {
        friendly[0] = friendly[0].toUpper();
    }
    return friendly;
}

QString SuggestionEngine::timeDescription(int hour) const
{
    if (hour >= 5 && hour < 12) {
        return QStringLiteral("morning");
    } else if (hour >= 12 && hour < 17) {
        return QStringLiteral("afternoon");
    } else if (hour >= 17 && hour < 21) {
        return QStringLiteral("evening");
    } else {
        return QStringLiteral("night");
    }
}

bool SuggestionEngine::isCurrentlyRunning(const QString &processName) const
{
    const QString cmd = QStringLiteral("pgrep -x %1 >/dev/null 2>&1")
                            .arg(processName);
    return system(cmd.toUtf8().constData()) == 0;
}
