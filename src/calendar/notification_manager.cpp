#include "calendar/notification_manager.hpp"

#include <QDateTime>

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

void NotificationManager::setConfig(const Config &config)
{
    m_config = config;
}

NotificationManager::Config NotificationManager::config() const
{
    return m_config;
}

void NotificationManager::checkReminders(const QList<CalendarEvent> &upcomingEvents)
{
    if (!m_config.enabled) {
        return;
    }

    for (const CalendarEvent &event : upcomingEvents) {
        const int minsUntil = event.minutesUntilStart();
        if (minsUntil < 0) {
            continue;
        }

        for (int reminderMin : m_config.reminderMinutes) {
            const QString key = QStringLiteral("%1_%2").arg(event.uid).arg(reminderMin);

            if (minsUntil <= reminderMin && minsUntil >= 0 && !m_notifiedKeys.contains(key)) {
                m_notifiedKeys.insert(key);

                QString timeDesc;
                if (reminderMin == 0) {
                    timeDesc = QStringLiteral("starting now");
                } else if (minsUntil <= 1) {
                    timeDesc = QStringLiteral("starting in 1 minute");
                } else {
                    timeDesc = QStringLiteral("starting in %1 minutes").arg(minsUntil);
                }

                const QString title = QStringLiteral("Event Reminder");
                const QString message = QStringLiteral("%1 - %2\n%3%4")
                                            .arg(event.summary,
                                                 event.formatTimeRange(),
                                                 timeDesc,
                                                 event.location.isEmpty()
                                                     ? QString()
                                                     : QStringLiteral("\nLocation: %1")
                                                           .arg(event.location));

                if (m_config.desktopPopup && m_trayIcon) {
                    showDesktopNotification(title, message, QSystemTrayIcon::Information);
                }

                emit notificationTriggered(title, message, event);
                emit reminderAlert(message);
            }
        }
    }
}

void NotificationManager::showDesktopNotification(const QString &title,
                                                   const QString &message,
                                                   QSystemTrayIcon::MessageIcon icon)
{
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, icon, 8000);
    }
}

void NotificationManager::setSystemTrayIcon(QSystemTrayIcon *trayIcon)
{
    m_trayIcon = trayIcon;
}

bool NotificationManager::hasNotifiedAbout(const QString &eventUid, int reminderMinutes) const
{
    const QString key = QStringLiteral("%1_%2").arg(eventUid).arg(reminderMinutes);
    return m_notifiedKeys.contains(key);
}

void NotificationManager::clearNotifications()
{
    m_notifiedKeys.clear();
}
