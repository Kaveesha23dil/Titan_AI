#ifndef TITANAI_DESKTOP_ENVIRONMENT_DETECTOR_HPP
#define TITANAI_DESKTOP_ENVIRONMENT_DETECTOR_HPP

#include <QString>
#include <QStringList>

struct DesktopEnvironmentInfo {
    QString sessionType;
    QString desktopName;
    QString compositor;
    QString windowBackend;
    QString unsupportedReason;
    bool    wayland{false};
    bool    x11{false};
    bool    hyprland{false};
    bool    hyprctlAvailable{false};
    bool    wmctrlAvailable{false};
    bool    xdotoolAvailable{false};
    bool    windowManagementSupported{false};
    bool    applicationLaunchSupported{true};
    QStringList capabilities;

    [[nodiscard]] QString summary() const;
};

class DesktopEnvironmentDetector {
public:
    DesktopEnvironmentDetector() = delete;

    [[nodiscard]] static DesktopEnvironmentInfo detect();
};

#endif // TITANAI_DESKTOP_ENVIRONMENT_DETECTOR_HPP
