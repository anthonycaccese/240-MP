#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include "ScriptsBackend.h"     // ScriptEntry / ScriptMeta

class QTimer;
class DisplayHandoff;

// Runs one user script at a time, in one of two modes:
//
// * console  — 240-MP keeps the screen and the script's merged stdout/stderr is
//              streamed into a QML view. Identical on every target.
// * takeover — the script owns the screen. On macOS / desktop Linux / SteamOS
//              that needs nothing: the child's window covers ours, exactly like
//              mpv. On a headless Pi it is bracketed by DisplayHandoff (VT switch
//              + drmDropMaster + CRTC save/restore).
//
// The child is always put in its own session/process group (setsid), so stopping
// a script takes its children with it rather than orphaning whatever it spawned —
// and in takeover mode that group is also how we know the screen is free again.
class ScriptLauncher : public QObject {
    Q_OBJECT
public:
    explicit ScriptLauncher(const QString &appRoot, const QString &dataRoot,
                            DisplayHandoff *handoff, QObject *parent = nullptr);
    ~ScriptLauncher() override;

    bool    isRunning() const;
    // The script itself has exited but something it started is still alive, so the
    // display has not been handed back yet.
    bool    drainingGroup() const;
    // Either of the above: this run is not finished with the screen.
    bool    isBusy() const { return isRunning() || drainingGroup(); }
    QString runningName() const     { return m_runningName; }
    QString runningBasename() const { return m_runningBasename; }
    // True when the requested takeover was downgraded to console because the
    // display could not be handed over safely.
    bool    downgraded() const      { return m_downgraded; }

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
    // Takeover teardown: wait for the process group to empty, then give the
    // display back, then report. Split out because the report must not land
    // before the screen is ours again.
    void startGroupDrain();
    void releaseDisplayAndReport();
    void report();
    void resetForNewRun();

    static constexpr int kMaxLines      = 500;
    static constexpr int kMaxBytes      = 64 * 1024;
    static constexpr int kKillGraceMs   = 3000;
    static constexpr int kGroupPollMs   = 250;
    static constexpr int kGroupWarnMs   = 5000;
    static constexpr int kStartWatchdogMs = 5000;
    static constexpr const char *kHandoffOwner = "scripts";

    QString         m_appRoot;
    QString         m_dataRoot;
    DisplayHandoff *m_handoff = nullptr;
    QProcess       *m_process = nullptr;
    QTimer   *m_outputTimer = nullptr;   // coalesces outputChanged
    QTimer   *m_killTimer   = nullptr;   // SIGTERM -> SIGKILL grace
    QTimer   *m_groupTimer  = nullptr;   // polls the process group after exit
    QTimer   *m_startTimer  = nullptr;   // never-started watchdog

    QString     m_runningName;
    QString     m_runningBasename;
    qint64      m_pgid = -1;
    bool        m_stopRequested = false;
    bool        m_finished      = true;   // latch; true when no run is in flight
    int         m_lastExitCode  = 0;
    QString     m_pendingReason;          // held while the display is handed back
    // This run took the takeover path, so it gets the takeover teardown (drain the
    // process group, hand the display back, then report). Note this is true on
    // desktop too, where acquire() held nothing — draining the group before saying
    // "done" is correct on every platform.
    bool        m_takeoverRun   = false;
    bool        m_waitForGroup  = true;
    bool        m_downgraded    = false;
    qint64      m_drainStartMs  = 0;
    bool        m_warnedDrain   = false;

    QStringList m_lines;
    QString     m_partial;
    bool        m_pendingCr = false;      // saw \r, waiting to see if \n follows
    int         m_bytes     = 0;
};
