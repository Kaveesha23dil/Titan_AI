#include "automation/automation_history_manager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr int kMaxHistoryEntries = 100;
}

AutomationHistoryManager::AutomationHistoryManager(QObject *parent)
    : QObject(parent)
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_storageDir = dataDir + QStringLiteral("/automation");
    QDir().mkpath(m_storageDir);
    QFile::setPermissions(m_storageDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
    load();
}

void AutomationHistoryManager::setStorageDirectory(const QString &directory)
{
    m_storageDir = directory;
    QDir().mkpath(m_storageDir);
    QFile::setPermissions(m_storageDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
    load();
}

QString AutomationHistoryManager::storageDirectory() const
{
    return m_storageDir;
}

QString AutomationHistoryManager::storageFilePath() const
{
    return m_storageDir + QStringLiteral("/history.json");
}

AutomationHistoryEntry AutomationHistoryManager::sanitized(AutomationHistoryEntry entry)
{
    for (AutomationActionResult &result : entry.results) {
        result.output.clear();
    }
    return entry;
}

void AutomationHistoryManager::load()
{
    m_entries.clear();

    QFile file(storageFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) {
        return;
    }

    const QJsonArray array = doc.array();
    m_entries.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            m_entries.append(AutomationHistoryEntry::fromJson(value.toObject()));
        }
    }
    while (m_entries.size() > kMaxHistoryEntries) {
        m_entries.removeLast();
    }
}

void AutomationHistoryManager::save() const
{
    QJsonArray array;
    for (const AutomationHistoryEntry &entry : m_entries) {
        array.append(entry.toJson());
    }

    QSaveFile file(storageFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if (file.commit()) {
        QFile::setPermissions(storageFilePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

QString AutomationHistoryManager::addEntry(const AutomationHistoryEntry &entry)
{
    AutomationHistoryEntry record = sanitized(entry);
    if (record.workflowId.isEmpty()) {
        record.workflowId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!record.startedAt.isValid()) {
        record.startedAt = QDateTime::currentDateTime();
    }
    if (!record.finishedAt.isValid()) {
        record.finishedAt = record.startedAt;
    }

    m_entries.prepend(record);
    while (m_entries.size() > kMaxHistoryEntries) {
        m_entries.removeLast();
    }

    save();
    emit historyChanged();
    return record.workflowId;
}

bool AutomationHistoryManager::updateEntry(const AutomationWorkflowResult &result)
{
    for (AutomationHistoryEntry &entry : m_entries) {
        if (entry.workflowId == result.workflowId) {
            entry.plan = result.plan;
            entry.taskName = result.plan.taskName;
            entry.status = result.status;
            entry.results = result.results;
            entry.error = result.error;
            entry.startedAt = result.startedAt;
            entry.finishedAt = result.finishedAt;
            entry = sanitized(entry);
            save();
            emit historyChanged();
            return true;
        }
    }
    return false;
}

QList<AutomationHistoryEntry> AutomationHistoryManager::allEntries() const
{
    return m_entries;
}

int AutomationHistoryManager::count() const
{
    return m_entries.size();
}

void AutomationHistoryManager::clear()
{
    m_entries.clear();
    save();
    emit historyChanged();
}
