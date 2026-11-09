#include "automation/desktop_environment_detector.hpp"

#include <QProcessEnvironment>
#include <QStandardPaths>

DesktopEnvironmentInfo DesktopEnvironmentDetector::detect()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    DesktopEnvironmentInfo info;
    info.sessionType = env.value(QStringLiteral("XDG_SESSION_TYPE")).trimmed().toLower();
    info.desktopName = env.value(QStringLiteral("XDG_CURRENT_DESKTOP")).trimmed();
    if (info.desktopName.isEmpty()) {
        info.desktopName = env.value(QStringLiteral("DESKTOP_SESSION")).trimmed();
    }

    const QString waylandDisplay = env.value(QStringLiteral("WAYLAND_DISPLAY")).trimmed();
    const QString display = env.value(QStringLiteral("DISPLAY")).trimmed();
    info.wayland = info.sessionType == QLatin1String("wayland")
                   || !waylandDisplay.isEmpty();
    info.x11 = info.sessionType == QLatin1String("x11") || !display.isEmpty();

    const QString desktopLower = info.desktopName.toLower();
    info.hyprland = desktopLower.contains(QStringLiteral("hyprland"))
                    || !env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"))
                            .trimmed().isEmpty();
    if (info.hyprland) {
        info.compositor = QStringLiteral("Hyprland");
    } else if (info.wayland) {
        info.compositor = info.desktopName.isEmpty() ? QStringLiteral("Wayland")
                                                     : info.desktopName;
    } else if (info.x11) {
        info.compositor = QStringLiteral("X11");
    } else {
        info.compositor = QStringLiteral("Unknown");
    }

    info.hyprctlAvailable =
        !QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty();
    info.wmctrlAvailable =
        !QStandardPaths::findExecutable(QStringLiteral("wmctrl")).isEmpty();
    info.xdotoolAvailable =
        !QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty();

    info.capabilities = {
        QStringLiteral("launch_application"),
        QStringLiteral("open_directory"),
        QStringLiteral("open_file"),
        QStringLiteral("run_approved_terminal_command")
    };

    if (info.hyprland && info.hyprctlAvailable) {
        info.windowBackend = QStringLiteral("hyprland");
        info.windowManagementSupported = true;
        info.capabilities << QStringLiteral("list_windows")
                          << QStringLiteral("focus_window")
                          << QStringLiteral("move_window")
                          << QStringLiteral("resize_window")
                          << QStringLiteral("get_active_window")
                          << QStringLiteral("wait_for_window");
    } else if (!info.wayland && info.x11 && info.wmctrlAvailable) {
        info.windowBackend = QStringLiteral("x11-wmctrl");
        info.windowManagementSupported = true;
        info.capabilities << QStringLiteral("list_windows")
                          << QStringLiteral("focus_window")
                          << QStringLiteral("move_window")
                          << QStringLiteral("resize_window")
                          << QStringLiteral("get_active_window")
                          << QStringLiteral("wait_for_window");
    } else {
        info.windowBackend = QStringLiteral("unsupported");
        if (info.wayland && !info.hyprland) {
            info.unsupportedReason = QStringLiteral(
                "This Wayland compositor does not expose a portable window-management "
                "API. Window operations are unavailable; applications can still be launched.");
        } else if (info.hyprland && !info.hyprctlAvailable) {
            info.unsupportedReason = QStringLiteral(
                "Hyprland was detected but 'hyprctl' is not installed.");
        } else if (info.x11 && !info.wmctrlAvailable) {
            info.unsupportedReason = QStringLiteral(
                "X11 was detected but no supported window-management utility "
                "(wmctrl) is installed.");
        } else {
            info.unsupportedReason = QStringLiteral(
                "No supported Linux desktop window-management backend was detected.");
        }
    }

    return info;
}

QString DesktopEnvironmentInfo::summary() const
{
    QString text = QStringLiteral("Session: %1 · Desktop: %2 · Backend: %3")
                       .arg(sessionType.isEmpty() ? QStringLiteral("unknown") : sessionType,
                            desktopName.isEmpty() ? QStringLiteral("unknown") : desktopName,
                            windowBackend);
    if (!windowManagementSupported && !unsupportedReason.isEmpty()) {
        text += QStringLiteral(" — %1").arg(unsupportedReason);
    }
    return text;
}
