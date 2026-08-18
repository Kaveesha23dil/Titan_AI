#ifndef TITANAI_CALENDAR_SETTINGS_DIALOG_HPP
#define TITANAI_CALENDAR_SETTINGS_DIALOG_HPP

#include <QDialog>
#include <QStringList>

class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;

class CalendarManager;
class NotificationManager;

class CalendarSettingsDialog : public QDialog {
    Q_OBJECT

public:
    CalendarSettingsDialog(CalendarManager &calendarMgr, NotificationManager &notifMgr,
                           QWidget *parent = nullptr);
    ~CalendarSettingsDialog() override = default;

private slots:
    void onAddFile();
    void onRemoveFile();
    void onCheckNow();
    void onTestNotification();
    void onCalendarFilesChanged();

private:
    void refreshFileList();
    void refreshEventsPreview();

    CalendarManager &m_calendarMgr;
    NotificationManager &m_notifMgr;
    QListWidget *m_fileList;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_checkNowButton;
    QPushButton *m_testNotifButton;
    QCheckBox *m_notifEnabledCheck;
    QCheckBox *m_reminder30Check;
    QCheckBox *m_reminder15Check;
    QCheckBox *m_reminder5Check;
    QLabel *m_eventsPreview;
};

#endif // TITANAI_CALENDAR_SETTINGS_DIALOG_HPP
