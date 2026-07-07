#include "IdleTracker.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>

IdleTracker::IdleTracker(int thresholdSec, QObject *parent)
    : QObject(parent)
    , m_threshold(thresholdSec)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &IdleTracker::tick);

    // App-wide event filter catches all keyboard and gamepad-synthesised key
    // events before they reach QML, so we reset the idle counter on any input.
    QCoreApplication::instance()->installEventFilter(this);

    m_timer.start();
}

void IdleTracker::setThreshold(int seconds)
{
    if (m_threshold == seconds)
        return;
    m_threshold = seconds;
    emit thresholdChanged();
}

void IdleTracker::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();

    if (!m_enabled && m_active) {
        m_active = false;
        emit activeChanged();
    }
}

void IdleTracker::setMpvActive(bool active)
{
    if (m_mpvActive == active)
        return;
    m_mpvActive = active;
    emit mpvActiveChanged();

    // If activation was blocked by mpv and mpv just ended, deactivate immediately
    // so the screen saver doesn't flash on the menu after playback.
    if (!m_mpvActive && m_active) {
        m_active = false;
        emit activeChanged();
    }
}

void IdleTracker::resetActivity()
{
    const bool wasActive = m_active;
    m_idleSeconds = 0;
    m_active = false;

    emit idleSecondsChanged();
    if (wasActive)
        emit activeChanged();
    emit activityDetected();
}

bool IdleTracker::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj)
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // Ignore auto-repeat events — holding a key shouldn't keep resetting
        // the idle timer forever (the first press resets, held repeats don't).
        if (!ke->isAutoRepeat())
            resetActivity();
    }
    // Never consume the event — let it reach QML normally.
    return false;
}

void IdleTracker::tick()
{
    if (m_active || !m_enabled || m_mpvActive)
        return;

    ++m_idleSeconds;
    emit idleSecondsChanged();

    if (m_idleSeconds >= m_threshold) {
        m_active = true;
        emit activeChanged();
    }
}
