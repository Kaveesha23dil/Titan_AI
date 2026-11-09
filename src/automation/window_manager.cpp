#include "automation/window_manager.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString processError(QProcess &process)
{
    const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (!err.isEmpty()) {
        return err;
    }
    return process.errorString();
}

bool parseGeometryPair(const QJsonValue &value, int *first, int *second)
{
    const QJsonArray array = value.toArray();
    if (array.size() < 2 || !array.at(0).isDouble() || !array.at(1).isDouble()) {
        return false;
    }
    *first = array.at(0).toInt();
    *second = array.at(1).toInt();
    return true;
}

bool isHyprlandAddress(const QString &windowId)
{
    static const QRegularExpression pattern(QStringLiteral("^0x[0-9a-fA-F]+$"));
    return pattern.match(windowId).hasMatch();
}

WindowInfo windowFromJson(const QJsonObject &obj)
{
    WindowInfo window;
    window.id    = obj.value(QStringLiteral("address")).toString().trimmed();
    window.title = obj.value(QStringLiteral("title")).toString();
    window.appClass = obj.value(QStringLiteral("class")).toString();
    if (window.appClass.isEmpty()) {
        window.appClass = obj.value(QStringLiteral("initialClass")).toString();
    }
    window.appName = obj.value(QStringLiteral("initialClass")).toString();
    window.active = obj.value(QStringLiteral("focusHistoryID")).toInt(-1) == 0;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    parseGeometryPair(obj.value(QStringLiteral("at")), &x, &y);
    parseGeometryPair(obj.value(QStringLiteral("size")), &w, &h);
    window.geometry = QRect(x, y, w, h);
    return window;
}

} // namespace

std::unique_ptr<IWindowManager> createWindowManager(const DesktopEnvironmentInfo &info)
{
    if (info.windowBackend == QLatin1String("hyprland")) {
        return std::make_unique<HyprlandWindowManager>(true);
    }
    if (info.windowBackend == QLatin1String("x11-wmctrl")) {
        return std::make_unique<X11WmctrlWindowManager>(true);
    }
    return std::make_unique<UnsupportedWindowManager>(
        info.unsupportedReason.isEmpty()
            ? QStringLiteral("Window management is not supported on this desktop.")
            : info.unsupportedReason);
}

// ─────────────────────────────────────────────────────────────
//  Unsupported backend
// ─────────────────────────────────────────────────────────────

UnsupportedWindowManager::UnsupportedWindowManager(QString reason)
    : m_reason(std::move(reason))
{
}

QList<WindowInfo> UnsupportedWindowManager::listWindows(QString *error)
{
    if (error) *error = m_reason;
    return {};
}

WindowInfo UnsupportedWindowManager::activeWindow(QString *error)
{
    if (error) *error = m_reason;
    return {};
}

bool UnsupportedWindowManager::focusWindow(const QString &, QString *error)
{
    if (error) *error = m_reason;
    return false;
}

bool UnsupportedWindowManager::moveWindow(const QString &, const QRect &, QString *error)
{
    if (error) *error = m_reason;
    return false;
}

bool UnsupportedWindowManager::resizeWindow(const QString &, const QSize &, QString *error)
{
    if (error) *error = m_reason;
    return false;
}

QRect UnsupportedWindowManager::screenGeometry() const
{
    return {};
}

// ─────────────────────────────────────────────────────────────
//  Hyprland backend (hyprctl, no shell)
// ─────────────────────────────────────────────────────────────

HyprlandWindowManager::HyprlandWindowManager(bool available, QString reason)
    : m_available(available)
    , m_reason(std::move(reason))
{
    if (m_available
        && QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty()) {
        m_available = false;
        m_reason = QStringLiteral("hyprctl is not installed.");
    }
}

bool HyprlandWindowManager::available(QString *error) const
{
    if (!m_available) {
        if (error) *error = m_reason;
    }
    return m_available;
}

bool HyprlandWindowManager::runHyprctl(const QStringList &arguments, QByteArray *output,
                                       QString *error, int timeoutMs) const
{
    QProcess process;
    process.setProgram(QStringLiteral("hyprctl"));
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(1000)) {
        if (error) *error = QStringLiteral("Could not start hyprctl.");
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        if (error) *error = QStringLiteral("hyprctl timed out.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = processError(process);
        return false;
    }
    if (output) {
        *output = process.readAllStandardOutput();
    }
    return true;
}

bool HyprlandWindowManager::dispatchLua(const QString &expression, QString *error) const
{
    QByteArray output;
    if (!runHyprctl({QStringLiteral("dispatch"), expression}, &output, error)) {
        return false;
    }
    const QString text = QString::fromUtf8(output).trimmed();
    if (!text.startsWith(QLatin1String("ok"), Qt::CaseInsensitive)) {
        if (error) {
            *error = text.isEmpty() ? QStringLiteral("Hyprland rejected the dispatch request.")
                                    : text;
        }
        return false;
    }
    return true;
}

QList<WindowInfo> HyprlandWindowManager::listWindows(QString *error)
{
    if (!available(error)) {
        return {};
    }
    QByteArray output;
    if (!runHyprctl({QStringLiteral("-j"), QStringLiteral("clients")}, &output, error)) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isArray()) {
        if (error) *error = QStringLiteral("Hyprland returned invalid window data.");
        return {};
    }

    QList<WindowInfo> windows;
    for (const QJsonValue &value : doc.array()) {
        if (value.isObject()) {
            windows.append(windowFromJson(value.toObject()));
        }
    }
    return windows;
}

WindowInfo HyprlandWindowManager::activeWindow(QString *error)
{
    if (!available(error)) {
        return {};
    }
    QByteArray output;
    if (!runHyprctl({QStringLiteral("-j"), QStringLiteral("activewindow")}, &output, error)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Hyprland returned invalid active-window data.");
        return {};
    }
    return windowFromJson(doc.object());
}

bool HyprlandWindowManager::focusWindow(const QString &windowId, QString *error)
{
    if (!available(error)) {
        return false;
    }
    if (!isHyprlandAddress(windowId)) {
        if (error) *error = QStringLiteral("Invalid Hyprland window address.");
        return false;
    }

    QString luaError;
    if (dispatchLua(QStringLiteral("hl.dsp.focus({ window = \"address:%1\" })").arg(windowId),
                    &luaError)) {
        return true;
    }

    QByteArray output;
    QString legacyError;
    if (runHyprctl({QStringLiteral("dispatch"), QStringLiteral("focuswindow"),
                    QStringLiteral("address:") + windowId}, &output, &legacyError)) {
        const QString text = QString::fromUtf8(output).trimmed();
        if (text.startsWith(QLatin1String("ok"), Qt::CaseInsensitive)) {
            return true;
        }
        if (error) {
            *error = text.isEmpty() ? QStringLiteral("Hyprland rejected focus request.") : text;
        }
        return false;
    }
    if (error) *error = legacyError.isEmpty() ? luaError : legacyError;
    return false;
}

bool HyprlandWindowManager::moveWindow(const QString &windowId, const QRect &geometry,
                                       QString *error)
{
    if (!available(error)) {
        return false;
    }
    if (!isHyprlandAddress(windowId) || !geometry.isValid()) {
        if (error) *error = QStringLiteral("Invalid window address or geometry.");
        return false;
    }

    QString luaError;
    if (!dispatchLua(QStringLiteral("hl.dsp.window.move({ x = %1, y = %2, window = \"address:%3\" })")
                         .arg(geometry.x())
                         .arg(geometry.y())
                         .arg(windowId),
                     &luaError)) {
        QByteArray output;
        QString legacyError;
        if (!runHyprctl({QStringLiteral("dispatch"), QStringLiteral("movewindowpixel"),
                         QStringLiteral("exact"), QString::number(geometry.x()),
                         QStringLiteral("%1,address:%2").arg(geometry.y()).arg(windowId)},
                        &output, &legacyError)) {
            if (error) *error = legacyError.isEmpty() ? luaError : legacyError;
            return false;
        }
        const QString text = QString::fromUtf8(output).trimmed();
        if (!text.startsWith(QLatin1String("ok"), Qt::CaseInsensitive)) {
            if (error) *error = text.isEmpty() ? QStringLiteral("Hyprland rejected move request.")
                                               : text;
            return false;
        }
    }

    if (geometry.width() > 0 && geometry.height() > 0) {
        return resizeWindow(windowId, geometry.size(), error);
    }
    return true;
}

bool HyprlandWindowManager::resizeWindow(const QString &windowId, const QSize &size,
                                         QString *error)
{
    if (!available(error)) {
        return false;
    }
    if (!isHyprlandAddress(windowId) || !size.isValid()) {
        if (error) *error = QStringLiteral("Invalid window address or size.");
        return false;
    }

    QString luaError;
    if (dispatchLua(QStringLiteral("hl.dsp.window.resize({ x = %1, y = %2, window = \"address:%3\" })")
                        .arg(size.width())
                        .arg(size.height())
                        .arg(windowId),
                    &luaError)) {
        return true;
    }

    QByteArray output;
    QString legacyError;
    if (runHyprctl({QStringLiteral("dispatch"), QStringLiteral("resizewindowpixel"),
                    QStringLiteral("exact"), QString::number(size.width()),
                    QStringLiteral("%1,address:%2").arg(size.height()).arg(windowId)},
                   &output, &legacyError)) {
        const QString text = QString::fromUtf8(output).trimmed();
        if (text.startsWith(QLatin1String("ok"), Qt::CaseInsensitive)) {
            return true;
        }
        if (error) {
            *error = text.isEmpty() ? QStringLiteral("Hyprland rejected resize request.") : text;
        }
        return false;
    }
    if (error) *error = legacyError.isEmpty() ? luaError : legacyError;
    return false;
}

QRect HyprlandWindowManager::screenGeometry() const
{
    QString error;
    QByteArray output;
    if (!runHyprctl({QStringLiteral("-j"), QStringLiteral("monitors")}, &output, &error)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isArray()) {
        return {};
    }
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        if (!obj.value(QStringLiteral("focused")).toBool(false)) {
            continue;
        }
        return QRect(obj.value(QStringLiteral("x")).toInt(),
                     obj.value(QStringLiteral("y")).toInt(),
                     obj.value(QStringLiteral("width")).toInt(),
                     obj.value(QStringLiteral("height")).toInt());
    }
    const QJsonArray array = doc.array();
    if (!array.isEmpty() && array.first().isObject()) {
        const QJsonObject obj = array.first().toObject();
        return QRect(obj.value(QStringLiteral("x")).toInt(),
                     obj.value(QStringLiteral("y")).toInt(),
                     obj.value(QStringLiteral("width")).toInt(),
                     obj.value(QStringLiteral("height")).toInt());
    }
    return {};
}

// ─────────────────────────────────────────────────────────────
//  X11 backend (wmctrl + xprop, no shell)
// ─────────────────────────────────────────────────────────────

X11WmctrlWindowManager::X11WmctrlWindowManager(bool available, QString reason)
    : m_available(available)
    , m_reason(std::move(reason))
{
    if (m_available
        && QStandardPaths::findExecutable(QStringLiteral("wmctrl")).isEmpty()) {
        m_available = false;
        m_reason = QStringLiteral("wmctrl is not installed.");
    }
}

bool X11WmctrlWindowManager::available(QString *error) const
{
    if (!m_available) {
        if (error) *error = m_reason;
    }
    return m_available;
}

bool X11WmctrlWindowManager::runWmctrl(const QStringList &arguments, QByteArray *output,
                                       QString *error, int timeoutMs) const
{
    QProcess process;
    process.setProgram(QStringLiteral("wmctrl"));
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(1000)) {
        if (error) *error = QStringLiteral("Could not start wmctrl.");
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        if (error) *error = QStringLiteral("wmctrl timed out.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = processError(process);
        return false;
    }
    if (output) {
        *output = process.readAllStandardOutput();
    }
    return true;
}

QList<WindowInfo> X11WmctrlWindowManager::listWindows(QString *error)
{
    if (!available(error)) {
        return {};
    }
    QByteArray output;
    if (!runWmctrl({QStringLiteral("-l"), QStringLiteral("-x"), QStringLiteral("-G")},
                   &output, error)) {
        return {};
    }

    QList<WindowInfo> windows;
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'),
                                                              Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 8) {
            continue;
        }
        bool xOk = false;
        bool yOk = false;
        bool wOk = false;
        bool hOk = false;
        const int x = parts.at(2).toInt(&xOk);
        const int y = parts.at(3).toInt(&yOk);
        const int w = parts.at(4).toInt(&wOk);
        const int h = parts.at(5).toInt(&hOk);
        if (!xOk || !yOk || !wOk || !hOk) {
            continue;
        }

        WindowInfo window;
        window.id = parts.at(0);
        window.appClass = parts.at(7);
        window.appName = parts.at(7).section(QLatin1Char('.'), -1);
        window.title = parts.mid(8).join(QLatin1Char(' '));
        window.geometry = QRect(x, y, w, h);
        windows.append(window);
    }
    return windows;
}

WindowInfo X11WmctrlWindowManager::activeWindow(QString *error)
{
    QList<WindowInfo> windows = listWindows(error);
    if (!windows.isEmpty()) {
        QProcess xprop;
        xprop.setProgram(QStringLiteral("xprop"));
        xprop.setArguments({QStringLiteral("-root"), QStringLiteral("_NET_ACTIVE_WINDOW")});
        xprop.start();
        if (xprop.waitForStarted(1000) && xprop.waitForFinished(2000)
            && xprop.exitStatus() == QProcess::NormalExit && xprop.exitCode() == 0) {
            const QString text = QString::fromUtf8(xprop.readAllStandardOutput());
            const QRegularExpressionMatch match = QRegularExpression(
                QStringLiteral("(0x[0-9a-fA-F]+)")).match(text);
            if (match.hasMatch()) {
                for (const WindowInfo &window : windows) {
                    if (window.id.compare(match.captured(1), Qt::CaseInsensitive) == 0) {
                        WindowInfo active = window;
                        active.active = true;
                        return active;
                    }
                }
            }
        }
    }
    if (error) *error = QStringLiteral("Could not determine the active window.");
    return {};
}

bool X11WmctrlWindowManager::focusWindow(const QString &windowId, QString *error)
{
    if (!available(error)) {
        return false;
    }
    return runWmctrl({QStringLiteral("-i"), QStringLiteral("-a"), windowId}, nullptr, error);
}

bool X11WmctrlWindowManager::moveWindow(const QString &windowId, const QRect &geometry,
                                        QString *error)
{
    if (!available(error)) {
        return false;
    }
    if (!geometry.isValid()) {
        if (error) *error = QStringLiteral("Invalid geometry.");
        return false;
    }
    const QString spec = QStringLiteral("0,%1,%2,%3,%4")
                             .arg(geometry.x())
                             .arg(geometry.y())
                             .arg(geometry.width())
                             .arg(geometry.height());
    return runWmctrl({QStringLiteral("-i"), QStringLiteral("-r"), windowId,
                      QStringLiteral("-e"), spec}, nullptr, error);
}

bool X11WmctrlWindowManager::resizeWindow(const QString &windowId, const QSize &size,
                                          QString *error)
{
    if (!available(error)) {
        return false;
    }
    if (!size.isValid()) {
        if (error) *error = QStringLiteral("Invalid size.");
        return false;
    }
    const QString spec = QStringLiteral("0,-1,-1,%1,%2")
                             .arg(size.width())
                             .arg(size.height());
    return runWmctrl({QStringLiteral("-i"), QStringLiteral("-r"), windowId,
                      QStringLiteral("-e"), spec}, nullptr, error);
}

QRect X11WmctrlWindowManager::screenGeometry() const
{
    QString error;
    QByteArray output;
    if (!runWmctrl({QStringLiteral("-d")}, &output, &error)) {
        return {};
    }
    const QString text = QString::fromUtf8(output);
    const QRegularExpressionMatch wa = QRegularExpression(
        QStringLiteral("WA:\\s*(-?\\d+),(-?\\d+)\\s+(\\d+)x(\\d+)")).match(text);
    if (wa.hasMatch()) {
        return QRect(wa.captured(1).toInt(), wa.captured(2).toInt(),
                     wa.captured(3).toInt(), wa.captured(4).toInt());
    }
    const QRegularExpressionMatch dg = QRegularExpression(
        QStringLiteral("DG:\\s*(\\d+)x(\\d+)")).match(text);
    if (dg.hasMatch()) {
        return QRect(0, 0, dg.captured(1).toInt(), dg.captured(2).toInt());
    }
    return {};
}
