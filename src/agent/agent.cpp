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

    // UI Developer
    connect(&m_uiDeveloper, &UiDeveloper::progress, this, &Agent::uiDevelopmentProgress);
    connect(&m_uiDeveloper, &UiDeveloper::finished, this, &Agent::uiDevelopmentFinished);
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

    if (handleFileOrganizationQuery(message)) {
        return;
    }

    if (handleDiskCleanupQuery(message)) {
        return;
    }

    if (handleUpdateCheckerQuery(message)) {
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

    if (handleUiDevelopmentQuery(message)) {
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
    constexpr int kMaxDim = 512;
    const int maxSide = qMax(scaled.width(), scaled.height());
    if (maxSide > kMaxDim) {
        scaled = scaled.scaled(
            kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "JPEG", 85);

    QString visionModel = m_ollamaManager.visionModel();
    if (visionModel.isEmpty() && OllamaManager::isVisionCapable(m_ollamaClient.model())) {
        visionModel = m_ollamaClient.model();
    }

    if (!visionModel.isEmpty()) {
        m_ollamaClient.sendImagePrompt(text, {bytes.toBase64()}, visionModel);
    } else {
        emit toolOutputReceived(QStringLiteral("Notice: Model '%1' is text-only. Processing text prompt without image.")
            .arg(m_ollamaClient.model()));
        m_ollamaClient.sendPrompt(text);
    }
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
    QString lower = message.toLower().simplified();

    static const QStringList kExplicitBuildFixPhrases = {
        QStringLiteral("fix my build"),
        QStringLiteral("fix build errors"),
        QStringLiteral("fix build error"),
        QStringLiteral("fix the build"),
        QStringLiteral("fix compiler errors"),
        QStringLiteral("fix compilation errors"),
        QStringLiteral("run build and fix"),
        QStringLiteral("auto fix build"),
        QStringLiteral("autofix build"),
        QStringLiteral("fix my project"),
        QStringLiteral("fix project errors"),
    };

    bool explicitBuildFix = false;
    for (const QString &phrase : kExplicitBuildFixPhrases) {
        if (lower.contains(phrase)) {
            explicitBuildFix = true;
            break;
        }
    }

    const QList<CodeFixer::BuildError> parsedErrors = CodeFixer::parseErrors(message);
    const bool hasParsedErrors = !parsedErrors.isEmpty();

    if (!explicitBuildFix && !hasParsedErrors) {
        return false;
    }

    if (m_codeFixInProgress) {
        emit responseReceived(QStringLiteral("A code-fix operation is already running. Please wait."));
        return true;
    }

    if (!m_autoFixEnabled) {
        if (hasParsedErrors) {
            emit responseReceived(
                QStringLiteral("Detected build error output, but auto-fix is disabled. "
                               "Enable it by saying 'enable auto fix' or with the checkbox in the Developer Hub."));
            return true;
        }
        if (explicitBuildFix) {
            emit responseReceived(
                QStringLiteral("Auto-fix code errors is disabled. Enable it by saying "
                               "'enable auto fix' or with the checkbox in the Developer Hub, then try again."));
            return true;
        }
        return false;
    }

    if (hasParsedErrors) {
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
    if (m_codeFixInProgress) {
        applyFixes(response);
    }
}

void Agent::onCompletionError(const QString &error)
{
    if (m_codeFixInProgress) {
        m_codeFixInProgress = false;
        emit codeFixFinished(QStringLiteral("Model request failed. %1").arg(error), false);
    }
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

FileOrganizer &Agent::fileOrganizer()
{
    return m_fileOrganizer;
}

DiskCleanup &Agent::diskCleanup()
{
    return m_diskCleanup;
}

UpdateChecker &Agent::updateChecker()
{
    return m_updateChecker;
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

bool Agent::handleFileOrganizationQuery(const QString &message)
{
    const QString lower = message.toLower().simplified();

    static const QStringList informationalHints = {
        QStringLiteral("how to"),
        QStringLiteral("how do i"),
        QStringLiteral("how can i"),
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

    const bool wantsDuplicates = lower.contains(QStringLiteral("duplicate")) ||
                                 lower.contains(QStringLiteral("duplicates")) ||
                                 lower.contains(QStringLiteral("dupe"));

    const bool wantsStructure = lower.contains(QStringLiteral("folder structure")) ||
                                lower.contains(QStringLiteral("directory structure")) ||
                                lower.contains(QStringLiteral("organize")) ||
                                lower.contains(QStringLiteral("organise")) ||
                                lower.contains(QStringLiteral("tidy"));

    if (!wantsDuplicates && !wantsStructure) {
        return false;
    }

    static const QStringList fileContextWords = {
        QStringLiteral("file"),      QStringLiteral("files"),
        QStringLiteral("folder"),    QStringLiteral("folders"),
        QStringLiteral("directory"), QStringLiteral("directories"),
        QStringLiteral("project"),   QStringLiteral("downloads"),
        QStringLiteral("documents"),
    };
    bool hasFileContext = false;
    for (const QString &word : fileContextWords) {
        if (lower.contains(word)) {
            hasFileContext = true;
            break;
        }
    }
    if (!hasFileContext) {
        return false;
    }

    if (m_fileOrganizer.isScanning()) {
        emit responseReceived(
            QStringLiteral("A file organization scan is already running. The report will appear "
                           "here when it finishes."));
        return true;
    }

    QString directory;

    static const QRegularExpression pathRe(QStringLiteral("(/[~\\w./+-]+)"));
    QRegularExpressionMatch match = pathRe.match(message);
    if (match.hasMatch()) {
        directory = match.captured(1);
    } else {
        static const QRegularExpression homeRe(QStringLiteral("(~/[\\w./+-]+)"));
        QRegularExpressionMatch homeMatch = homeRe.match(message);
        if (homeMatch.hasMatch()) {
            directory = QDir::homePath() + homeMatch.captured(1).mid(1);
        }
    }

    if (!directory.isEmpty()) {
        while (directory.size() > 1 && directory.endsWith(QLatin1Char('/'))) {
            directory.chop(1);
        }
        static const QRegularExpression trailingPunctRe(QStringLiteral("[.,!?;:]+$"));
        directory.remove(trailingPunctRe);

        if (!QDir(directory).exists()) {
            emit errorOccurred(
                QStringLiteral("Directory not found: %1. Set a valid Project path or mention an "
                               "existing directory.")
                    .arg(directory));
            return true;
        }
    } else if (!m_projectDirectory.isEmpty()) {
        directory = m_projectDirectory;
    } else {
        directory = QDir::homePath();
    }

    m_fileOrganizer.startScan(directory);

    emit responseReceived(
        QStringLiteral("Scanning '%1' for duplicate files and organization ideas. "
                       "The full report will appear here shortly.")
            .arg(directory));

    return true;
}

bool Agent::handleDiskCleanupQuery(const QString &message)
{
    const QString lower = message.toLower().simplified();

    static const QStringList informationalHints = {
        QStringLiteral("how to"),
        QStringLiteral("how do i"),
        QStringLiteral("how can i"),
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

    const bool mentionsDisk = lower.contains(QStringLiteral("disk")) ||
                              lower.contains(QStringLiteral("storage"));
    const bool wantsUsageReport = mentionsDisk &&
                                  (lower.contains(QStringLiteral("usage")) ||
                                   lower.contains(QStringLiteral("space")) ||
                                   lower.contains(QStringLiteral("full")));
    const bool wantsCleanup = lower.contains(QStringLiteral("cleanup")) ||
                              lower.contains(QStringLiteral("clean up")) ||
                              lower.contains(QStringLiteral("clean-up")) ||
                              lower.contains(QStringLiteral("tidy up"));
    const bool wantsFreeSpace = lower.contains(QStringLiteral("free up")) ||
                                lower.contains(QStringLiteral("free space")) ||
                                lower.contains(QStringLiteral("release space"));
    const bool wantsCacheClean = lower.contains(QStringLiteral("cache")) &&
                                 (lower.contains(QStringLiteral("clean")) ||
                                  lower.contains(QStringLiteral("clear")) ||
                                  lower.contains(QStringLiteral("size")));

    if (!wantsUsageReport && !wantsCleanup && !wantsFreeSpace && !wantsCacheClean) {
        return false;
    }

    if (m_diskCleanup.isAnalyzing()) {
        emit responseReceived(
            QStringLiteral("A disk cleanup analysis is already running. The report will "
                           "appear here when it finishes."));
        return true;
    }

    m_diskCleanup.startAnalysis();

    emit responseReceived(
        QStringLiteral("Analyzing disk usage and looking for safe cleanup opportunities. "
                       "The full report will appear here shortly."));

    return true;
}

bool Agent::handleUpdateCheckerQuery(const QString &message)
{
    const QString lower = message.toLower().simplified();

    static const QStringList informationalHints = {
        QStringLiteral("how to"),
        QStringLiteral("how do i"),
        QStringLiteral("how can i"),
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

    const bool wantsCheck = (lower.contains(QStringLiteral("check")) &&
                             lower.contains(QStringLiteral("update"))) ||
                            lower.contains(QStringLiteral("pending update")) ||
                            lower.contains(QStringLiteral("available update")) ||
                            lower.contains(QStringLiteral("updates available")) ||
                            lower.contains(QStringLiteral("outdated")) ||
                            ((lower.contains(QStringLiteral("update")) ||
                              lower.contains(QStringLiteral("upgrade"))) &&
                             (lower.contains(QStringLiteral("package")) ||
                              lower.contains(QStringLiteral("system")) ||
                              lower.contains(QStringLiteral("pacman")) ||
                              lower.contains(QStringLiteral("repo"))));

    if (!wantsCheck) {
        return false;
    }

    if (m_updateChecker.isChecking()) {
        emit responseReceived(
            QStringLiteral("An update check is already running. The report will appear "
                           "here when it finishes."));
        return true;
    }

    m_updateChecker.startCheck();

    emit responseReceived(
        QStringLiteral("Checking your installed packages against the latest repository "
                       "versions. The report will appear here shortly."));

    return true;
}

// ─────────────────────────────────────────────────────────────
//  UI Design-to-Code
// ─────────────────────────────────────────────────────────────
bool Agent::handleUiDevelopmentQuery(const QString &message, const QImage &image)
{
    const QString lower = message.toLower();

    static const QStringList kTriggers = {
        QStringLiteral("develop this ui"),
        QStringLiteral("implement this ui"),
        QStringLiteral("implement this design"),
        QStringLiteral("code this ui"),
        QStringLiteral("code this design"),
        QStringLiteral("build this ui"),
        QStringLiteral("create this ui"),
        QStringLiteral("generate this ui"),
        QStringLiteral("develop ui"),
        QStringLiteral("implement ui"),
        QStringLiteral("build ui from"),
        QStringLiteral("create ui from"),
        QStringLiteral("ui from this image"),
        QStringLiteral("design this screen"),
        QStringLiteral("implement this screen"),
        QStringLiteral("develop this screen"),
    };

    bool triggered = false;
    for (const QString &trigger : kTriggers) {
        if (lower.contains(trigger)) {
            triggered = true;
            break;
        }
    }

    if (!triggered) {
        return false;
    }

    if (m_uiDeveloper.isBusy()) {
        emit responseReceived(
            QStringLiteral("A UI generation is already in progress. Please wait for it to finish."));
        return true;
    }

    if (m_projectDirectory.isEmpty()) {
        emit responseReceived(
            QStringLiteral("Please set a Project Directory in the Developer Hub before requesting UI generation."));
        return true;
    }

    // Auto-detect framework
    const UiDeveloper::Framework fw = UiDeveloper::detectFramework(m_projectDirectory);
    const QString fwName = UiDeveloper::frameworkName(fw);

    emit responseReceived(
        QStringLiteral("🚀 Starting UI generation for **%1** project.\n"
                       "Framework detected: %2\n"
                       "I will ask the AI to generate the code and create a git branch for you."
                       ).arg(m_projectDirectory, fwName));

    developUi(image, message, QString(), fw);
    return true;
}

void Agent::developUi(const QImage &designImage,
                      const QString &requirements,
                      const QString &branchName,
                      UiDeveloper::Framework framework)
{
    if (m_uiDeveloper.isBusy()) {
        emit uiDevelopmentProgress(QStringLiteral("UI generation already in progress."));
        return;
    }

    // Resolve framework
    UiDeveloper::Framework fw = framework;
    if (fw == UiDeveloper::Framework::AutoDetect) {
        fw = UiDeveloper::detectFramework(m_projectDirectory);
    }

    // Build branch name
    QString branch = branchName.trimmed();
    if (branch.isEmpty()) {
        // Sanitize requirements into a slug
        QString slug = requirements.toLower();
        slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        slug = slug.left(40).remove(QRegularExpression(QStringLiteral("-+$")));
        branch = QStringLiteral("feat/ui-%1").arg(slug.isEmpty() ? QStringLiteral("design") : slug);
    }

    emit uiDevelopmentProgress(QStringLiteral("Building prompt for %1 framework..."
        ).arg(UiDeveloper::frameworkName(fw)));

    const QString prompt = UiDeveloper::buildUiGenerationPrompt(requirements, fw, m_projectDirectory);

    if (!designImage.isNull()) {
        // Vision-based: encode image and do single-shot structured completion
        QImage scaled = designImage;
        constexpr int kMaxDim = 512;
        if (qMax(scaled.width(), scaled.height()) > kMaxDim) {
            scaled = scaled.scaled(kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "JPEG", 85);

        emit uiDevelopmentProgress(QStringLiteral("Preparing requirements and design image..."));

        QString visionModel = m_ollamaManager.visionModel();
        if (visionModel.isEmpty() && OllamaManager::isVisionCapable(m_ollamaClient.model())) {
            visionModel = m_ollamaClient.model();
        }

        // We use requestImageCompletion for a non-streaming structured response
        // Wire a one-shot connection to handle the result
        const QString capturedBranch = branch;
        const QString capturedDir    = m_projectDirectory;
        QMetaObject::Connection *connPtr = new QMetaObject::Connection;
        *connPtr = connect(&m_ollamaClient, &OllamaClient::completionReceived, this,
            [this, connPtr, capturedBranch, capturedDir](const QString &response) {
                disconnect(*connPtr);
                delete connPtr;
                m_uiDeveloper.implementFromLlmOutput(response, capturedDir, capturedBranch);
            });
        QMetaObject::Connection *errConnPtr = new QMetaObject::Connection;
        *errConnPtr = connect(&m_ollamaClient, &OllamaClient::completionError, this,
            [this, errConnPtr](const QString &error) {
                disconnect(*errConnPtr);
                delete errConnPtr;
                emit uiDevelopmentFinished(false,
                    QStringLiteral("AI model error: %1").arg(error), QString());
            });

        if (!visionModel.isEmpty()) {
            emit uiDevelopmentProgress(QStringLiteral("Sending design image to vision model (%1)...").arg(visionModel));
            m_ollamaClient.requestImageCompletion(prompt, {bytes.toBase64()}, visionModel);
        } else {
            emit uiDevelopmentProgress(QStringLiteral("Active model is text-only; generating UI from text requirements..."));
            m_ollamaClient.requestCompletion(prompt);
        }
    } else {
        // Text-only: use non-streaming completion
        const QString capturedBranch = branch;
        const QString capturedDir    = m_projectDirectory;
        QMetaObject::Connection *connPtr = new QMetaObject::Connection;
        *connPtr = connect(&m_ollamaClient, &OllamaClient::completionReceived, this,
            [this, connPtr, capturedBranch, capturedDir](const QString &response) {
                disconnect(*connPtr);
                delete connPtr;
                m_uiDeveloper.implementFromLlmOutput(response, capturedDir, capturedBranch);
            });
        QMetaObject::Connection *errConnPtr = new QMetaObject::Connection;
        *errConnPtr = connect(&m_ollamaClient, &OllamaClient::completionError, this,
            [this, errConnPtr](const QString &error) {
                disconnect(*errConnPtr);
                delete errConnPtr;
                emit uiDevelopmentFinished(false,
                    QStringLiteral("AI model error: %1").arg(error), QString());
            });

        emit uiDevelopmentProgress(QStringLiteral("Sending requirements to AI model..."));
        m_ollamaClient.requestCompletion(prompt);
    }
}
