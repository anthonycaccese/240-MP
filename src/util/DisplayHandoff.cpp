#include "DisplayHandoff.h"

#include <cstring>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/mman.h>
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
    //
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
    // A same-owner re-acquire while held is refused too: proceeding would
    // overwrite m_previousVt with the free VT we are currently on, losing the
    // real VT to return to. Callers that relaunch a child use isHeldBy() and
    // skip acquire() entirely (see MpvController).
    if (isHeldBy(owner)) {
        qWarning("[DisplayHandoff] %s tried to acquire the screen it already holds",
                 qPrintable(owner));
        return -1;
    }
    if (!isHeadless())
        return 0;

    m_owner      = owner;
    m_previousVt = getActiveVt();
    m_qtDrmFd    = -1;

#ifdef Q_OS_LINUX
    // VT switch first — suspends Qt's render thread via the kernel's VT switch
    // signal before DRM master is dropped, which is what keeps the
    // "Failed to commit atomic request (code=-13)" (EACCES) noise down. Expect a
    // handful of those lines anyway: Qt can still have commits in flight between
    // the switch completing and the master drop. Pi-observed as 1-4 lines per
    // hand-off, with the display recovering correctly. A continuous stream of them
    // instead means the VT switch silently failed — see the warning below.
    const int freeVt = findFreeVt(m_previousVt);
    m_switchedVt = switchToVt(freeVt);
    if (!m_switchedVt) {
        // This is not cosmetic. Without the switch, Qt's renderer keeps drawing
        // while DRM master is dropped below it: expect a stream of
        // "Failed to commit atomic request (code=-13)" (EACCES), and a child that
        // draws nothing will leave the 240-MP UI on screen rather than blanking,
        // so a "takeover" doesn't visibly take over. It usually still recovers,
        // which is exactly what makes it easy to miss — so say so plainly.
        qWarning("[DisplayHandoff] Could not switch VT, so Qt's renderer will keep "
                 "drawing while DRM master is dropped. This process needs "
                 "CAP_SYS_TTY_CONFIG or membership of the 'tty' group, and "
                 "/dev/tty0 must be group-writable. The installed systemd service "
                 "grants both (see scripts/install.sh); a hand-launched dev build "
                 "may not.");
    }

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
    // Only switch back if we actually switched away — otherwise this just logs a
    // second, confusing permission error on a VT we never left.
    if (m_previousVt > 0 && m_switchedVt) {
        qDebug("[DisplayHandoff] Switching back to VT %d", m_previousVt);
        switchToVt(m_previousVt);
    }
    m_previousVt = -1;
    m_switchedVt = false;
    m_owner.clear();

    emit displayReturned();
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

int DisplayHandoff::findFreeVt(int activeVt) const {
#ifdef Q_OS_LINUX
    int n = -1;
    const int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd >= 0) {
        ::ioctl(fd, VT_OPENQRY, &n);
        ::close(fd);
    }
    if (n <= 0 || n > 63) n = 7;

    // VT_OPENQRY reports the lowest VT that no process currently has OPEN, which
    // is NOT the same as "a VT other than the one we are displaying on". Under
    // the installed service getty@tty1 and autovt@ are masked and 240mp.service
    // opens no tty of its own, so nothing holds /dev/tty1 open and this can hand
    // back the very VT Qt is on.
    //
    // Switching to the VT we are already on is a silent no-op: the kernel sends
    // no VT-switch signal, so Qt's renderer never suspends, and we then drop DRM
    // master out from under a still-drawing Qt. The symptoms are a continuous
    // stream of "Failed to commit atomic request (code=-13)" and a takeover that
    // doesn't visibly take over — with m_switchedVt true, so nothing warns.
    //
    // Any other VT will do. A VT does not have to be free to be activated; being
    // unused is merely preferable, because then no getty repaints over the child.
    if (n == activeVt) n = (activeVt < 63) ? activeVt + 1 : activeVt - 1;
    return n;
#else
    Q_UNUSED(activeVt)
    return -1;
#endif
}

#ifdef Q_OS_LINUX
// Opens a tty suitable for issuing VT_ACTIVATE.
//
// Deliberately prefers OUR OWN console over /dev/tty0. /dev/tty0 resolves to
// whichever VT is currently in the foreground, and the kernel permits VT_ACTIVATE
// only when the opened tty is the caller's controlling terminal or the caller has
// CAP_SYS_TTY_CONFIG. So with /dev/tty0 the switch AWAY succeeds (we are still the
// foreground console) but the switch BACK fails with EPERM — the app is left
// stranded on the VT it moved to, and the next hand-off starts from there,
// walking up a VT each time. Opening /dev/tty<our own VT> keeps the check
// satisfied in both directions, since that tty stays our controlling terminal.
//
// The installed systemd service has CAP_SYS_TTY_CONFIG and no controlling
// terminal at all, so either path works there; this is what makes a hand-launched
// dev build behave the same as the service.
int DisplayHandoff::openVtControlFd() const {
    if (m_previousVt > 0) {
        const QByteArray own = QByteArray("/dev/tty") + QByteArray::number(m_previousVt);
        const int fd = ::open(own.constData(), O_WRONLY);
        if (fd >= 0)
            return fd;
    }
    return ::open("/dev/tty0", O_WRONLY);
}
#endif

bool DisplayHandoff::switchToVt(int vt) {
#ifdef Q_OS_LINUX
    int fd = openVtControlFd();
    if (fd < 0) {
        qWarning("[DisplayHandoff] switchToVt %d: cannot open a control tty: %s",
                 vt, strerror(errno));
        return false;
    }
    bool ok = true;
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0) {
        qWarning("[DisplayHandoff] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
        ok = false;
    }
    if (::ioctl(fd, VT_WAITACTIVE, vt) < 0) {
        qWarning("[DisplayHandoff] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
        ok = false;
    }
    ::close(fd);
    return ok;
#else
    Q_UNUSED(vt)
    return false;
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

bool DisplayHandoff::ensureBlankFb(int fd, uint32_t width, uint32_t height) {
    if (fd < 0 || width == 0 || height == 0) return false;

    if (m_blankFbId) {
        if (m_blankWidth == width && m_blankHeight == height) return true;
        drmModeRmFB(fd, m_blankFbId);
        drmModeDestroyDumbBuffer(fd, m_blankHandle);
        m_blankFbId = m_blankHandle = 0;
        m_blankWidth = m_blankHeight = 0;
    }

    uint32_t handle = 0, pitch = 0;
    uint64_t size = 0;
    if (drmModeCreateDumbBuffer(fd, width, height, 32, 0, &handle, &pitch, &size) != 0) {
        qWarning("[DisplayHandoff] could not allocate a blank framebuffer: %s", strerror(errno));
        return false;
    }

    uint64_t offset = 0;
    if (drmModeMapDumbBuffer(fd, handle, &offset) != 0) {
        qWarning("[DisplayHandoff] could not map the blank framebuffer: %s", strerror(errno));
        drmModeDestroyDumbBuffer(fd, handle);
        return false;
    }
    void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     static_cast<off_t>(offset));
    if (map == MAP_FAILED) {
        qWarning("[DisplayHandoff] could not mmap the blank framebuffer: %s", strerror(errno));
        drmModeDestroyDumbBuffer(fd, handle);
        return false;
    }
    std::memset(map, 0, static_cast<size_t>(size));
    munmap(map, static_cast<size_t>(size));

    uint32_t fbId = 0;
    if (drmModeAddFB(fd, width, height, 24, 32, pitch, handle, &fbId) != 0) {
        qWarning("[DisplayHandoff] could not register the blank framebuffer: %s", strerror(errno));
        drmModeDestroyDumbBuffer(fd, handle);
        return false;
    }

    m_blankFbId   = fbId;
    m_blankHandle = handle;
    m_blankWidth  = width;
    m_blankHeight = height;
    qDebug("[DisplayHandoff] blank framebuffer ready (%ux%u, fb %u)", width, height, fbId);
    return true;
}

void DisplayHandoff::restoreDrmCrtcState(int fd) {
    if (!m_savedDrm.valid) return;

    //
    const bool haveBlank = ensureBlankFb(fd, m_savedDrm.mode.hdisplay,
                                             m_savedDrm.mode.vdisplay);
    const uint32_t fbId = haveBlank ? m_blankFbId : m_savedDrm.fbId;

    int ret = drmModeSetCrtc(fd,
                              m_savedDrm.crtcId,
                              fbId,
                              m_savedDrm.x, m_savedDrm.y,
                              &m_savedDrm.connectorId, 1,
                              &m_savedDrm.mode);
    if (ret < 0) {
        qWarning("[DisplayHandoff] drmModeSetCrtc restore failed: %s", strerror(errno));
        if (haveBlank) {
            ret = drmModeSetCrtc(fd, m_savedDrm.crtcId, m_savedDrm.fbId,
                                 m_savedDrm.x, m_savedDrm.y,
                                 &m_savedDrm.connectorId, 1, &m_savedDrm.mode);
            if (ret == 0)
                qDebug("[DisplayHandoff] CRTC restored with Qt's framebuffer instead");
        }
    } else {
        qDebug("[DisplayHandoff] CRTC restored %s (mode %dx%d@%d)",
               haveBlank ? "blank" : "with Qt's framebuffer",
               m_savedDrm.mode.hdisplay, m_savedDrm.mode.vdisplay,
               m_savedDrm.mode.vrefresh);
    }

    m_savedDrm.valid = false;
}
#endif
