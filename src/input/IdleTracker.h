#pragma once
#include <QObject>
#include <QTimer>

// Tracks global input inactivity and emits signals when idle for a configurable
// number of seconds. Used by the screen saver overlay in Main.qml.
//
// Installs a global QEventFilter that resets on any KeyPress (real keyboard
// and gamepad-synthesized events both reach it). A 1-second tick timer
// increments m_idleSeconds while no activity is detected.
//
// Exposed to QML as a context property "idleTracker".
class IdleTracker : public QObject {
    Q_OBJECT
    // True when idle time has exceeded the threshold.
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    // Current consecutive idle seconds since last activity.
    Q_PROPERTY(int idleSeconds READ idleSeconds NOTIFY idleSecondsChanged)
    // Idle threshold in seconds before active becomes true.
    Q_PROPERTY(int threshold READ threshold WRITE setThreshold NOTIFY thresholdChanged)
    // Master switch — when false the timer never fires active.
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    // Blocks activation while a media session is active (mpv playing).
    // Prevents the screen saver from showing on top of playback.
    Q_PROPERTY(bool mpvActive READ isMpvActive WRITE setMpvActive NOTIFY mpvActiveChanged)

public:
    explicit IdleTracker(int thresholdSec = 60, QObject *parent = nullptr);

    bool isActive()      const { return m_active;       }
    int  idleSeconds()   const { return m_idleSeconds;  }
    int  threshold()     const { return m_threshold;    }
    bool isEnabled()     const { return m_enabled;      }
    bool isMpvActive()   const { return m_mpvActive;    }

    void setThreshold(int seconds);
    void setEnabled(bool on);
    void setMpvActive(bool active);

    // Called from C++ or QML to mark input activity — resets the idle counter
    // and deactivates the idle state if it was active.
    Q_INVOKABLE void resetActivity();

signals:
    void activeChanged();
    void idleSecondsChanged();
    void thresholdChanged();
    void enabledChanged();
    void mpvActiveChanged();
    // Emitted on every call to resetActivity (including from the event filter).
    void activityDetected();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void tick();

private:
    QTimer m_timer;
    int    m_threshold;
    int    m_idleSeconds = 0;
    bool   m_active      = false;
    bool   m_enabled     = false;   // off until Main.qml applies the saved setting
    bool   m_mpvActive   = false;
};
