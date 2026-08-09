#include "DisplayHandoff.h"
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/vt.h>
#include <cstring>
// DRM master ioctls (also provided by xf86drm.h, but define as fallback).
#ifndef DRM_IOCTL_SET_MASTER
#define DRM_IOCTL_SET_MASTER   _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER  _IO('d', 0x1f)
#endif
#endif

DisplayHandoff::DisplayHandoff(QObject *parent)
    : QObject(parent)
{
    // A member timer rather than QTimer::singleShot so releaseNow() can cancel a
    // pending release during shutdown — the stored callback captures an object
    // that is being destroyed.
    m_releaseTimer = new QTimer(this);
    m_releaseTimer->setSingleShot(true);
    connect(m_releaseTimer, &QTimer::timeout, this, [this] {
        auto cb = m_onRestored;
        m_onRestored = nullptr;
        doRestore();
        if (cb) cb();
    });
}

DisplayHandoff::~DisplayHandoff() {
    // Restore unconditionally: leaving a Pi on a blank free VT with DRM master
    // dropped means a black screen and a text console the user never asked for.
    if (!m_owner.isEmpty()) {
        m_releaseTimer->stop();
        m_onRestored = nullptr;
        doRestore();
    }
}

bool DisplayHandoff::isHeadless() {
#ifdef Q_OS_LINUX
    return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
#else
    return false;
#endif
}

bool DisplayHandoff::isHeldBy(const QString &owner) const {
    return !m_owner.isEmpty() && m_owner == owner && m_previousVt > 0;
}

bool DisplayHandoff::savedStateValid() const {
#ifdef Q_OS_LINUX
    return m_savedDrm.valid;
#else
    return false;
#endif
}

int DisplayHandoff::acquire(const QString &owner) {
    if (!m_owner.isEmpty() && m_owner != owner) {
        qWarning("[DisplayHandoff] %s requested the screen but %s holds it",
                 qPrintable(owner), qPrintable(m_owner));
        return -1;
    }
    if (!isHeadless())
        return 0;

    m_owner      = owner;
    m_previousVt = getActiveVt();
    m_qtDrmFd    = -1;

#ifdef Q_OS_LINUX
    // VT switch first — suspends Qt's render thread via the kernel's VT switch
    // signal before DRM master is dropped, eliminating the race that causes
    // "Failed to commit atomic request" log noise.
    const int freeVt = findFreeVt();
    switchToVt(freeVt);

    m_qtDrmFd = findQtDrmFd();
    if (m_qtDrmFd < 0) {
        qWarning("[DisplayHandoff] Could not find Qt DRM fd");
    } else {
        qDebug("[DisplayHandoff] DRM master dropped (fd %d)", m_qtDrmFd);
        // Save the current CRTC state so we can restore it exactly after the
        // child exits. An atomic cleanup disables the CRTC (CRTC_ACTIVE=0);
        // without this restore, Qt EGLFS gets EINVAL on its next page flip.
        saveDrmCrtcState(m_qtDrmFd);
    }
    return freeVt;
#else
    return 0;
#endif
}

void DisplayHandoff::releaseDeferred(const QString &owner,
                                     std::function<void()> onRestored,
                                     int delayMs) {
    if (!m_owner.isEmpty() && m_owner != owner) {
        // Not ours to release. Still run the callback — the caller needs it to
        // report that its process ended.
        qWarning("[DisplayHandoff] %s tried to release a hand-off held by %s",
                 qPrintable(owner), qPrintable(m_owner));
        if (onRestored) onRestored();
        return;
    }
    m_onRestored = std::move(onRestored);
    m_releaseTimer->start(delayMs);
}

void DisplayHandoff::releaseNow(const QString &owner) {
    if (m_owner.isEmpty() || m_owner != owner)
        return;
    m_releaseTimer->stop();
    m_onRestored = nullptr;
    doRestore();
}

void DisplayHandoff::doRestore() {
#ifdef Q_OS_LINUX
    if (m_qtDrmFd >= 0) {
        if (::ioctl(m_qtDrmFd, DRM_IOCTL_SET_MASTER, 0) < 0) {
            qWarning("[DisplayHandoff] drmSetMaster failed: %s", strerror(errno));
        } else {
            qDebug("[DisplayHandoff] DRM master restored (fd %d)", m_qtDrmFd);
            // Restore the CRTC to its pre-hand-off state using legacy
            // drmModeSetCrtc. This re-enables the CRTC with the original mode and
            // Qt's last framebuffer, so EGLFS's first atomic page flip succeeds
            // instead of getting EINVAL from a disabled CRTC.
            restoreDrmCrtcState(m_qtDrmFd);
        }
        m_qtDrmFd = -1;
    }
#endif
    if (m_previousVt > 0) {
        qDebug("[DisplayHandoff] Switching back to VT %d", m_previousVt);
        int prevVt = m_previousVt;
        m_previousVt = -1;
        switchToVt(prevVt);
    }
    m_owner.clear();
}

int DisplayHandoff::getActiveVt() const {
#ifdef Q_OS_LINUX
    QFile f("/sys/class/tty/tty0/active");
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QString name = QString::fromLatin1(f.readAll()).trimmed();
    bool ok;
    int n = name.mid(3).toInt(&ok);
    return ok ? n : -1;
#else
    return -1;
#endif
}

int DisplayHandoff::findFreeVt() const {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) return 7;
    int n = -1;
    ::ioctl(fd, VT_OPENQRY, &n);
    ::close(fd);
    return (n > 0) ? n : 7;
#else
    return -1;
#endif
}

void DisplayHandoff::switchToVt(int vt) {
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) {
        qWarning("[DisplayHandoff] switchToVt %d: open /dev/tty0 failed: %s", vt, strerror(errno));
        return;
    }
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0)
        qWarning("[DisplayHandoff] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
    if (::ioctl(fd, VT_WAITACTIVE, vt) < 0)
        qWarning("[DisplayHandoff] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
    ::close(fd);
#else
    Q_UNUSED(vt)
#endif
}

int DisplayHandoff::findQtDrmFd() const {
#ifdef Q_OS_LINUX
    // Scan the process's open file descriptors for Qt's DRM primary card
    // device. DRM primary nodes have major=226, minor 0-63 (card0, card1…).
    // We try DRM_IOCTL_DROP_MASTER on each candidate — it succeeds only on
    // the fd that currently holds DRM master, which tells us it's Qt's fd.
    QDir fdDir("/proc/self/fd");
    const QStringList entries = fdDir.entryList(QDir::Files | QDir::System);
    for (const QString &entry : entries) {
        bool ok;
        int fd = entry.toInt(&ok);
        if (!ok) continue;
        struct stat st;
        if (::fstat(fd, &st) < 0) continue;
        if (!S_ISCHR(st.st_mode)) continue;
        if (major(st.st_rdev) != 226) continue;   // not a DRM device
        if (minor(st.st_rdev) >= 64) continue;    // render node, not primary card
        // Found a DRM primary fd — try to drop master; if it works, this is it.
        if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == 0)
            return fd;
    }
    return -1;
#else
    return -1;
#endif
}

#ifdef Q_OS_LINUX
void DisplayHandoff::saveDrmCrtcState(int fd) {
    m_savedDrm = {};

    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        qWarning("[DisplayHandoff] saveDrmCrtcState: drmModeGetResources failed");
        return;
    }

    for (int i = 0; i < res->count_crtcs && !m_savedDrm.valid; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) continue;

        if (crtc->mode_valid) {
            m_savedDrm.crtcId = crtc->crtc_id;
            m_savedDrm.fbId   = crtc->buffer_id;
            m_savedDrm.x      = crtc->x;
            m_savedDrm.y      = crtc->y;
            m_savedDrm.mode   = crtc->mode;

            // Find the connector whose encoder is driving this CRTC
            for (int j = 0; j < res->count_connectors; ++j) {
                drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[j]);
                if (!conn) continue;
                if (conn->encoder_id) {
                    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        if (enc->crtc_id == m_savedDrm.crtcId) {
                            m_savedDrm.connectorId = conn->connector_id;
                            m_savedDrm.valid = true;
                        }
                        drmModeFreeEncoder(enc);
                    }
                }
                drmModeFreeConnector(conn);
                if (m_savedDrm.valid) break;
            }
        }
        drmModeFreeCrtc(crtc);
    }
    drmModeFreeResources(res);

    if (m_savedDrm.valid)
        qDebug("[DisplayHandoff] Saved CRTC %u connector %u mode %dx%d@%d",
               m_savedDrm.crtcId, m_savedDrm.connectorId,
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);
    else
        qWarning("[DisplayHandoff] Could not save CRTC state");
}

void DisplayHandoff::restoreDrmCrtcState(int fd) {
    if (!m_savedDrm.valid) return;

    int ret = drmModeSetCrtc(fd,
                              m_savedDrm.crtcId,
                              m_savedDrm.fbId,
                              m_savedDrm.x, m_savedDrm.y,
                              &m_savedDrm.connectorId, 1,
                              &m_savedDrm.mode);
    if (ret < 0)
        qWarning("[DisplayHandoff] drmModeSetCrtc restore failed: %s", strerror(errno));
    else
        qDebug("[DisplayHandoff] CRTC restored (mode %dx%d@%d)",
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);

    m_savedDrm.valid = false;
}
#endif
