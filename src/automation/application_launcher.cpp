#include "automation/application_launcher.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QMap>
#include <QSet>

#include <algorithm>

#include "terminal/command_safety_validator.hpp"

namespace {

QString normalizedName(QString value)
{
    value = value.trimmed().toLower();
    if (value.endsWith(QStringLiteral(".desktop"))) {
        value.chop(8);
    }
    value.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return value;
}

bool isBlockedExecutable(const QString &program)
{
    static const QStringList blocked = {
        QStringLiteral("sh"),   QStringLiteral("bash"), QStringLiteral("dash"),
        QStringLiteral("zsh"),  QStringLiteral("fish"), QStringLiteral("env"),
        QStringLiteral("python"), QStringLiteral("python3"), QStringLiteral("perl"),
        QStringLiteral("ruby"), QStringLiteral("node"), QStringLiteral("php"),
        QStringLiteral("lua"),  QStringLiteral("busybox"), QStringLiteral("xargs")
    };
    return blocked.contains(QFileInfo(program).fileName().toLower());
}

struct ParsedExec {
    QString     program;
    QStringList arguments;
    QString     error;
    bool        ok{false};
};

bool containsControlCharacters(const QString &value)
{
    for (const QChar &ch : value) {
        if (ch.unicode() < 0x20 && ch != QLatin1Char('\t')) {
            return true;
        }
    }
    return false;
}

ParsedExec parseExec(const QString &exec)
{
    ParsedExec parsed;

    QStringList tokens;
    QString current;
    QChar quote;
    bool escaping = false;

    for (const QChar &ch : exec) {
        if (escaping) {
            current.append(ch);
            escaping = false;
            continue;
        }
        if (ch == QLatin1Char('\\') && quote != QLatin1Char('\'')) {
            escaping = true;
            continue;
        }
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                current.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }

    if (escaping) {
        current.append(QLatin1Char('\\'));
    }
    if (!quote.isNull()) {
        parsed.error = QStringLiteral("Unbalanced quotes in desktop Exec entry.");
        return parsed;
    }
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    if (tokens.isEmpty()) {
        parsed.error = QStringLiteral("Empty desktop Exec entry.");
        return parsed;
    }

    static const QString fieldCodes = QStringLiteral("fFuUdDnNickvm");
    QStringList cleanedTokens;
    for (const QString &token : tokens) {
        QString cleaned;
        for (int i = 0; i < token.size(); ++i) {
            const QChar ch = token.at(i);
            if (ch == QLatin1Char('%')) {
                if (i + 1 >= token.size()) {
                    parsed.error = QStringLiteral("Invalid trailing '%' in Exec entry.");
                    return parsed;
                }
                const QChar code = token.at(++i);
                if (code == QLatin1Char('%')) {
                    cleaned.append(QLatin1Char('%'));
                } else if (fieldCodes.contains(code)) {
                    continue;
                } else {
                    parsed.error = QStringLiteral("Unsupported Exec field code %%%1.")
                                       .arg(code);
                    return parsed;
                }
                continue;
            }
            if (containsControlCharacters(QString(ch))) {
                parsed.error = QStringLiteral("Control character in Exec entry.");
                return parsed;
            }
            cleaned.append(ch);
        }
        if (!cleaned.isEmpty()) {
            cleanedTokens.append(cleaned);
        }
    }

    if (cleanedTokens.isEmpty()) {
        parsed.error = QStringLiteral("Desktop Exec entry contains no program.");
        return parsed;
    }

    QString program = cleanedTokens.takeFirst();
    if (isBlockedExecutable(program)) {
        parsed.error = QStringLiteral("Desktop entry uses a blocked shell/interpreter: %1")
                           .arg(program);
        return parsed;
    }

    for (const QString &arg : cleanedTokens) {
        if (CommandSafetyValidator::hasShellMetacharacters(arg)) {
            parsed.error = QStringLiteral("Desktop entry contains unsupported shell syntax.");
            return parsed;
        }
    }

    parsed.program = program;
    parsed.arguments = cleanedTokens;
    parsed.ok = true;
    return parsed;
}

QString resolveProgram(const QString &program)
{
    if (program.isEmpty()) {
        return {};
    }
    const QFileInfo info(program);
    if (info.isAbsolute()) {
        return info.exists() && info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    if (program.contains(QLatin1Char('/'))) {
        return {};
    }
    return QStandardPaths::findExecutable(program);
}

ApplicationInfo applicationFromExecutable(const QString &name, const QString &executable)
{
    ApplicationInfo info;
    info.id = name.toLower();
    info.name = name;
    info.executable = executable;
    info.fromDesktopEntry = false;
    return info;
}

} // namespace

ApplicationLauncher::ApplicationLauncher() = default;
ApplicationLauncher::~ApplicationLauncher() = default;

QStringList ApplicationLauncher::trustedDesktopDirectories()
{
    const QString home = QDir::cleanPath(QDir::homePath());
    QStringList dirs = {
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/snapd/desktop/applications")
    };

    const QStringList standard = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation);
    for (const QString &dir : standard) {
        const QString clean = QDir::cleanPath(dir);
        if (clean.startsWith(home + QLatin1Char('/')) || clean == home) {
            continue;
        }
        if (!dirs.contains(clean)) {
            dirs.append(clean);
        }
    }
    return dirs;
}

QList<ApplicationInfo> ApplicationLauncher::parseDesktopDirectory(const QString &directory)
{
    QList<ApplicationInfo> applications;
    const QDir dir(directory);
    if (!dir.exists()) {
        return applications;
    }

    const QFileInfoList files = dir.entryInfoList(
        {QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        bool inDesktopEntry = false;
        bool hidden = false;
        bool noDisplay = false;
        QString type;
        QString name;
        QString exec;
        QString tryExec;

        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                inDesktopEntry = line == QLatin1String("[Desktop Entry]");
                continue;
            }
            if (!inDesktopEntry || line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = line.left(eq).trimmed();
            const QString value = line.mid(eq + 1).trimmed();
            if (key == QLatin1String("Type")) {
                type = value;
            } else if (key == QLatin1String("Name")) {
                name = value;
            } else if (key == QLatin1String("Exec")) {
                exec = value;
            } else if (key == QLatin1String("TryExec")) {
                tryExec = value;
            } else if (key == QLatin1String("Hidden")) {
                hidden = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            } else if (key == QLatin1String("NoDisplay")) {
                noDisplay = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            }
        }

        if (type != QLatin1String("Application") || hidden || noDisplay
            || name.isEmpty() || exec.isEmpty()) {
            continue;
        }
        if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()
            && !QFileInfo(tryExec).isExecutable()) {
            continue;
        }

        const ParsedExec parsed = parseExec(exec);
        if (!parsed.ok) {
            continue;
        }
        const QString executable = resolveProgram(parsed.program);
        if (executable.isEmpty()) {
            continue;
        }

        ApplicationInfo info;
        info.id = fileInfo.completeBaseName();
        info.name = name;
        info.executable = executable;
        info.desktopFile = fileInfo.absoluteFilePath();
        info.baseArguments = parsed.arguments;
        info.fromDesktopEntry = true;
        applications.append(info);
    }

    return applications;
}

QStringList ApplicationLauncher::searchPaths() const
{
    if (!m_searchPathsOverride.isEmpty()) {
        return m_searchPathsOverride;
    }
    return trustedDesktopDirectories();
}

QList<ApplicationInfo> ApplicationLauncher::scan() const
{
    QList<ApplicationInfo> result;
    QSet<QString> seen;
    for (const QString &directory : searchPaths()) {
        for (const ApplicationInfo &app : parseDesktopDirectory(directory)) {
            if (seen.contains(app.id)) {
                continue;
            }
            seen.insert(app.id);
            result.append(app);
        }
    }
    std::sort(result.begin(), result.end(), [](const ApplicationInfo &a,
                                               const ApplicationInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<ApplicationInfo> ApplicationLauncher::applications() const
{
    if (!m_cacheValid) {
        m_cache = scan();
        m_cacheValid = true;
    }
    return m_cache;
}

void ApplicationLauncher::setSearchPathsForTesting(const QStringList &directories)
{
    m_searchPathsOverride = directories;
    m_cacheValid = false;
}

ApplicationInfo ApplicationLauncher::resolve(const QString &name, QString *error) const
{
    const QString query = name.trimmed();
    if (query.isEmpty()) {
        if (error) *error = QStringLiteral("Application name is empty.");
        return {};
    }
    if (query.contains(QLatin1Char('/')) || query.contains(QLatin1Char('\\'))) {
        if (error) *error = QStringLiteral("Application names must not contain paths.");
        return {};
    }

    const QList<ApplicationInfo> installed = applications();
    const QString queryNorm = normalizedName(query);
    const QString queryLower = query.toLower();

    for (const ApplicationInfo &app : installed) {
        const QString idNorm = normalizedName(app.id);
        const QString nameNorm = normalizedName(app.name);
        if (idNorm == queryNorm || nameNorm == queryNorm
            || app.id.compare(query, Qt::CaseInsensitive) == 0
            || app.name.compare(query, Qt::CaseInsensitive) == 0) {
            return app;
        }
    }

    static const QMap<QString, QStringList> aliases = {
        {QStringLiteral("browser"),
         {QStringLiteral("firefox"), QStringLiteral("chromium"),
          QStringLiteral("google-chrome"), QStringLiteral("brave")}},
        {QStringLiteral("firefox"), {QStringLiteral("firefox"), QStringLiteral("firefox-esr")}},
        {QStringLiteral("code"),
         {QStringLiteral("code"), QStringLiteral("code-oss"),
          QStringLiteral("codium"), QStringLiteral("vscodium")}},
        {QStringLiteral("vscode"),
         {QStringLiteral("code"), QStringLiteral("code-oss"),
          QStringLiteral("codium"), QStringLiteral("vscodium")}},
        {QStringLiteral("vs code"),
         {QStringLiteral("code"), QStringLiteral("code-oss"),
          QStringLiteral("codium"), QStringLiteral("vscodium")}},
        {QStringLiteral("visual studio code"),
         {QStringLiteral("code"), QStringLiteral("code-oss"),
          QStringLiteral("codium"), QStringLiteral("vscodium")}},
        {QStringLiteral("terminal"),
         {QStringLiteral("konsole"), QStringLiteral("alacritty"),
          QStringLiteral("kitty"), QStringLiteral("foot"),
          QStringLiteral("wezterm"), QStringLiteral("gnome-terminal"),
          QStringLiteral("kgx"), QStringLiteral("xterm")}},
        {QStringLiteral("file manager"),
         {QStringLiteral("dolphin"), QStringLiteral("nautilus"),
          QStringLiteral("thunar"), QStringLiteral("nemo"),
          QStringLiteral("pcmanfm")}},
        {QStringLiteral("files"),
         {QStringLiteral("dolphin"), QStringLiteral("nautilus"),
          QStringLiteral("thunar"), QStringLiteral("nemo"),
          QStringLiteral("pcmanfm")}},
        {QStringLiteral("editor"),
         {QStringLiteral("code"), QStringLiteral("kate"),
          QStringLiteral("gedit"), QStringLiteral("mousepad"),
          QStringLiteral("gnome-text-editor")}}
    };

    const QStringList aliasCandidates = aliases.value(queryLower);
    for (const QString &candidate : aliasCandidates) {
        for (const ApplicationInfo &app : installed) {
            if (app.id.compare(candidate, Qt::CaseInsensitive) == 0
                || app.id.compare(candidate + QStringLiteral(".desktop"),
                                  Qt::CaseInsensitive) == 0
                || app.name.compare(candidate, Qt::CaseInsensitive) == 0
                || QFileInfo(app.executable).fileName().compare(
                       candidate, Qt::CaseInsensitive) == 0) {
                return app;
            }
        }
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty()) {
            return applicationFromExecutable(candidate, executable);
        }
    }

    if (!query.contains(QLatin1Char(' '))) {
        const QString executable = QStandardPaths::findExecutable(queryLower);
        if (!executable.isEmpty()) {
            return applicationFromExecutable(queryLower, executable);
        }
    }

    if (error) {
        *error = QStringLiteral("Application '%1' is not installed or has no trusted "
                                "desktop entry.").arg(query);
    }
    return {};
}

bool ApplicationLauncher::isInstalled(const QString &name) const
{
    return resolve(name, nullptr).isValid();
}

bool ApplicationLauncher::launch(const ApplicationInfo &application,
                                 const QStringList &extraArguments,
                                 const QString &workingDirectory,
                                 QString *error)
{
    if (!application.isValid()) {
        if (error) *error = QStringLiteral("Application has no executable.");
        return false;
    }
    if (isBlockedExecutable(application.executable)) {
        if (error) *error = QStringLiteral("Refusing to launch a shell/interpreter.");
        return false;
    }

    const QString program = resolveProgram(application.executable);
    if (program.isEmpty()) {
        if (error) *error = QStringLiteral("Executable not found: %1")
                                .arg(application.executable);
        return false;
    }

    QStringList arguments = application.baseArguments;
    for (const QString &arg : extraArguments) {
        if (containsControlCharacters(arg)) {
            if (error) *error = QStringLiteral("Invalid control character in argument.");
            return false;
        }
        arguments.append(arg);
    }

    QString directory = workingDirectory.trimmed();
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    } else {
        const QFileInfo dirInfo(directory);
        if (!dirInfo.exists() || !dirInfo.isDir()) {
            if (error) *error = QStringLiteral("Working directory does not exist: %1")
                                    .arg(directory);
            return false;
        }
    }

    qint64 pid = 0;
    const bool started = QProcess::startDetached(program, arguments, directory, &pid);
    if (!started) {
        if (error) *error = QStringLiteral("Failed to launch '%1'.").arg(program);
        return false;
    }
    return true;
}

bool ApplicationLauncher::openDirectory(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        if (error) *error = QStringLiteral("Directory does not exist: %1").arg(path);
        return false;
    }

    ApplicationInfo fileManager = resolve(QStringLiteral("files"), nullptr);
    if (fileManager.isValid()
        && launch(fileManager, {info.absoluteFilePath()}, QDir::homePath(), error)) {
        return true;
    }

    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (xdgOpen.isEmpty()) {
        if (error) *error = QStringLiteral("No trusted file manager is available.");
        return false;
    }
    const bool started = QProcess::startDetached(
        xdgOpen, {info.absoluteFilePath()}, QDir::homePath());
    if (!started && error) {
        *error = QStringLiteral("Failed to open the directory.");
    }
    return started;
}

bool ApplicationLauncher::openFile(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) *error = QStringLiteral("File does not exist: %1").arg(path);
        return false;
    }

    const ApplicationInfo editor = resolve(QStringLiteral("editor"), nullptr);
    if (!editor.isValid()) {
        if (error) *error = QStringLiteral("No trusted text editor is available.");
        return false;
    }
    return launch(editor, {info.absoluteFilePath()}, QDir::homePath(), error);
}
