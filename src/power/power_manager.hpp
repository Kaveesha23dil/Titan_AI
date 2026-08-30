#ifndef TITANAI_POWER_MANAGER_HPP
#define TITANAI_POWER_MANAGER_HPP

#include <QObject>
#include <QString>
#include <QTimer>

// ---------------------------------------------------------------------------
// BatteryInfo – snapshot of the system's power state read from sysfs
// ---------------------------------------------------------------------------
struct BatteryInfo {
    bool   present      {false};   // false when no battery found (desktop)
    bool   charging     {false};   // true when AC or charging
    bool   acOnline     {false};   // true when AC adapter is plugged in
    int    percent      {-1};      // 0-100, -1 = unknown
    int    powerNowMw   {-1};      // current power draw in milliwatts, -1 = n/a
    int    energyNowMwh {-1};      // remaining energy in mWh
    int    energyFullMwh{-1};      // full design energy in mWh
    QString statusText;            // "Charging", "Discharging", "Full", "Unknown"

    // Convenience helpers
    [[nodiscard]] bool isCritical()  const { return present && !acOnline && percent >= 0 && percent <= 10; }
    [[nodiscard]] bool isLow()       const { return present && !acOnline && percent >= 0 && percent <= 20; }
    [[nodiscard]] bool isModerate()  const { return present && !acOnline && percent >  20 && percent <= 50; }
    [[nodiscard]] bool isHealthy()   const { return !present || acOnline || percent > 50; }
};

// ---------------------------------------------------------------------------
// PowerProfile – the four operating modes
// ---------------------------------------------------------------------------
enum class PowerProfile {
    Performance,  // max speed, no throttling
    Balanced,     // default – moderate settings
    PowerSaver,   // reduced threads, brightness, longer keep_alive gap
    SmartAuto     // auto-selects based on battery % + usage pattern
};

// ---------------------------------------------------------------------------
// PowerManager
// ---------------------------------------------------------------------------
class PowerManager : public QObject {
    Q_OBJECT

public:
    explicit PowerManager(QObject *parent = nullptr);
    ~PowerManager() override = default;

    // --- Battery info ---
    [[nodiscard]] BatteryInfo batteryInfo() const;
    void refreshBatteryInfo();                // reads sysfs immediately

    // --- Profile control ---
    void setProfile(PowerProfile profile);
    [[nodiscard]] PowerProfile currentProfile() const;
    [[nodiscard]] QString profileName(PowerProfile p) const;

    // --- Smart auto-policy (call after refreshing battery + usage pattern) ---
    // usageCategory: "development", "browsing", "media", "idle", etc.
    void applySmartPolicy(const QString &usageCategory = QString());

    // --- Brightness control ---
    [[nodiscard]] int  currentBrightness() const;   // 0-100 %
    bool setBrightness(int percent);                 // returns false if brightnessctl absent

    // --- LLM thread / context recommendations based on current profile ---
    [[nodiscard]] int recommendedThreads()  const;
    [[nodiscard]] int recommendedContext()  const;
    [[nodiscard]] int recommendedBatch()    const;
    [[nodiscard]] QString recommendedKeepAlive() const;

    // --- Human-readable report ---
    [[nodiscard]] QString generateReport() const;

signals:
    void batteryInfoUpdated(const BatteryInfo &info);
    void profileChanged(PowerProfile profile);
    void brightnessChanged(int percent);
    void lowBatteryWarning(int percent);
    void criticalBatteryWarning(int percent);

private slots:
    void onPollTimer();

private:
    // sysfs helpers
    static QString readSysfsFile(const QString &path);
    static int     readSysfsInt(const QString &path, int fallback = -1);
    BatteryInfo    readBatteryInfo() const;
    int            readBrightnessPercent() const;
    void           applyProfileSettings();
    void           applyProfileSettings(PowerProfile effective);

    BatteryInfo  m_batteryInfo;
    PowerProfile m_profile    {PowerProfile::SmartAuto};
    QTimer       m_pollTimer;
    int          m_lastWarningPercent{100};     // tracks low-battery warning hysteresis
};

#endif // TITANAI_POWER_MANAGER_HPP
