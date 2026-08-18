#include "agent/agent.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QTimer>

Agent::Agent(QObject *parent)
    : QObject(parent)
    , m_ollamaClient(this)
{
    connect(&m_ollamaClient, &OllamaClient::responseChunkReceived, this, &Agent::responseChunkReceived);
    connect(&m_ollamaClient, &OllamaClient::responseReceived, this, &Agent::responseReceived);
    connect(&m_ollamaClient, &OllamaClient::errorOccurred, this, &Agent::errorOccurred);
    connect(&m_ollamaClient, &OllamaClient::completionReceived, this, &Agent::onCompletionReceived);
    connect(&m_ollamaClient, &OllamaClient::completionError, this, &Agent::onCompletionError);

    connect(&m_ollamaManager, &OllamaManager::statusChanged, this, &Agent::modelStatusChanged);
    connect(&m_ollamaManager, &OllamaManager::modelReady, this, &Agent::onModelReady);
    connect(&m_ollamaManager, &OllamaManager::modelError, this, &Agent::modelError);

    connect(&m_packageManager, &PackageManager::outputReceived, this, &Agent::toolOutputReceived);
    connect(&m_packageManager, &PackageManager::finished, this, &Agent::onPackageManagerFinished);

    connect(&m_codeFixer, &CodeFixer::buildFinished, this, &Agent::onCodeFixBuildFinished);

    connect(&m_calendarManager, &CalendarManager::eventsUpdated, this, [this]() {
        const QList<CalendarEvent> upcoming = m_calendarManager.getUpcomingEvents(24);
        m_notificationManager.checkReminders(upcoming);
    });
    connect(&m_notificationManager, &NotificationManager::notificationTriggered, this,
            [this](const QString &title, const QString &message, const CalendarEvent &) {
                emit calendarNotificationAlert(title, message);
            });

    m_suggestionEngine.initialize(&m_taskTracker, &m_activityAnalyzer);
}

void Agent::initializeModel(const QString &model)
{
    m_ollamaManager.ensureModelReady(model);
}

void Agent::onModelReady(const QString &model)
{
    m_ollamaClient.setModel(model);
    m_ollamaClient.warmUp();
    emit modelReady(model);
}

void Agent::sendMessage(const QString &message)
{
    if (handleSystemInfoQuery(message)) {
        return;
    }

    if (handleCameraQuery(message)) {
        return;
    }

    if (handleCalendarQuery(message)) {
        return;
    }

    if (handlePackageInstallQuery(message)) {
        return;
    }

    if (handleAutoFixToggleQuery(message)) {
        return;
    }

    if (handleCodeFixRequestQuery(message)) {
        return;
    }

    m_ollamaClient.sendPrompt(message);
}

void Agent::performInstall(const QStringList &packages)
{
    m_packageManager.install(packages);
}

void Agent::sendImageMessage(const QImage &image, const QString &text)
{
    if (image.isNull()) {
        m_ollamaClient.sendPrompt(text);
        return;
    }

    QImage scaled = image;
    constexpr int kMaxDim = 1024;
    const int maxSide = qMax(scaled.width(), scaled.height());
    if (maxSide > kMaxDim) {
        scaled = scaled.scaled(
            kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "JPEG", 85);

    m_ollamaClient.sendImagePrompt(text, {bytes.toBase64()});
}

void Agent::setAutoFixEnabled(bool enabled)
{
    if (m_autoFixEnabled == enabled) {
        return;
    }
    m_autoFixEnabled = enabled;
    emit autoFixEnabledChanged(enabled);
}

bool Agent::autoFixEnabled() const
{
    return m_autoFixEnabled;
}

void Agent::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = directory.trimmed();
}

void Agent::setBuildCommand(const QString &command)
{
    m_buildCommand = command.trimmed();
}

bool Agent::isCodeFixBusy() const
{
    return m_codeFixInProgress || m_codeFixer.isBusy();
}

void Agent::runBuildAndFix()
{
    if (m_codeFixInProgress) {
        emit codeFixFinished(QStringLiteral("A code-fix operation is already running."), false);
        return;
    }
    startBuildFix();
}

void Agent::onPackageManagerFinished(bool success, const QString &summary)
{
    if (success) {
        emit responseReceived(summary);
    } else {
        emit errorOccurred(summary);
    }
}

bool Agent::handleAutoFixToggleQuery(const QString &message)
{
    QString lower = message.toLower().simplified();
    if (!lower.contains(QStringLiteral("auto fix")) && !lower.contains(QStringLiteral("autofix"))) {
        return false;
    }

    if (lower.contains(QStringLiteral("is")) && lower.contains(QStringLiteral("enabled"))) {
        emit responseReceived(
            QStringLiteral("Auto-fix code errors is currently %1.")
                .arg(m_autoFixEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled")));
        return true;
    }

    bool enable = lower.contains(QStringLiteral("enable")) ||
                  lower.contains(QStringLiteral("turn on")) ||
                  lower.contains(QStringLiteral("start")) ||
                  lower.contains(QStringLiteral(" on"));
    bool disable = lower.contains(QStringLiteral("disable")) ||
                   lower.contains(QStringLiteral("turn off")) ||
                   lower.contains(QStringLiteral("deactivate")) ||
                   lower.contains(QStringLiteral(" off"));

    if (enable && !disable) {
        setAutoFixEnabled(true);
        emit responseReceived(
            QStringLiteral("Auto-fix code errors is enabled. Paste build errors into the chat, "
                           "say 'fix my build', or click 'Build && Fix'."));
        return true;
    }
    if (disable) {
        setAutoFixEnabled(false);
        emit responseReceived(QStringLiteral("Auto-fix code errors is disabled."));
        return true;
    }

    emit responseReceived(
        QStringLiteral("Auto-fix code errors: currently %1. When enabled, I detect build errors "
                       "from pasted output or your build command and automatically apply fixes.")
            .arg(m_autoFixEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled")));
    return true;
}

bool Agent::handleCodeFixRequestQuery(const QString &message)
{
    QString lower = message.toLower();

    bool explicitFix = false;
    if (!isInformationalFixQuery(lower)) {
        explicitFix = (lower.contains(QStringLiteral("fix")) ||
                       lower.contains(QStringLiteral("repair"))) &&
                      (lower.contains(QStringLiteral("error")) ||
                       lower.contains(QStringLiteral("build")) ||
                       lower.contains(QStringLiteral("compile")) ||
                       lower.contains(QStringLiteral("code")));
    }

    bool hasErrors = looksLikeErrorOutput(message);

    if (!explicitFix && !hasErrors) {
        return false;
    }

    if (m_codeFixInProgress) {
        emit responseReceived(QStringLiteral("A code-fix operation is already running. Please wait."));
        return true;
    }

    if (!m_autoFixEnabled) {
        if (explicitFix) {
            emit responseReceived(
                QStringLiteral("Auto-fix code errors is disabled. Enable it by saying "
                               "'enable auto fix' or with the checkbox in the window, then try again."));
        }
        return explicitFix;
    }

    if (hasErrors) {
        startPasteFix(message);
    } else {
        startBuildFix();
    }
    return true;
}

void Agent::startPasteFix(const QString &message)
{
    const QList<CodeFixer::BuildError> errors = CodeFixer::parseErrors(message);
    if (errors.isEmpty()) {
        emit codeFixFinished(
            QStringLiteral("No parseable error locations were found in the pasted output."), false);
        return;
    }
    m_codeFixInProgress = true;
    emit codeFixStatus(QStringLiteral("Found %1 error(s). Analyzing and fixing...").arg(errors.size()));
    requestFix(errors);
}

void Agent::startBuildFix()
{
    if (m_buildCommand.isEmpty()) {
        emit codeFixFinished(
            QStringLiteral("No build command is configured. Set a project directory and build "
                           "command in the window (e.g. 'cmake --build build' or 'npm run build'), "
                           "or paste the error output directly into the chat."),
            false);
        return;
    }
    m_codeFixInProgress = true;
    emit codeFixStatus(QStringLiteral("Running build: %1").arg(m_buildCommand));
    m_codeFixer.runBuild(m_projectDirectory, m_buildCommand);
}

void Agent::onCodeFixBuildFinished(bool success, const QString &output)
{
    if (success) {
        m_codeFixInProgress = false;
        emit codeFixFinished(QStringLiteral("Build succeeded with no errors."), true);
        return;
    }

    const QList<CodeFixer::BuildError> errors = CodeFixer::parseErrors(output);
    if (errors.isEmpty()) {
        m_codeFixInProgress = false;
        emit codeFixFinished(
            QStringLiteral("Build failed, but no error locations could be parsed from the output."),
            false);
        return;
    }

    emit codeFixStatus(QStringLiteral("Found %1 error(s). Analyzing and fixing...").arg(errors.size()));
    requestFix(errors);
}

void Agent::requestFix(const QList<CodeFixer::BuildError> &errors)
{
    const QString root = m_projectDirectory.isEmpty()
                             ? CodeFixer::resolveProjectRoot(QDir::currentPath())
                             : m_projectDirectory;

    QList<CodeFixer::BuildError> resolved = errors;
    for (CodeFixer::BuildError &error : resolved) {
        error.file = resolveFilePath(error.file);
    }

    emit codeFixStatus(QStringLiteral("Asking the model to fix the errors..."));
    m_ollamaClient.requestCompletion(buildFixPrompt(resolved));
}

void Agent::onCompletionReceived(const QString &response)
{
    applyFixes(response);
}

void Agent::onCompletionError(const QString &error)
{
    m_codeFixInProgress = false;
    emit codeFixFinished(QStringLiteral("Model request failed. %1").arg(error), false);
}

void Agent::applyFixes(const QString &llmOutput)
{
    m_codeFixInProgress = false;

    const QList<CodeFixer::FixEdit> edits = CodeFixer::parseFixes(llmOutput);
    if (edits.isEmpty()) {
        const QString snippet = llmOutput.left(400).simplified();
        emit codeFixFinished(
            QStringLiteral("The model did not return a parseable fix. It replied: \"%1\"")
                .arg(snippet),
            false);
        return;
    }

    const QString root = m_projectDirectory.isEmpty()
                             ? CodeFixer::resolveProjectRoot(QDir::currentPath())
                             : m_projectDirectory;

    QStringList applied;
    QStringList skipped;
    for (const CodeFixer::FixEdit &edit : edits) {
        QString absPath = edit.file;
        if (!QDir::isAbsolutePath(absPath)) {
            absPath = QDir(root).filePath(absPath);
        }

        QString error;
        if (CodeFixer::applyEdit(absPath, edit, &error)) {
            applied.append(QStringLiteral("%1 (lines %2-%3)").arg(absPath).arg(edit.startLine).arg(edit.endLine));
        } else {
            skipped.append(QStringLiteral("%1 (%2)").arg(absPath, error));
        }
    }

    if (!applied.isEmpty()) {
        QString summary = QStringLiteral("Applied %1 fix(es):\n  %2")
                              .arg(applied.size())
                              .arg(applied.join(QStringLiteral("\n  ")));
        if (!skipped.isEmpty()) {
            summary += QStringLiteral("\n\nSkipped %1:\n  %2")
                           .arg(skipped.size())
                           .arg(skipped.join(QStringLiteral("\n  ")));
        }
        summary += QStringLiteral("\n\nBackups were saved as '<file>.bak' before each edit.");
        emit codeFixFinished(summary, true);
    } else {
        emit codeFixFinished(
            QStringLiteral("No fixes could be applied.\n%1").arg(skipped.join(QStringLiteral("\n"))),
            false);
    }
}

QString Agent::buildFixPrompt(const QList<CodeFixer::BuildError> &errors) const
{
    QMap<QString, QList<CodeFixer::BuildError>> byFile;
    for (const CodeFixer::BuildError &error : errors) {
        byFile[error.file].append(error);
    }

    QString prompt;
    prompt += QStringLiteral(
        "You are an expert developer fixing build errors in the user's local project. "
        "Apply the minimal correct fix for each reported error.\n\n"
        "Output ONLY fix blocks in this exact format, one per replacement, with no extra text:\n"
        "FILE: <absolute path>\n"
        "REPLACE_LINES: <first-line>-<last-line>\n"
        "<replacement code exactly as it should appear in the file, without line numbers>\n"
        "END\n\n"
        "Rules:\n"
        "- The line numbers in the context refer to the current file content.\n"
        "- Preserve the original indentation exactly.\n"
        "- If a file needs several disjoint replacements, output separate blocks.\n"
        "- Do not include line numbers or any explanation.\n\n");

    prompt += QStringLiteral("Errors:\n");
    int index = 1;
    for (const CodeFixer::BuildError &error : errors) {
        prompt += QStringLiteral("%1. %2:%3:%4: error: %5\n")
                      .arg(index++)
                      .arg(error.file)
                      .arg(error.line)
                      .arg(error.column > 0 ? error.column : 1)
                      .arg(error.message);
    }

    prompt += QStringLiteral("\nRelevant code context (line numbers are for reference only):\n");
    for (auto it = byFile.constBegin(); it != byFile.constEnd(); ++it) {
        prompt += CodeFixer::readContextForFile(it.key(), it.value());
        prompt += QLatin1Char('\n');
    }

    return prompt;
}

QString Agent::resolveFilePath(const QString &path) const
{
    if (QDir::isAbsolutePath(path)) {
        return path;
    }

    const QString root = m_projectDirectory.isEmpty()
                             ? CodeFixer::resolveProjectRoot(QDir::currentPath())
                             : m_projectDirectory;

    QString candidate = QDir(root).filePath(path);
    if (QFile::exists(candidate)) {
        return candidate;
    }

    const QString cwdCandidate = QDir(QDir::currentPath()).filePath(path);
    if (QFile::exists(cwdCandidate)) {
        return cwdCandidate;
    }

    return candidate;
}

bool Agent::isInformationalFixQuery(const QString &lowerMessage)
{
    static const QStringList hints = {
        QStringLiteral("how to fix"),     QStringLiteral("how do i fix"),
        QStringLiteral("how can i fix"),  QStringLiteral("steps to fix"),
        QStringLiteral("guide"),          QStringLiteral("tutorial"),
        QStringLiteral("explain"),        QStringLiteral("what does"),
        QStringLiteral("what is"),        QStringLiteral("what command"),
        QStringLiteral("show me"),        QStringLiteral("mean"),
        QStringLiteral("why is"),         QStringLiteral("why does"),
        QStringLiteral("best practice"),  QStringLiteral("example"),
        QStringLiteral("documentation"),
    };

    for (const QString &hint : hints) {
        if (lowerMessage.contains(hint)) {
            return true;
        }
    }
    return false;
}

bool Agent::looksLikeErrorOutput(const QString &message)
{
    if (!CodeFixer::parseErrors(message).isEmpty()) {
        return true;
    }
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("error:")) ||
           lower.contains(QStringLiteral("fatal error")) ||
           lower.contains(QStringLiteral("error["));
}

bool Agent::handlePackageInstallQuery(const QString &message)
{
    QString lower = message.toLower();

    if (isInformationalQuery(lower)) {
        return false;
    }

    if (looksLikeErrorOutput(message)) {
        return false;
    }

    static const QRegularExpression installRe(QStringLiteral("\\binstall\\b"));
    if (!installRe.match(lower).hasMatch()) {
        return false;
    }

    QStringList packages = extractPackageNames(message);
    if (packages.isEmpty()) {
        return false;
    }

    emit installRequested(packages);
    return true;
}

bool Agent::isInformationalQuery(const QString &lowerMessage)
{
    static const QStringList hints = {
        QStringLiteral("how to install"),
        QStringLiteral("how do i"),
        QStringLiteral("how can i"),
        QStringLiteral("steps to install"),
        QStringLiteral("guide"),
        QStringLiteral("tutorial"),
        QStringLiteral("explain"),
        QStringLiteral("what is"),
        QStringLiteral("what command"),
        QStringLiteral("show me"),
        QStringLiteral("documentation"),
    };

    for (const QString &hint : hints) {
        if (lowerMessage.contains(hint)) {
            return true;
        }
    }

    return false;
}

QStringList Agent::extractPackageNames(const QString &message) const
{
    QString lower = message.toLower();

    static const QRegularExpression installRe(QStringLiteral("\\binstall\\b"));
    QRegularExpressionMatch match = installRe.match(lower);
    if (!match.hasMatch()) {
        return {};
    }

    QString rest = message.mid(match.capturedEnd()).simplified();

    static const QStringList stopWords = {
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("with"),
        QStringLiteral("without"), QStringLiteral("for"), QStringLiteral("of"),
        QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("from"),
        QStringLiteral("by"), QStringLiteral("at"), QStringLiteral("as"),
        QStringLiteral("into"), QStringLiteral("onto"), QStringLiteral("over"),
        QStringLiteral("under"), QStringLiteral("up"), QStringLiteral("down"),
        QStringLiteral("out"), QStringLiteral("off"), QStringLiteral("if"),
        QStringLiteral("to"), QStringLiteral("is"), QStringLiteral("are"),
        QStringLiteral("was"), QStringLiteral("were"), QStringLiteral("has"),
        QStringLiteral("have"), QStringLiteral("had"), QStringLiteral("do"),
        QStringLiteral("does"), QStringLiteral("did"), QStringLiteral("can"),
        QStringLiteral("could"), QStringLiteral("would"), QStringLiteral("should"),
        QStringLiteral("will"), QStringLiteral("i"), QStringLiteral("we"),
        QStringLiteral("you"), QStringLiteral("it"), QStringLiteral("this"),
        QStringLiteral("that"), QStringLiteral("these"), QStringLiteral("those"),
        QStringLiteral("they"), QStringLiteral("them"), QStringLiteral("your"),
        QStringLiteral("my"), QStringLiteral("us"), QStringLiteral("me"),
        QStringLiteral("our"), QStringLiteral("their"), QStringLiteral("please"),
        QStringLiteral("thanks"), QStringLiteral("thank"), QStringLiteral("want"),
        QStringLiteral("wanted"), QStringLiteral("need"), QStringLiteral("needed"),
        QStringLiteral("like"), QStringLiteral("using"), QStringLiteral("use"),
        QStringLiteral("used"), QStringLiteral("get"), QStringLiteral("help"),
        QStringLiteral("just"), QStringLiteral("then"), QStringLiteral("now"),
        QStringLiteral("also"), QStringLiteral("too"), QStringLiteral("very"),
        QStringLiteral("only"), QStringLiteral("currently"), QStringLiteral("maybe"),
        QStringLiteral("probably"), QStringLiteral("there"), QStringLiteral("here"),
        QStringLiteral("some"), QStringLiteral("any"), QStringLiteral("more"),
        QStringLiteral("most"), QStringLiteral("best"), QStringLiteral("new"),
        QStringLiteral("latest"), QStringLiteral("version"), QStringLiteral("versions"),
        QStringLiteral("install"), QStringLiteral("installing"), QStringLiteral("installed"),
        QStringLiteral("installation"), QStringLiteral("installer"),
        QStringLiteral("package"), QStringLiteral("packages"), QStringLiteral("software"),
        QStringLiteral("app"), QStringLiteral("apps"), QStringLiteral("application"),
        QStringLiteral("applications"), QStringLiteral("system"), QStringLiteral("computer"),
        QStringLiteral("machine"), QStringLiteral("desktop"), QStringLiteral("device"),
        QStringLiteral("laptop"), QStringLiteral("following"), QStringLiteral("all"),
    };

    static const QRegularExpression firstWordRe(QStringLiteral("^\\S+"));
    static const QRegularExpression trailingDescriptorRe(
        QStringLiteral("(?:^|\\s+)(?:browser|browsers|software|program|programs|tool|tools|"
                       "client|clients|player|players|viewer|viewers|editor|editors|shell|"
                       "extension|extensions|plugin|plugins|addon|addons|package|packages|"
                       "app|apps|application|applications|media|manager|gui|latest|version|"
                       "versions|suite|utility|utilities|text)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    bool stripped = true;
    while (stripped) {
        stripped = false;
        QRegularExpressionMatch firstMatch = firstWordRe.match(rest);
        if (!firstMatch.hasMatch()) {
            break;
        }
        QString first = firstMatch.captured();
        if (stopWords.contains(first.toLower())) {
            rest = rest.mid(first.length()).simplified();
            stripped = true;
        }
    }

    while (true) {
        QRegularExpressionMatch descMatch = trailingDescriptorRe.match(rest);
        if (!descMatch.hasMatch()) {
            break;
        }
        rest = rest.left(descMatch.capturedStart()).simplified();
        if (rest.isEmpty()) {
            break;
        }
    }

    static const QRegularExpression tokenRe(QStringLiteral("[\\s,;]+"));
    static const QRegularExpression validName(QStringLiteral("^[a-z0-9][a-z0-9@._+\\-]*$"));

    QStringList packages;
    const QStringList tokens = rest.split(tokenRe, Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token.startsWith(QLatin1Char('-'))) {
            continue;
        }

        QString word = token;
        while (!word.isEmpty() && word.back().isPunct() && word.back() != QLatin1Char('-')) {
            word.chop(1);
        }
        while (!word.isEmpty() && word.front().isPunct()) {
            word = word.mid(1);
        }

        QString lowered = word.toLower();
        if (lowered.isEmpty() || stopWords.contains(lowered)) {
            continue;
        }

        if (!validName.match(lowered).hasMatch()) {
            continue;
        }

        if (!packages.contains(lowered)) {
            packages.append(lowered);
        }

        if (packages.size() >= 8) {
            break;
        }
    }

    return packages;
}

bool Agent::handleSystemInfoQuery(const QString &message)
{
    QString lower = message.toLower().trimmed();
    SystemInfo info = m_systemInfoTool.getSystemInfo();

    QString response;

    bool isOsQuery = lower.contains(QStringLiteral("operating system")) ||
                     lower.contains(QStringLiteral("linux distribution")) ||
                     lower.contains(QStringLiteral("distro")) ||
                     lower.contains(QStringLiteral("os am i running")) ||
                     lower.contains(QStringLiteral("what os")) ||
                     lower.contains(QStringLiteral("which os"));

    bool isKernelQuery = lower.contains(QStringLiteral("kernel"));

    bool isCpuQuery = lower.contains(QStringLiteral("cpu")) ||
                      lower.contains(QStringLiteral("processor"));

    bool isAvailRamQuery = lower.contains(QStringLiteral("available")) ||
                           lower.contains(QStringLiteral("free"));

    bool isRamQuery = lower.contains(QStringLiteral("ram")) ||
                      lower.contains(QStringLiteral("memory"));

    bool isGeneralSysQuery = lower.contains(QStringLiteral("system info")) ||
                             lower.contains(QStringLiteral("system specs")) ||
                             lower.contains(QStringLiteral("my system")) ||
                             lower.contains(QStringLiteral("sysinfo")) ||
                             lower.contains(QStringLiteral("system summary"));

    if (isGeneralSysQuery) {
        double totalGB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double availGB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

        response = QStringLiteral("System Information:\n") +
                   QStringLiteral("  Operating System: %1\n").arg(info.operatingSystem) +
                   QStringLiteral("  Kernel: %1 (%2)\n").arg(info.kernelVersion, info.architecture) +
                   QStringLiteral("  Hostname: %1\n").arg(info.hostname) +
                   QStringLiteral("  CPU: %1 (%2 cores)\n").arg(info.cpuModel).arg(info.cpuCoreCount) +
                   QStringLiteral("  Total Memory: %1 GB\n").arg(QString::number(totalGB, 'f', 2)) +
                   QStringLiteral("  Available Memory: %1 GB").arg(QString::number(availGB, 'f', 2));
    } else if (isOsQuery) {
        response = QStringLiteral("Operating System: %1").arg(info.operatingSystem);
    } else if (isKernelQuery) {
        response = QStringLiteral("Kernel Version: %1 (%2)").arg(info.kernelVersion, info.architecture);
    } else if (isCpuQuery) {
        response = QStringLiteral("CPU: %1 (%2 cores)").arg(info.cpuModel).arg(info.cpuCoreCount);
    } else if (isRamQuery && isAvailRamQuery) {
        double availGB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double availMB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0);
        response = QStringLiteral("Available RAM: %1 GB (%2 MB)").arg(QString::number(availGB, 'f', 2), QString::number(availMB, 'f', 0));
    } else if (isRamQuery) {
        double totalGB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double totalMB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0);
        response = QStringLiteral("Total RAM: %1 GB (%2 MB)").arg(QString::number(totalGB, 'f', 2), QString::number(totalMB, 'f', 0));
    } else {
        return false;
    }

    QTimer::singleShot(0, this, [this, response]() {
        emit responseReceived(response);
    });

    return true;
}

bool Agent::handleCameraQuery(const QString &message)
{
    const QString lower = message.toLower().simplified();

    static const QStringList phrases = {
        QStringLiteral("open camera"),
        QStringLiteral("open the camera"),
        QStringLiteral("open webcam"),
        QStringLiteral("open the webcam"),
        QStringLiteral("start camera"),
        QStringLiteral("start the camera"),
        QStringLiteral("turn on camera"),
        QStringLiteral("turn on the camera"),
        QStringLiteral("launch camera"),
        QStringLiteral("use camera"),
        QStringLiteral("show camera"),
        QStringLiteral("take a photo"),
        QStringLiteral("take photo"),
        QStringLiteral("take a picture"),
        QStringLiteral("take picture"),
        QStringLiteral("capture image"),
        QStringLiteral("capture a photo"),
    };

    static const QStringList informationalHints = {
        QStringLiteral("how to"), QStringLiteral("how do i"), QStringLiteral("how can i"),
        QStringLiteral("what is"), QStringLiteral("what are"), QStringLiteral("explain"),
        QStringLiteral("guide"), QStringLiteral("tutorial"), QStringLiteral("documentation"),
    };

    for (const QString &hint : informationalHints) {
        if (lower.contains(hint)) {
            return false;
        }
    }

    for (const QString &phrase : phrases) {
        if (lower.contains(phrase)) {
            QTimer::singleShot(0, this, [this]() { emit cameraRequested(); });
            return true;
        }
    }

    return false;
}

void Agent::startLearning()
{
    m_taskTracker.startTracking();
    emit learningStarted();
}

void Agent::stopLearning()
{
    m_taskTracker.stopTracking();
    emit learningStopped();
}

bool Agent::isLearning() const
{
    return m_taskTracker.isTracking();
}

QString Agent::getStartupSuggestions() const
{
    QList<TaskEntry> entries = m_taskTracker.recentEntries(500);

    ActivityAnalyzer analyzer;
    analyzer.analyze(entries);

    SuggestionEngine engine;
    engine.initialize(const_cast<TaskTracker *>(&m_taskTracker), &analyzer);

    const QList<Suggestion> suggestions = engine.generateStartupSuggestions();
    return engine.formatSuggestions(suggestions);
}

void Agent::refreshSuggestions()
{
    QList<TaskEntry> entries = m_taskTracker.recentEntries(500);
    m_activityAnalyzer.analyze(entries);
    const QList<Suggestion> suggestions = m_suggestionEngine.generateStartupSuggestions();
    const QString formatted = m_suggestionEngine.formatSuggestions(suggestions);
    emit startupSuggestionsReady(formatted);
}

void Agent::startCalendar()
{
    m_calendarManager.loadAllCalendars();
    const QList<CalendarEvent> upcoming = m_calendarManager.getUpcomingEvents(24);
    m_notificationManager.checkReminders(upcoming);

    if (!m_calendarManager.calendarFiles().isEmpty()) {
        const QString summary = m_calendarManager.formatUpcomingSummary();
        emit calendarEventsReady(summary);
    }
}

CalendarManager &Agent::calendarManager()
{
    return m_calendarManager;
}

NotificationManager &Agent::notificationManager()
{
    return m_notificationManager;
}

bool Agent::handleCalendarQuery(const QString &message)
{
    const QString lower = message.toLower().simplified();

    static const QStringList calendarKeywords = {
        QStringLiteral("schedule"),
        QStringLiteral("events"),
        QStringLiteral("meeting"),
        QStringLiteral("meetings"),
        QStringLiteral("calendar"),
        QStringLiteral("today"),
        QStringLiteral("upcoming"),
        QStringLiteral("agenda"),
        QStringLiteral("appointment"),
    };

    bool isCalendarQuery = false;
    for (const QString &keyword : calendarKeywords) {
        if (lower.contains(keyword)) {
            isCalendarQuery = true;
            break;
        }
    }

    if (!isCalendarQuery) {
        return false;
    }

    static const QStringList informationalHints = {
        QStringLiteral("how to"),
        QStringLiteral("how do i"),
        QStringLiteral("what is"),
        QStringLiteral("explain"),
        QStringLiteral("guide"),
        QStringLiteral("tutorial"),
    };
    for (const QString &hint : informationalHints) {
        if (lower.contains(hint)) {
            return false;
        }
    }

    QString response;
    if (lower.contains(QStringLiteral("today"))) {
        const QList<CalendarEvent> today = m_calendarManager.getTodaysEvents();
        if (today.isEmpty()) {
            response = QStringLiteral("No events scheduled for today.");
        } else {
            response = QStringLiteral("**Today's Schedule:**\n\n") + m_calendarManager.formatEventList(today);
        }
    } else if (lower.contains(QStringLiteral("next")) || lower.contains(QStringLiteral("upcoming"))) {
        response = m_calendarManager.formatUpcomingSummary();
    } else {
        const QList<CalendarEvent> today = m_calendarManager.getTodaysEvents();
        const QList<CalendarEvent> upcoming = m_calendarManager.getUpcomingEvents(48);

        if (today.isEmpty() && upcoming.isEmpty()) {
            response = m_calendarManager.calendarFiles().isEmpty()
                           ? QStringLiteral("No calendar files are configured. Add an ICS file via Calendar Settings.")
                           : QStringLiteral("No upcoming events found in your calendars.");
        } else {
            if (!today.isEmpty()) {
                response += QStringLiteral("**Today:**\n") + m_calendarManager.formatEventList(today) + QStringLiteral("\n");
            }
            if (!upcoming.isEmpty()) {
                response += QStringLiteral("**Upcoming (48 hours):**\n") + m_calendarManager.formatEventList(upcoming);
            }
        }
    }

    QTimer::singleShot(0, this, [this, response]() {
        emit responseReceived(response);
    });

    return true;
}
