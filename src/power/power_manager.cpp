#include "power/power_manager.hpp"

#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

// ---------------------------------------------------------------------------
// Sysfs paths (verified on user's Arch Linux / Intel i5-8250U system)
// ---------------------------------------------------------------------------
static constexpr const char *kBatDir    = "/sys/class/power_supply/BAT1";
static constexpr const char *kAcPath    = "/sys/class/power_supply/ACAD/online";
static constexpr const char *kGovPath   = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
static constexpr const char *kBrightnessctl = "/usr/bin/brightnessctl";

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------
PowerManager::PowerManager(QObject *parent)
    : QObject(parent)
{
    // Initial read
    refreshBatteryInfo();

    // Poll every 30 seconds
    connect(&m_pollTimer, &QTimer::timeout, this, &PowerManager::onPollTimer);
    m_pollTimer.setInterval(30'000);
    m_pollTimer.start();
}

// ---------------------------------------------------------------------------
// Battery reading
// ---------------------------------------------------------------------------
QString PowerManager::readSysfsFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

int PowerManager::readSysfsInt(const QString &path, int fallback)
{
    const QString val = readSysfsFile(path);
    if (val.isEmpty()) return fallback;
    bool ok = false;
    const int v = val.toInt(&ok);
    return ok ? v : fallback;
}

BatteryInfo PowerManager::readBatteryInfo() const
{
    BatteryInfo info;

    // Check if battery directory exists
    if (!QDir(kBatDir).exists()) {
        info.present    = false;
        info.acOnline   = true;   // assume desktop / always-on
        info.charging   = true;
        info.statusText = QStringLiteral("No Battery (Desktop)");
        return info;
    }

    info.present = true;

    // Capacity (0-100)
    info.percent = readSysfsInt(QStringLiteral("%1/capacity").arg(kBatDir));

    // Status string
    info.statusText = readSysfsFile(QStringLiteral("%1/status").arg(kBatDir));
    if (info.statusText.isEmpty()) info.statusText = QStringLiteral("Unknown");

    info.charging = (info.statusText == QLatin1String("Charging") ||
                     info.statusText == QLatin1String("Full"));

    // AC adapter
    const int acOnlineRaw = readSysfsInt(kAcPath, 0);
    info.acOnline = (acOnlineRaw == 1);

    // Energy values (sysfs reports micro-watts / micro-watt-hours → convert to mW/mWh)
    const int powerNowUw  = readSysfsInt(QStringLiteral("%1/power_now").arg(kBatDir), -1);
    const int energyNowUwh= readSysfsInt(QStringLiteral("%1/energy_now").arg(kBatDir), -1);
    const int energyFullUwh=readSysfsInt(QStringLiteral("%1/energy_full_design").arg(kBatDir), -1);

    if (powerNowUw   >= 0) info.powerNowMw   = powerNowUw   / 1000;
    if (energyNowUwh >= 0) info.energyNowMwh = energyNowUwh / 1000;
    if (energyFullUwh>= 0) info.energyFullMwh= energyFullUwh/ 1000;

    return info;
}

void PowerManager::refreshBatteryInfo()
{
    const BatteryInfo prev = m_batteryInfo;
    m_batteryInfo = readBatteryInfo();

    emit batteryInfoUpdated(m_batteryInfo);

    // Low battery warnings with hysteresis
    if (m_batteryInfo.present && !m_batteryInfo.acOnline) {
        const int pct = m_batteryInfo.percent;
        if (pct <= 10 && m_lastWarningPercent > 10) {
            emit criticalBatteryWarning(pct);
            m_lastWarningPercent = pct;
        } else if (pct <= 20 && m_lastWarningPercent > 20) {
            emit lowBatteryWarning(pct);
            m_lastWarningPercent = pct;
        }
    } else {
        m_lastWarningPercent = 100; // reset hysteresis when charging
    }

    // If in SmartAuto mode, re-apply policy when battery state changes
    if (m_profile == PowerProfile::SmartAuto) {
        if (prev.acOnline != m_batteryInfo.acOnline ||
            prev.isLow()  != m_batteryInfo.isLow()  ||
            prev.isCritical() != m_batteryInfo.isCritical()) {
            applySmartPolicy();
        }
    }
}

BatteryInfo PowerManager::batteryInfo() const
{
    return m_batteryInfo;
}

// ---------------------------------------------------------------------------
// Profile control
// ---------------------------------------------------------------------------
PowerProfile PowerManager::currentProfile() const
{
    return m_profile;
}

QString PowerManager::profileName(PowerProfile p) const
{
    switch (p) {
    case PowerProfile::Performance: return QStringLiteral("⚡ Performance");
    case PowerProfile::Balanced:    return QStringLiteral("⚖️  Balanced");
    case PowerProfile::PowerSaver:  return QStringLiteral("🌿 Power Saver");
    case PowerProfile::SmartAuto:   return QStringLiteral("🤖 Smart Auto");
    }
    return QStringLiteral("Unknown");
}

void PowerManager::setProfile(PowerProfile profile)
{
    if (m_profile == profile) return;
    m_profile = profile;
    applyProfileSettings();
    emit profileChanged(m_profile);
}

void PowerManager::applySmartPolicy(const QString &usageCategory)
{
    const BatteryInfo &bat = m_batteryInfo;
    PowerProfile chosen;

    if (bat.acOnline) {
        // Plugged in – performance unless user is idle
        if (usageCategory == QLatin1String("idle")) {
            chosen = PowerProfile::Balanced;
        } else {
            chosen = PowerProfile::Performance;
        }
    } else if (bat.isCritical()) {
        chosen = PowerProfile::PowerSaver;
    } else if (bat.isLow()) {
        chosen = PowerProfile::PowerSaver;
    } else if (bat.isModerate()) {
        // 20-50 %: balance between saving and usability
        if (usageCategory == QLatin1String("development")) {
            chosen = PowerProfile::Balanced;
        } else {
            chosen = PowerProfile::PowerSaver;
        }
    } else {
        // > 50 % on battery
        if (usageCategory == QLatin1String("development") ||
            usageCategory == QLatin1String("media")) {
            chosen = PowerProfile::Balanced;
        } else {
            chosen = PowerProfile::Balanced;
        }
    }

    // Keep SmartAuto as the user-visible profile label; internally apply settings
    applyProfileSettings(chosen);
    qDebug() << "[PowerManager] SmartAuto selected:" << profileName(chosen)
             << "(battery:" << bat.percent << "%, AC:" << bat.acOnline
             << ", usage:" << usageCategory << ")";
}

// Applies settings for an explicit profile (used internally from SmartAuto)
void PowerManager::applyProfileSettings()
{
    applyProfileSettings(m_profile);
}

// Private overload that accepts a computed profile (e.g. from SmartAuto)
// We piggyback on the real m_profile to avoid exposing an extra method; the
// function just writes the governor and brightness without touching m_profile.
static void writeCpuGovernor(const QString &gov)
{
    // Try to write to each online CPU
    const QDir cpuDir(QStringLiteral("/sys/devices/system/cpu"));
    const auto cpus = cpuDir.entryList(QStringList{QStringLiteral("cpu[0-9]*")},
                                       QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &cpu : cpus) {
        const QString path = QStringLiteral("/sys/devices/system/cpu/%1/cpufreq/scaling_governor").arg(cpu);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << gov;
        }
    }
}

void PowerManager::applyProfileSettings(PowerProfile effective)
{
    switch (effective) {
    case PowerProfile::Performance:
        writeCpuGovernor(QStringLiteral("performance"));
        setBrightness(100);
        break;
    case PowerProfile::Balanced:
        writeCpuGovernor(QStringLiteral("powersave")); // kernel handles boost
        // Don't forcibly change brightness in balanced mode
        break;
    case PowerProfile::PowerSaver:
        writeCpuGovernor(QStringLiteral("powersave"));
        setBrightness(50);
        break;
    case PowerProfile::SmartAuto:
        applySmartPolicy(); // delegate
        break;
    }
}

// ---------------------------------------------------------------------------
// LLM parameter recommendations per profile
// ---------------------------------------------------------------------------
int PowerManager::recommendedThreads() const
{
    const PowerProfile eff = (m_profile == PowerProfile::SmartAuto)
        ? (m_batteryInfo.isLow() ? PowerProfile::PowerSaver
           : m_batteryInfo.acOnline ? PowerProfile::Performance : PowerProfile::Balanced)
        : m_profile;

    switch (eff) {
    case PowerProfile::Performance: return 8;
    case PowerProfile::Balanced:    return 4;
    case PowerProfile::PowerSaver:  return 2;
    default:                        return 4;
    }
}

int PowerManager::recommendedContext() const
{
    switch (m_profile) {
    case PowerProfile::Performance: return 4096;
    case PowerProfile::Balanced:    return 1536;
    case PowerProfile::PowerSaver:  return 512;
    default:                        return 1536;
    }
}

int PowerManager::recommendedBatch() const
{
    switch (m_profile) {
    case PowerProfile::Performance: return 512;
    case PowerProfile::Balanced:    return 256;
    case PowerProfile::PowerSaver:  return 128;
    default:                        return 256;
    }
}

QString PowerManager::recommendedKeepAlive() const
{
    switch (m_profile) {
    case PowerProfile::Performance: return QStringLiteral("10m");
    case PowerProfile::Balanced:    return QStringLiteral("5m");
    case PowerProfile::PowerSaver:  return QStringLiteral("2m");
    default:                        return QStringLiteral("5m");
    }
}

// ---------------------------------------------------------------------------
// Brightness
// ---------------------------------------------------------------------------
int PowerManager::readBrightnessPercent() const
{
    if (!QFile::exists(kBrightnessctl)) return -1;
    QProcess proc;
    proc.start(kBrightnessctl, {QStringLiteral("get")});
    proc.waitForFinished(2000);
    const int current = proc.readAllStandardOutput().trimmed().toInt();

    QProcess procMax;
    procMax.start(kBrightnessctl, {QStringLiteral("max")});
    procMax.waitForFinished(2000);
    const int maxVal = procMax.readAllStandardOutput().trimmed().toInt();

    if (maxVal <= 0) return -1;
    return static_cast<int>((static_cast<double>(current) / maxVal) * 100.0);
}

int PowerManager::currentBrightness() const
{
    return readBrightnessPercent();
}

bool PowerManager::setBrightness(int percent)
{
    if (!QFile::exists(kBrightnessctl)) return false;
    percent = qBound(5, percent, 100); // never go fully dark
    QProcess proc;
    proc.start(kBrightnessctl, {QStringLiteral("set"),
                                 QStringLiteral("%1%").arg(percent)});
    const bool ok = proc.waitForFinished(3000) && proc.exitCode() == 0;
    if (ok) emit brightnessChanged(percent);
    return ok;
}

// ---------------------------------------------------------------------------
// Poll timer slot
// ---------------------------------------------------------------------------
void PowerManager::onPollTimer()
{
    refreshBatteryInfo();
}

// ---------------------------------------------------------------------------
// Human-readable report
// ---------------------------------------------------------------------------
QString PowerManager::generateReport() const
{
    const BatteryInfo &b = m_batteryInfo;
    QString r;
    QTextStream ts(&r);

    ts << "🔋 **Power Management Report**\n";
    ts << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";

    ts << "**Battery Status**\n";
    if (!b.present) {
        ts << "  • No battery detected (desktop / AC-only system)\n";
    } else {
        ts << "  • Level:    " << b.percent << " %\n";
        ts << "  • Status:   " << b.statusText << "\n";
        ts << "  • AC Power: " << (b.acOnline ? "Yes (plugged in)" : "No (on battery)") << "\n";
        if (b.powerNowMw >= 0)
            ts << "  • Draw:     " << b.powerNowMw << " mW\n";
        if (b.energyNowMwh >= 0 && b.energyFullMwh > 0) {
            ts << "  • Remaining:" << b.energyNowMwh << " / " << b.energyFullMwh << " mWh\n";
            // Rough time estimate
            if (!b.acOnline && b.powerNowMw > 0) {
                const double hours = static_cast<double>(b.energyNowMwh) / b.powerNowMw;
                const int h = static_cast<int>(hours);
                const int m = static_cast<int>((hours - h) * 60);
                ts << "  • Est. Life: ~" << h << "h " << m << "m remaining\n";
            }
        }
    }

    ts << "\n**Active Profile**: " << profileName(m_profile) << "\n";
    ts << "\n**LLM Recommendations**\n";
    ts << "  • Threads:    " << recommendedThreads()  << "\n";
    ts << "  • Context:    " << recommendedContext()   << " tokens\n";
    ts << "  • Batch:      " << recommendedBatch()     << "\n";
    ts << "  • Keep-Alive: " << recommendedKeepAlive() << "\n";

    const int bright = readBrightnessPercent();
    if (bright >= 0)
        ts << "\n**Screen Brightness**: " << bright << " %\n";

    return r;
}
