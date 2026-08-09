#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QList>

// Per-script settings, read from the sidecar .txt beside each .sh.
//
// Metadata lives in a sidecar rather than in the .sh itself because a user's
// launcher script may be third-party or git-managed (a FieldStation42 boot
// script, say) — we must never need to edit it to give it a display name.
struct ScriptMeta {
    QString name;                                 // row label; defaults to the filename
    QString mode     = QStringLiteral("console"); // "console" | "takeover"
    bool    favorite = false;                     // adds a main-menu row (Phase 5)
    bool    confirm  = false;                     // yes/no prompt before running
    QString args;                                 // appended after the script path
    QString wait     = QStringLiteral("pgroup");  // takeover: "pgroup" | "child"
    bool    tty      = false;                     // takeover + Linux: give the child the VT

    bool isTakeover() const { return mode == QLatin1String("takeover"); }
};

struct ScriptEntry {
    QString    path;      // absolute path to the .sh
    QString    basename;  // "foo.sh" — the stable key stored in settings
    ScriptMeta meta;
};

class ScriptLauncher;

// Lists the user's own .sh scripts and runs them, so 240-MP can act as a
// remote-friendly front end for anything else on the machine. Owns the
// ScriptLauncher and is the single object QML talks to.
class ScriptsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    scriptRunning  READ scriptRunning  NOTIFY scriptRunningChanged)
    Q_PROPERTY(QString consoleOutput  READ consoleOutput  NOTIFY consoleOutputChanged)
    Q_PROPERTY(QString runningName    READ runningName    NOTIFY scriptRunningChanged)
public:
    explicit ScriptsBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);

    // Resolved scripts directory (never empty — an unset setting means the
    // dataRoot default, per the directory_browser contract).
    Q_INVOKABLE QString scriptsDir() const { return m_scriptsDir; }

    // Rescans the directory and returns one map per script:
    //   { name, path, basename, mode, favorite, confirm }
    // Rescanning on every call is deliberate: re-entering the view picks up
    // sidecar edits with no explicit refresh.
    Q_INVOKABLE QVariantList getScripts();

    // basename ("foo.sh") -> absolute path, or empty if it no longer exists.
    // Settings store basenames so that changing scripts_directory can't leave a
    // stale absolute path behind.
    Q_INVOKABLE QString resolveScript(const QString &basename);

    // manifest: startup_script (list_single, options_source "dynamic")
    Q_INVOKABLE void getStartupScriptOptions();
    // manifest: rescan (action)
    Q_INVOKABLE void rescanScripts();

    // --- running ---
    bool    scriptRunning() const;
    QString consoleOutput() const;
    QString runningName() const;

    // Starts the named script. Returns false if it could not be started, in which
    // case lastError() says why and nothing was spawned.
    Q_INVOKABLE bool runScript(const QString &basename);
    Q_INVOKABLE void stopScript();
    Q_INVOKABLE QString lastError() const { return m_lastError; }
    Q_INVOKABLE int     lastExitCode() const;
    // Run mode for a script ("console" | "takeover"), so a view can route before
    // starting anything.
    Q_INVOKABLE QString modeFor(const QString &basename);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    // Wired to AppCore by registerModule's introspection.
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void scriptsChanged();

    void scriptRunningChanged();
    void consoleOutputChanged();
    // reason: "ok" | "failed" | "stopped" | "failed_to_start"
    void scriptFinished(int exitCode, const QString &reason);

private:
    void setScriptsDir(const QString &path);
    // Looks up a scanned entry by basename; false if there is no such script.
    bool entryFor(const QString &basename, ScriptEntry &out);
    void scanScriptsDir();
    bool parseSidecar(const QString &sidecarPath, ScriptMeta &meta) const;
    void writeStubSidecar(const QString &sidecarPath, const ScriptMeta &defaults);

    static QString sidecarPathFor(const QString &scriptPath);
    static QString defaultNameFor(const QString &basename);
    static bool    parseBool(const QString &value, bool fallback);

    QString            m_appRoot;
    QString            m_dataRoot;
    QString            m_scriptsDir;
    QList<ScriptEntry> m_scripts;
    bool               m_warnedUnwritable = false; // log the read-only dir once, not per scan
    ScriptLauncher    *m_launcher = nullptr;
    QString            m_lastError;
};
