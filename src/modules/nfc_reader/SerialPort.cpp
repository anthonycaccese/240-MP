#include "SerialPort.h"
#include "NfcDriver.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace {

// USB-serial bridges used by PN532 USB modules. Sourced from the same set
// Zaparoo probes (go-pn532 detection/uart), plus the newer WCH parts that ship
// on current boards. A port whose VID:PID is discoverable but absent here is
// never opened.
const char *const kAllowedVidPids[] = {
    "1a86:7523",  // QinHeng CH340 — the common PN532 USB module
    "1a86:5523",  // QinHeng CH341
    "1a86:55d4",  // QinHeng CH9102
    "10c4:ea60",  // Silicon Labs CP210x
    "0403:6001",  // FTDI FT232
    "0403:6015",  // FTDI FT231X
    "067b:2303",  // Prolific PL2303
};

[[maybe_unused]] bool vidPidAllowed(const QString &vidPid) {
    for (const char *allowed : kAllowedVidPids) {
        if (vidPid == QLatin1String(allowed)) return true;
    }
    return false;
}

// A name or product string that says "NFC" outright. Used only to rank
// candidates, never to admit one that failed the VID:PID check.
bool looksLikeNfc(const QString &text) {
    const QString lower = text.toLower();
    return lower.contains("pn532") || lower.contains("nfc") || lower.contains("rfid");
}

[[maybe_unused]] QString readSysAttr(const QString &dir, const QString &name) {
    QFile f(dir + "/" + name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const QString &path, int baud) {
    close();

    // O_NONBLOCK is required on the open itself, not just afterwards: opening a
    // tty without it blocks until carrier is asserted, which for a device that
    // never raises DCD is forever. Cleared immediately after, so reads and
    // writes below are ordinary blocking calls governed by poll().
    const int fd = ::open(path.toUtf8().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (nfcDebugEnabled())
            qDebug("[NfcReader] open(%s) failed: %s", qPrintable(path), strerror(errno));
        return false;
    }

    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        ::close(fd);
        return false;
    }

    termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        ::close(fd);
        return false;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;  // the PN532 uses no flow control
    // HUPCL drops DTR on close, which pulses reset on boards that wire DTR to
    // the MCU. Harmless for a PN532, but this port gets opened during probing
    // on devices that turn out to be something else.
    tio.c_cflag &= ~HUPCL;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;  // timing is poll()'s job, not termios' decisecond VTIME

    speed_t speed = B115200;
    if (baud == 9600) speed = B9600;
    else if (baud == 38400) speed = B38400;
    else if (baud == 57600) speed = B57600;
    else if (baud == 230400) speed = B230400;
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        ::close(fd);
        return false;
    }

    // Keep the modem control lines low. Same reasoning as HUPCL above: it
    // limits how much a probe disturbs a device that isn't ours.
    int lines = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &lines);

    tcflush(fd, TCIOFLUSH);

    m_fd = fd;
    m_path = path;
    return true;
}

void SerialPort::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_path.clear();
}

bool SerialPort::write(const QByteArray &data) {
    if (m_fd < 0) return false;

    qsizetype written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(m_fd, data.constData() + written, size_t(data.size() - written));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += n;
    }
    return true;
}

QByteArray SerialPort::read(int count, int timeoutMs) {
    QByteArray out;
    if (m_fd < 0 || count <= 0) return out;

    QElapsedTimer elapsed;
    elapsed.start();

    while (out.size() < count) {
        const qint64 remaining = timeoutMs - elapsed.elapsed();
        if (remaining <= 0) break;

        pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, int(remaining));
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) break;  // timed out

        char buf[256];
        const size_t want = qMin(size_t(count - out.size()), sizeof(buf));
        const ssize_t n = ::read(m_fd, buf, want);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;  // device went away
        out.append(buf, n);
    }
    return out;
}

void SerialPort::flushInput() {
    if (m_fd >= 0) tcflush(m_fd, TCIFLUSH);
}

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

#if defined(Q_OS_LINUX)

namespace {

// Walk up from a tty's sysfs device link until a node carrying USB descriptors
// is found. ttyUSB0's device link points at the usb-serial interface, so the
// idVendor/idProduct live two or three levels up depending on the driver.
QString findUsbDeviceDir(const QString &ttyName) {
    QString dir = QFileInfo("/sys/class/tty/" + ttyName + "/device").canonicalFilePath();
    for (int i = 0; i < 5 && !dir.isEmpty(); ++i) {
        if (QFile::exists(dir + "/idVendor") && QFile::exists(dir + "/idProduct"))
            return dir;
        const QString parent = QFileInfo(dir).path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

} // namespace

QList<SerialPortInfo> SerialPort::enumerateCandidates() {
    QList<SerialPortInfo> found;

    const QStringList names =
        QDir("/sys/class/tty").entryList({"ttyUSB*", "ttyACM*"}, QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &name : names) {
        SerialPortInfo info;
        info.path = "/dev/" + name;
        if (!QFile::exists(info.path)) continue;

        const QString usbDir = findUsbDeviceDir(name);
        if (!usbDir.isEmpty()) {
            const QString vid = readSysAttr(usbDir, "idVendor").toLower();
            const QString pid = readSysAttr(usbDir, "idProduct").toLower();
            if (!vid.isEmpty() && !pid.isEmpty())
                info.vidPid = vid + ":" + pid;
            const QString manufacturer = readSysAttr(usbDir, "manufacturer");
            const QString product = readSysAttr(usbDir, "product");
            info.description = QStringList({manufacturer, product}).join(' ').trimmed();
        }

        // No descriptors means we cannot tell what this is, and on Linux that
        // is rare enough (it implies a non-USB tty) that rejecting is safer
        // than probing.
        if (info.vidPid.isEmpty() || !vidPidAllowed(info.vidPid)) {
            if (nfcDebugEnabled()) {
                qDebug("[NfcReader] Skipping %s (vid:pid %s, %s) - not an allowlisted USB-serial bridge",
                       qPrintable(info.path),
                       info.vidPid.isEmpty() ? "unknown" : qPrintable(info.vidPid),
                       qPrintable(info.description));
            }
            continue;
        }
        found.append(info);
    }

    std::stable_sort(found.begin(), found.end(), [](const SerialPortInfo &a, const SerialPortInfo &b) {
        return looksLikeNfc(a.description) && !looksLikeNfc(b.description);
    });
    return found;
}

#elif defined(Q_OS_MAC)

QList<SerialPortInfo> SerialPort::enumerateCandidates() {
    QList<SerialPortInfo> found;

    // /dev/cu.* rather than /dev/tty.*: the callout device does not block on
    // DCD, which the dial-in device does.
    //
    // macOS exposes no VID:PID without shelling out to ioreg, so candidacy is
    // decided by the node name, which encodes the driver that claimed the
    // device. That is a coarser filter than Linux's, but the prefixes below are
    // all USB-serial bridges.
    static const QStringList kPatterns = {
        "cu.usbserial*",       // FTDI and generic bridges
        "cu.wchusbserial*",    // WinChipHead CH340/CH341
        "cu.SLAB_USBtoUART*",  // Silicon Labs CP210x
        "cu.usbmodem*",        // CDC-ACM
    };

    const QDir dev("/dev");
    for (const QString &pattern : kPatterns) {
        const QStringList names = dev.entryList({pattern},
                                                QDir::System | QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &name : names) {
            SerialPortInfo info;
            info.path = "/dev/" + name;
            info.description = name;
            if (std::none_of(found.cbegin(), found.cend(),
                             [&](const SerialPortInfo &e) { return e.path == info.path; })) {
                found.append(info);
            }
        }
    }

    // cu.usbmodem is also what Arduino-class CDC boards appear as, so it is
    // tried last: a real reader on a bridge chip is matched before anything
    // that might merely be sharing the prefix.
    std::stable_sort(found.begin(), found.end(), [](const SerialPortInfo &a, const SerialPortInfo &b) {
        const bool aModem = a.path.contains("usbmodem");
        const bool bModem = b.path.contains("usbmodem");
        if (aModem != bModem) return bModem;
        return looksLikeNfc(a.description) && !looksLikeNfc(b.description);
    });
    return found;
}

#else

QList<SerialPortInfo> SerialPort::enumerateCandidates() {
    return {};
}

#endif
