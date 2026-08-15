#include "agent/agent.hpp"

#include <QRegularExpression>
#include <QTimer>

Agent::Agent(QObject *parent)
    : QObject(parent)
    , m_ollamaClient(this)
{
    connect(&m_ollamaClient, &OllamaClient::responseChunkReceived, this, &Agent::responseChunkReceived);
    connect(&m_ollamaClient, &OllamaClient::responseReceived, this, &Agent::responseReceived);
    connect(&m_ollamaClient, &OllamaClient::errorOccurred, this, &Agent::errorOccurred);

    connect(&m_ollamaManager, &OllamaManager::statusChanged, this, &Agent::modelStatusChanged);
    connect(&m_ollamaManager, &OllamaManager::modelReady, this, &Agent::onModelReady);
    connect(&m_ollamaManager, &OllamaManager::modelError, this, &Agent::modelError);

    connect(&m_packageManager, &PackageManager::outputReceived, this, &Agent::toolOutputReceived);
    connect(&m_packageManager, &PackageManager::finished, this, &Agent::onPackageManagerFinished);
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

    if (handlePackageInstallQuery(message)) {
        return;
    }

    m_ollamaClient.sendPrompt(message);
}

void Agent::performInstall(const QStringList &packages)
{
    m_packageManager.install(packages);
}

void Agent::onPackageManagerFinished(bool success, const QString &summary)
{
    if (success) {
        emit responseReceived(summary);
    } else {
        emit errorOccurred(summary);
    }
}

bool Agent::handlePackageInstallQuery(const QString &message)
{
    QString lower = message.toLower();

    if (isInformationalQuery(lower)) {
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
