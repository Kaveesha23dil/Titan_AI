#include "gui/calendar_settings_dialog.hpp"
#include "calendar/calendar_manager.hpp"
#include "calendar/notification_manager.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

CalendarSettingsDialog::CalendarSettingsDialog(CalendarManager &calendarMgr,
                                               NotificationManager &notifMgr,
                                               QWidget *parent)
    : QDialog(parent)
    , m_calendarMgr(calendarMgr)
    , m_notifMgr(notifMgr)
{
    setWindowTitle(QStringLiteral("Calendar Settings"));
    setMinimumSize(500, 520);

    auto *filesGroup = new QGroupBox(QStringLiteral("Calendar Files (ICS)"), this);
    auto *filesLayout = new QVBoxLayout(filesGroup);

    m_fileList = new QListWidget(this);
    m_fileList->setMinimumHeight(120);
    filesLayout->addWidget(m_fileList);

    auto *fileButtons = new QHBoxLayout;
    m_addButton = new QPushButton(QStringLiteral("Add File..."), this);
    m_removeButton = new QPushButton(QStringLiteral("Remove"), this);
    m_checkNowButton = new QPushButton(QStringLiteral("Check Now"), this);
    fileButtons->addWidget(m_addButton);
    fileButtons->addWidget(m_removeButton);
    fileButtons->addStretch(1);
    fileButtons->addWidget(m_checkNowButton);
    filesLayout->addLayout(fileButtons);

    auto *notifGroup = new QGroupBox(QStringLiteral("Notifications"), this);
    auto *notifLayout = new QVBoxLayout(notifGroup);

    m_notifEnabledCheck = new QCheckBox(QStringLiteral("Enable event notifications"), this);
    notifLayout->addWidget(m_notifEnabledCheck);

    auto *reminderLayout = new QHBoxLayout;
    reminderLayout->addWidget(new QLabel(QStringLiteral("Remind before:")));
    m_reminder30Check = new QCheckBox(QStringLiteral("30 min"), this);
    m_reminder15Check = new QCheckBox(QStringLiteral("15 min"), this);
    m_reminder5Check = new QCheckBox(QStringLiteral("5 min"), this);
    reminderLayout->addWidget(m_reminder30Check);
    reminderLayout->addWidget(m_reminder15Check);
    reminderLayout->addWidget(m_reminder5Check);
    reminderLayout->addStretch(1);
    notifLayout->addLayout(reminderLayout);

    m_testNotifButton = new QPushButton(QStringLiteral("Test Notification"), this);
    notifLayout->addWidget(m_testNotifButton);

    auto *previewGroup = new QGroupBox(QStringLiteral("Today's Events"), this);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    m_eventsPreview = new QLabel(this);
    m_eventsPreview->setWordWrap(true);
    m_eventsPreview->setMinimumHeight(80);
    m_eventsPreview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_eventsPreview->setStyleSheet(QStringLiteral("background: #f7fafc; padding: 8px; border: 1px solid #e2e8f0; border-radius: 4px;"));
    previewLayout->addWidget(m_eventsPreview);

    auto *dialogButtons = new QHBoxLayout;
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    dialogButtons->addStretch(1);
    dialogButtons->addWidget(closeButton);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(filesGroup);
    mainLayout->addWidget(notifGroup);
    mainLayout->addWidget(previewGroup);
    mainLayout->addLayout(dialogButtons);

    connect(m_addButton, &QPushButton::clicked, this, &CalendarSettingsDialog::onAddFile);
    connect(m_removeButton, &QPushButton::clicked, this, &CalendarSettingsDialog::onRemoveFile);
    connect(m_checkNowButton, &QPushButton::clicked, this, &CalendarSettingsDialog::onCheckNow);
    connect(m_testNotifButton, &QPushButton::clicked, this, &CalendarSettingsDialog::onTestNotification);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_notifEnabledCheck, &QCheckBox::toggled, this, [this](bool checked) {
        NotificationManager::Config cfg = m_notifMgr.config();
        cfg.enabled = checked;
        m_notifMgr.setConfig(cfg);
    });
    connect(m_reminder30Check, &QCheckBox::toggled, this, [this](bool) {
        NotificationManager::Config cfg = m_notifMgr.config();
        cfg.reminderMinutes.clear();
        if (m_reminder30Check->isChecked()) cfg.reminderMinutes.append(30);
        if (m_reminder15Check->isChecked()) cfg.reminderMinutes.append(15);
        if (m_reminder5Check->isChecked()) cfg.reminderMinutes.append(5);
        if (cfg.reminderMinutes.isEmpty()) cfg.reminderMinutes.append(15);
        m_notifMgr.setConfig(cfg);
    });
    connect(m_reminder15Check, &QCheckBox::toggled, this, [this](bool) {
        m_reminder30Check->toggled(false);
    });
    connect(m_reminder5Check, &QCheckBox::toggled, this, [this](bool) {
        m_reminder30Check->toggled(false);
    });

    const NotificationManager::Config cfg = m_notifMgr.config();
    m_notifEnabledCheck->setChecked(cfg.enabled);
    m_reminder30Check->setChecked(cfg.reminderMinutes.contains(30));
    m_reminder15Check->setChecked(cfg.reminderMinutes.contains(15));
    m_reminder5Check->setChecked(cfg.reminderMinutes.contains(5));

    refreshFileList();
    refreshEventsPreview();
}

void CalendarSettingsDialog::onAddFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select ICS Calendar File"),
        QDir::homePath(),
        QStringLiteral("iCalendar Files (*.ics *.ical);;All Files (*)"));

    if (!file.isEmpty()) {
        m_calendarMgr.addCalendarFile(file);
        refreshFileList();
        refreshEventsPreview();
    }
}

void CalendarSettingsDialog::onRemoveFile()
{
    const int row = m_fileList->currentRow();
    if (row < 0) {
        return;
    }

    const QString file = m_fileList->item(row)->text();
    m_calendarMgr.removeCalendarFile(file);
    refreshFileList();
    refreshEventsPreview();
}

void CalendarSettingsDialog::onCheckNow()
{
    m_calendarMgr.refresh();
    refreshEventsPreview();
}

void CalendarSettingsDialog::onTestNotification()
{
    m_notifMgr.showDesktopNotification(
        QStringLiteral("TitanAI Test"),
        QStringLiteral("Calendar notifications are working! You will receive reminders before your events."),
        QSystemTrayIcon::Information);
}

void CalendarSettingsDialog::onCalendarFilesChanged()
{
    refreshFileList();
    refreshEventsPreview();
}

void CalendarSettingsDialog::refreshFileList()
{
    m_fileList->clear();
    const QStringList files = m_calendarMgr.calendarFiles();
    for (const QString &file : files) {
        m_fileList->addItem(file);
    }
    m_removeButton->setEnabled(!files.isEmpty());
}

void CalendarSettingsDialog::refreshEventsPreview()
{
    const QList<CalendarEvent> today = m_calendarMgr.getTodaysEvents();
    if (today.isEmpty()) {
        m_eventsPreview->setText(QStringLiteral("No events today. Add an ICS file to get started."));
        return;
    }

    QString text = QStringLiteral("<b>Today's Events (%1):</b><br/><br/>").arg(today.size());
    for (const CalendarEvent &e : today) {
        text += QStringLiteral("&#8226; <b>%1</b><br/>").arg(e.summary.toHtmlEscaped());
        text += QStringLiteral("&nbsp;&nbsp;&nbsp;%1<br/>").arg(e.formatTimeRange().toHtmlEscaped());
        if (!e.location.isEmpty()) {
            text += QStringLiteral("&nbsp;&nbsp;&nbsp;Location: %1<br/>").arg(e.location.toHtmlEscaped());
        }
        text += QStringLiteral("<br/>");
    }
    m_eventsPreview->setText(text);
}
