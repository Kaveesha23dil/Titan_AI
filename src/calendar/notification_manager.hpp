#ifndef TITANAI_NOTIFICATION_MANAGER_HPP
#define TITANAI_NOTIFICATION_MANAGER_HPP

#include <QObject>
#include <QTimer>
#include <QSet>
#include <QString>
#include <QList>
#include <QSystemTrayIcon>

#include "calendar/calendar_event.hpp"

class NotificationManager : public QObject {
    Q_OBJECT

public:
    struct Config {
        bool enabled{true};
        QList<int> reminderMinutes{30, 15, 5};
        bool desktopPopup{true};
        bool inAppNotification{true};
    };

    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() override = default;

    void setConfig(const Config &config);
    [[nodiscard]] Config config() const;

    void checkReminders(const QList<CalendarEvent> &upcomingEvents);
    void showDesktopNotification(const QString &title, const QString &message,
                                 QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);
    void setSystemTrayIcon(QSystemTrayIcon *trayIcon);
    [[nodiscard]] bool hasNotifiedAbout(const QString &eventUid, int reminderMinutes) const;
    void clearNotifications();

signals:
    void notificationTriggered(const QString &title, const QString &message,
                               const CalendarEvent &event);
    void reminderAlert(const QString &formattedMessage);

private:
    struct NotificationKey {
        QString eventUid;
        int reminderMinutes;
        bool operator==(const NotificationKey &other) const {
            return eventUid == other.eventUid && reminderMinutes == other.reminderMinutes;
        }
    };

    Config m_config;
    QSystemTrayIcon *m_trayIcon{nullptr};
    QSet<QString> m_notifiedKeys;
};

#endif // TITANAI_NOTIFICATION_MANAGER_HPP
