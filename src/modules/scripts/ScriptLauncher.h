#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include "ScriptsBackend.h"     // ScriptEntry / ScriptMeta

class QTimer;

// Runs one user script at a time.
//
// Console mode (this class's only mode until takeover lands): 240-MP keeps the
// screen and the script's merged stdout/stderr is streamed into a QML view. That
// is identical on every target — no VT, no DRM, no window juggling.
//
// The child is always put in its own session/process group (setsid), so stopping
// a script takes its children with it rather than orphaning whatever it spawned.
class ScriptLauncher : public QObject {
    Q_OBJECT
public:
    explicit ScriptLauncher(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);
    ~ScriptLauncher() override;

    bool    isRunning() const;
    QString runningName() const     { return m_runningName; }
    QString runningBasename() const { return m_runningBasename; }

    // Validates before spawning and returns false with *errorOut set if anything
    // is wrong, having started nothing. Callers must surface that error: from
    // Phase 4 a failed start after the display was handed over would strand the
    // user on a black screen, so nothing may be handed over before this passes.
    bool start(const ScriptEntry &entry, QString *errorOut);

    // SIGTERM the process group, then SIGKILL if it hasn't gone after a grace period.
    void requestStop();
    // SIGKILL the group immediately (no grace) — for the Phase 6 escape hatch.
    void forceStop();

    // Bounded tail of the script's output: at most kMaxLines lines / kMaxBytes
    // bytes, trimmed from the front, so a chatty script can't grow without limit.
    QString outputText() const;
    int     lastExitCode() const { return m_lastExitCode; }

signals:
    void runningChanged();
    // Coalesced (~100 ms) rather than emitted per readyRead, so a noisy script
    // doesn't force a QML relayout per chunk.
    void outputChanged();
    // Emitted exactly once per run. reason is one of:
    //   "ok"              — exited 0
    //   "failed"          — exited non-zero
    //   "stopped"         — the user stopped it
    //   "failed_to_start" — never ran (bad interpreter, permissions, fork failure)
    void finished(int exitCode, const QString &reason);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    void appendOutput(const QString &chunk);
    void pushLine();
    void trimOutput();
    void scheduleOutputChanged();
    void killGroup(int sig);
    // Idempotent: QProcess::finished and errorOccurred can both arrive for one run.
    void finish(int exitCode, const QString &reason);
    void resetForNewRun();

    static constexpr int kMaxLines    = 500;
    static constexpr int kMaxBytes    = 64 * 1024;
    static constexpr int kKillGraceMs = 3000;

    QString   m_appRoot;
    QString   m_dataRoot;
    QProcess *m_process = nullptr;
    QTimer   *m_outputTimer = nullptr;   // coalesces outputChanged
    QTimer   *m_killTimer   = nullptr;   // SIGTERM -> SIGKILL grace

    QString     m_runningName;
    QString     m_runningBasename;
    qint64      m_pgid = -1;
    bool        m_stopRequested = false;
    bool        m_finished      = true;   // latch; true when no run is in flight
    int         m_lastExitCode  = 0;

    QStringList m_lines;
    QString     m_partial;
    bool        m_pendingCr = false;      // saw \r, waiting to see if \n follows
    int         m_bytes     = 0;
};
