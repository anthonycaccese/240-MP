#include "ScriptsBackend.h"
#include "ScriptLauncher.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

static const char *kModuleId       = "com.240mp.scripts";
// Deliberately NOT "scripts": $APP_ROOT/scripts already means the app's own Lua
// and install scripts, and two different things called "the scripts dir" would
// make every support thread ambiguous.
static const char *kScriptsDirName = "user_scripts";

ScriptsBackend::ScriptsBackend(const QString &appRoot, const QString &dataRoot,
                               QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    m_launcher = new ScriptLauncher(m_appRoot, m_dataRoot, this);
    connect(m_launcher, &ScriptLauncher::runningChanged,
            this, &ScriptsBackend::scriptRunningChanged);
    connect(m_launcher, &ScriptLauncher::outputChanged,
            this, &ScriptsBackend::consoleOutputChanged);
    connect(m_launcher, &ScriptLauncher::finished,
            this, &ScriptsBackend::scriptFinished);

    // Read the configured directory straight from config.json — AppCore isn't
    // available to backends at construction time. Same approach as
    // AmbientModeBackend / NfcReaderBackend.
    QString configured;
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        configured = cfg["modules"].toObject()[QLatin1String(kModuleId)].toObject()
                        ["scripts_directory"].toString();
    }
    setScriptsDir(configured);
}

// An empty (or "USE DEFAULT") setting means the dataRoot default.
void ScriptsBackend::setScriptsDir(const QString &path) {
    m_scriptsDir = path.isEmpty() ? m_dataRoot + "/" + QLatin1String(kScriptsDirName)
                                  : path;
    QDir().mkpath(m_scriptsDir);
    m_warnedUnwritable = false;   // new directory, new chance to be writable
    qDebug("[Scripts] Scripts dir: %s", qPrintable(m_scriptsDir));
    scanScriptsDir();
}

void ScriptsBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                      const QVariant &value) {
    if (moduleId != QLatin1String(kModuleId)) return;

    if (key == QLatin1String("scripts_directory")) {
        setScriptsDir(value.toString());
        emit scriptsChanged();
    }
}

QString ScriptsBackend::sidecarPathFor(const QString &scriptPath) {
    const QFileInfo fi(scriptPath);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".txt";
}

// "boot-fieldstation42.sh" -> "boot fieldstation42". Views uppercase everything
// (font.capitalization: Font.AllUppercase), so there's no need to title-case here.
QString ScriptsBackend::defaultNameFor(const QString &basename) {
    QString name = QFileInfo(basename).completeBaseName();
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    return name.trimmed();
}

bool ScriptsBackend::parseBool(const QString &value, bool fallback) {
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("yes") || v == QLatin1String("on")
        || v == QLatin1String("true") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("no") || v == QLatin1String("off")
        || v == QLatin1String("false") || v == QLatin1String("0"))
        return false;
    return fallback;
}

// Sidecar format: whole-line "#" comments, blank lines ignored, and "key = value"
// split at the FIRST "=" so a value may itself contain one (args, notably).
// Unknown keys are logged and ignored, which keeps older app versions working
// with sidecars written by newer ones.
bool ScriptsBackend::parseSidecar(const QString &sidecarPath, ScriptMeta &meta) const {
    QFile file(sidecarPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))     // strip a BOM if an editor added one
        text.remove(0, 1);

    for (QString line : text.split(u'\n')) {
        line = line.trimmed();              // also strips the \r of CRLF files
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            qWarning("[Scripts] %s: ignoring unparsable line '%s'",
                     qPrintable(QFileInfo(sidecarPath).fileName()), qPrintable(line));
            continue;
        }
        const QString key   = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("name")) {
            if (!value.isEmpty()) meta.name = value;
        } else if (key == QLatin1String("mode")) {
            const QString m = value.toLower();
            if (m == QLatin1String("console") || m == QLatin1String("takeover")) {
                meta.mode = m;
            } else if (!m.isEmpty()) {
                // Fall back to console, never takeover: a bad value must not
                // hand the whole display to a script by accident.
                qWarning("[Scripts] %s: unknown mode '%s' — using console",
                         qPrintable(QFileInfo(sidecarPath).fileName()), qPrintable(value));
            }
        } else if (key == QLatin1String("favorite")) {
            meta.favorite = parseBool(value, meta.favorite);
        } else if (key == QLatin1String("confirm")) {
            meta.confirm = parseBool(value, meta.confirm);
        } else if (key == QLatin1String("args")) {
            meta.args = value;
        } else if (key == QLatin1String("wait")) {
            const QString w = value.toLower();
            if (w == QLatin1String("pgroup") || w == QLatin1String("child"))
                meta.wait = w;
            else if (!w.isEmpty())
                qWarning("[Scripts] %s: unknown wait '%s' — using pgroup",
                         qPrintable(QFileInfo(sidecarPath).fileName()), qPrintable(value));
        } else if (key == QLatin1String("tty")) {
            meta.tty = parseBool(value, meta.tty);
        } else {
            qDebug("[Scripts] %s: ignoring unknown key '%s'",
                   qPrintable(QFileInfo(sidecarPath).fileName()), qPrintable(key));
        }
    }
    return true;
}

// A script with no sidecar gets one written with its defaults, so the user only
// has to edit values rather than remember the format. NewOnly never overwrites:
// if a same-named .txt already exists (someone's unrelated notes file), we leave
// it alone — parseSidecar simply found no recognised keys and the defaults stand.
void ScriptsBackend::writeStubSidecar(const QString &sidecarPath,
                                      const ScriptMeta &defaults) {
    QFile file(sidecarPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        // Expected on a read-only directory (a mounted share, a git checkout the
        // user can't write). Not fatal — the script still runs on defaults.
        if (!m_warnedUnwritable) {
            qWarning("[Scripts] Could not write sidecar %s: %s (using defaults; "
                     "further sidecar-write failures will not be logged)",
                     qPrintable(sidecarPath), qPrintable(file.errorString()));
            m_warnedUnwritable = true;
        }
        return;
    }

    const QString scriptName = QFileInfo(sidecarPath).completeBaseName() + ".sh";
    QString out;
    out += "# 240-MP script metadata for " + scriptName + "\n";
    out += "# Whole-line comments only. Unknown keys are ignored.\n";
    out += "# Delete this file to regenerate it with defaults.\n";
    out += "\n";
    out += "# Row label shown in 240-MP.\n";
    out += "name = " + defaults.name + "\n";
    out += "\n";
    out += "# console  = 240-MP stays on screen and shows this script's output.\n";
    out += "# takeover = the script gets the whole screen (a TV app, a game front end).\n";
    out += "mode = " + defaults.mode + "\n";
    out += "\n";
    out += "# yes = also show this script on the main menu.\n";
    out += "favorite = " + QString(defaults.favorite ? "yes" : "no") + "\n";
    out += "\n";
    out += "# yes = ask for confirmation before running.\n";
    out += "confirm = " + QString(defaults.confirm ? "yes" : "no") + "\n";
    out += "\n";
    out += "# Extra arguments passed to the script.\n";
    out += "args = " + defaults.args + "\n";
    out += "\n";
    out += "# takeover only. pgroup = wait for everything the script started before\n";
    out += "# taking the screen back; child = wait only for the script itself.\n";
    out += "wait = " + defaults.wait + "\n";
    out += "\n";
    out += "# takeover on Linux only, and needs a udev rule (see INSTALL.md).\n";
    out += "# yes = give the script a real terminal, for one that needs typed input.\n";
    out += "tty = " + QString(defaults.tty ? "yes" : "no") + "\n";

    file.write(out.toUtf8());
    qDebug("[Scripts] Created sidecar: %s", qPrintable(QFileInfo(sidecarPath).fileName()));
}

void ScriptsBackend::scanScriptsDir() {
    m_scripts.clear();

    // QDir::Files excludes hidden entries (.DS_Store and friends). The suffix is
    // compared manually because nameFilters are case-sensitive on Linux, and a
    // user's FOO.SH should still be listed.
    const QDir dir(m_scriptsDir);
    const QFileInfoList files =
        dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fi : files) {
        if (fi.suffix().compare(QLatin1String("sh"), Qt::CaseInsensitive) != 0)
            continue;

        ScriptEntry entry;
        entry.path      = fi.absoluteFilePath();
        entry.basename  = fi.fileName();
        entry.meta.name = defaultNameFor(entry.basename);

        const QString sidecar = sidecarPathFor(entry.path);
        if (!parseSidecar(sidecar, entry.meta))
            writeStubSidecar(sidecar, entry.meta);

        m_scripts.append(entry);
    }

    qDebug("[Scripts] Scanned %lld script(s) from %s",
           static_cast<long long>(m_scripts.size()), qPrintable(m_scriptsDir));
}

QVariantList ScriptsBackend::getScripts() {
    scanScriptsDir();

    QVariantList out;
    for (const ScriptEntry &e : m_scripts) {
        QVariantMap m;
        m["name"]     = e.meta.name;
        m["path"]     = e.path;
        m["basename"] = e.basename;
        m["mode"]     = e.meta.mode;
        m["favorite"] = e.meta.favorite;
        m["confirm"]  = e.meta.confirm;
        out.append(m);
    }
    return out;
}

QString ScriptsBackend::resolveScript(const QString &basename) {
    if (basename.isEmpty()) return {};
    if (m_scripts.isEmpty()) scanScriptsDir();
    for (const ScriptEntry &e : m_scripts) {
        if (e.basename == basename)
            return e.path;
    }
    // Might have been added since the last scan.
    scanScriptsDir();
    for (const ScriptEntry &e : m_scripts) {
        if (e.basename == basename)
            return e.path;
    }
    qWarning("[Scripts] No script named '%s' in %s",
             qPrintable(basename), qPrintable(m_scriptsDir));
    return {};
}

void ScriptsBackend::getStartupScriptOptions() {
    scanScriptsDir();

    QVariantList options;
    auto add = [&](const QString &id, const QString &label) {
        QVariantMap m;
        m["id"]    = id;
        m["label"] = label;
        options.append(m);
    };
    // "None" first, matching the app's other opt-out settings.
    add(QStringLiteral("None"), QStringLiteral("None"));
    for (const ScriptEntry &e : m_scripts)
        add(e.basename, e.meta.name);   // id is the basename, never a full path

    emit dynamicOptionsReady(QStringLiteral("startup_script"), options);
}

void ScriptsBackend::rescanScripts() {
    scanScriptsDir();
    emit scriptsChanged();
}

// --- running ---

bool ScriptsBackend::entryFor(const QString &basename, ScriptEntry &out) {
    if (basename.isEmpty()) return false;
    for (const ScriptEntry &e : m_scripts) {
        if (e.basename == basename) { out = e; return true; }
    }
    scanScriptsDir();   // may have been added since the last scan
    for (const ScriptEntry &e : m_scripts) {
        if (e.basename == basename) { out = e; return true; }
    }
    return false;
}

bool ScriptsBackend::scriptRunning() const { return m_launcher->isRunning(); }
QString ScriptsBackend::consoleOutput() const { return m_launcher->outputText(); }
QString ScriptsBackend::runningName() const { return m_launcher->runningName(); }
int ScriptsBackend::lastExitCode() const { return m_launcher->lastExitCode(); }

QString ScriptsBackend::modeFor(const QString &basename) {
    ScriptEntry e;
    if (!entryFor(basename, e)) return {};
    return e.meta.mode;
}

bool ScriptsBackend::runScript(const QString &basename) {
    m_lastError.clear();
    ScriptEntry entry;
    if (!entryFor(basename, entry)) {
        m_lastError = QStringLiteral("No script named %1").arg(basename);
        qWarning("[Scripts] %s", qPrintable(m_lastError));
        return false;
    }
    return m_launcher->start(entry, &m_lastError);
}

void ScriptsBackend::stopScript() {
    m_launcher->requestStop();
}
