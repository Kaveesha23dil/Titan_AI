#ifndef TITANAI_WINDOW_MANAGER_HPP
#define TITANAI_WINDOW_MANAGER_HPP

#include <QRect>
#include <QSize>
#include <QString>
#include <QList>

#include <memory>

#include "automation/desktop_environment_detector.hpp"

struct WindowInfo {
    QString id;
    QString title;
    QString appClass;
    QString appName;
    QRect   geometry;
    bool    active{false};
};

class IWindowManager {
public:
    virtual ~IWindowManager() = default;

    [[nodiscard]] virtual QString backendName() const = 0;
    [[nodiscard]] virtual bool isSupported() const = 0;
    [[nodiscard]] virtual QString unsupportedReason() const = 0;

    [[nodiscard]] virtual QList<WindowInfo> listWindows(QString *error = nullptr) = 0;
    [[nodiscard]] virtual WindowInfo activeWindow(QString *error = nullptr) = 0;
    [[nodiscard]] virtual bool focusWindow(const QString &windowId,
                                           QString *error = nullptr) = 0;
    [[nodiscard]] virtual bool moveWindow(const QString &windowId,
                                          const QRect &geometry,
                                          QString *error = nullptr) = 0;
    [[nodiscard]] virtual bool resizeWindow(const QString &windowId,
                                            const QSize &size,
                                            QString *error = nullptr) = 0;
    [[nodiscard]] virtual QRect screenGeometry() const = 0;
};

[[nodiscard]] std::unique_ptr<IWindowManager> createWindowManager(
    const DesktopEnvironmentInfo &info = DesktopEnvironmentDetector::detect());

class UnsupportedWindowManager final : public IWindowManager {
public:
    explicit UnsupportedWindowManager(QString reason);

    [[nodiscard]] QString backendName() const override { return QStringLiteral("unsupported"); }
    [[nodiscard]] bool isSupported() const override { return false; }
    [[nodiscard]] QString unsupportedReason() const override { return m_reason; }

    [[nodiscard]] QList<WindowInfo> listWindows(QString *error = nullptr) override;
    [[nodiscard]] WindowInfo activeWindow(QString *error = nullptr) override;
    [[nodiscard]] bool focusWindow(const QString &, QString *error = nullptr) override;
    [[nodiscard]] bool moveWindow(const QString &, const QRect &,
                                  QString *error = nullptr) override;
    [[nodiscard]] bool resizeWindow(const QString &, const QSize &,
                                    QString *error = nullptr) override;
    [[nodiscard]] QRect screenGeometry() const override;

private:
    QString m_reason;
};

class HyprlandWindowManager final : public IWindowManager {
public:
    explicit HyprlandWindowManager(bool available, QString reason = QString());

    [[nodiscard]] QString backendName() const override { return QStringLiteral("hyprland"); }
    [[nodiscard]] bool isSupported() const override { return m_available; }
    [[nodiscard]] QString unsupportedReason() const override { return m_reason; }

    [[nodiscard]] QList<WindowInfo> listWindows(QString *error = nullptr) override;
    [[nodiscard]] WindowInfo activeWindow(QString *error = nullptr) override;
    [[nodiscard]] bool focusWindow(const QString &windowId,
                                   QString *error = nullptr) override;
    [[nodiscard]] bool moveWindow(const QString &windowId, const QRect &geometry,
                                  QString *error = nullptr) override;
    [[nodiscard]] bool resizeWindow(const QString &windowId, const QSize &size,
                                    QString *error = nullptr) override;
    [[nodiscard]] QRect screenGeometry() const override;

private:
    [[nodiscard]] bool runHyprctl(const QStringList &arguments, QByteArray *output,
                                  QString *error, int timeoutMs = 2500) const;
    [[nodiscard]] bool dispatchLua(const QString &expression, QString *error) const;
    [[nodiscard]] bool available(QString *error = nullptr) const;

    bool    m_available{false};
    QString m_reason;
};

class X11WmctrlWindowManager final : public IWindowManager {
public:
    explicit X11WmctrlWindowManager(bool available, QString reason = QString());

    [[nodiscard]] QString backendName() const override { return QStringLiteral("x11-wmctrl"); }
    [[nodiscard]] bool isSupported() const override { return m_available; }
    [[nodiscard]] QString unsupportedReason() const override { return m_reason; }

    [[nodiscard]] QList<WindowInfo> listWindows(QString *error = nullptr) override;
    [[nodiscard]] WindowInfo activeWindow(QString *error = nullptr) override;
    [[nodiscard]] bool focusWindow(const QString &windowId,
                                   QString *error = nullptr) override;
    [[nodiscard]] bool moveWindow(const QString &windowId, const QRect &geometry,
                                  QString *error = nullptr) override;
    [[nodiscard]] bool resizeWindow(const QString &windowId, const QSize &size,
                                    QString *error = nullptr) override;
    [[nodiscard]] QRect screenGeometry() const override;

private:
    [[nodiscard]] bool runWmctrl(const QStringList &arguments, QByteArray *output,
                                 QString *error, int timeoutMs = 2500) const;
    [[nodiscard]] bool available(QString *error = nullptr) const;

    bool    m_available{false};
    QString m_reason;
};

#endif // TITANAI_WINDOW_MANAGER_HPP
