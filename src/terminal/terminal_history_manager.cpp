#include "terminal/terminal_history_manager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr int kMaxHistoryEntries = 200;
}

TerminalHistoryManager::TerminalHistoryManager(QObject *parent)
    : QObject(parent)
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_storageDir = dataDir + QStringLiteral("/terminal");
    QDir().mkpath(m_storageDir);
    QFile::setPermissions(m_storageDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
    load();
}

void TerminalHistoryManager::setStorageDirectory(const QString &directory)
{
    m_storageDir = directory;
    QDir().mkpath(m_storageDir);
    QFile::setPermissions(m_storageDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
    load();
}

QString TerminalHistoryManager::storageDirectory() const
{
    return m_storageDir;
}

QString TerminalHistoryManager::storageFilePath() const
{
    return m_storageDir + QStringLiteral("/history.json");
}

void TerminalHistoryManager::load()
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

    const QJsonArray arr = doc.array();
    m_entries.reserve(arr.size());
    for (const QJsonValue &value : arr) {
        if (value.isObject()) {
            m_entries.append(TerminalHistoryEntry::fromJson(value.toObject()));
        }
    }
}

void TerminalHistoryManager::save() const
{
    QJsonArray arr;
    for (const TerminalHistoryEntry &entry : m_entries) {
        arr.append(entry.toJson());
    }

    QSaveFile file(storageFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    if (file.commit()) {
        QFile::setPermissions(storageFilePath(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

QString TerminalHistoryManager::addEntry(const TerminalHistoryEntry &entry)
{
    TerminalHistoryEntry record = entry;
    if (record.id.isEmpty()) {
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!record.timestamp.isValid()) {
        record.timestamp = QDateTime::currentDateTime();
    }

    m_entries.prepend(record);
    while (m_entries.size() > kMaxHistoryEntries) {
        m_entries.removeLast();
    }

    save();
    emit historyChanged();
    return record.id;
}

bool TerminalHistoryManager::updateEntry(const QString &id, const QString &status,
                                         int exitCode, qint64 durationMs)
{
    for (TerminalHistoryEntry &entry : m_entries) {
        if (entry.id == id) {
            entry.status     = status;
            entry.exitCode   = exitCode;
            entry.durationMs = durationMs;
            save();
            emit historyChanged();
            return true;
        }
    }
    return false;
}

QList<TerminalHistoryEntry> TerminalHistoryManager::allEntries() const
{
    return m_entries;
}

int TerminalHistoryManager::count() const
{
    return m_entries.size();
}

void TerminalHistoryManager::clear()
{
    m_entries.clear();
    save();
    emit historyChanged();
}
