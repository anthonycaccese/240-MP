#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

// A USB-serial device that could be hosting a PN532.
struct SerialPortInfo {
    QString path;         // "/dev/ttyUSB0", "/dev/cu.usbserial-1420"
    QString vidPid;       // "1a86:7523", lowercase; empty when not discoverable
    QString description;  // manufacturer/product, best effort; may be empty
};

// Minimal blocking serial port on POSIX termios.
//
// Deliberately not Qt6::SerialPort: the whole point of the PN532 path is that
// it adds no build or runtime dependency, so it runs on an immutable distro
// with nothing installed. Qt SerialPort would mean a new find_package
// component, a Qt module on every build host, and another bundled AppImage
// library — for an open/read/write wrapper this file implements in ~200 lines.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;

    bool open(const QString &path, int baud = 115200);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    QString path() const { return m_path; }

    bool write(const QByteArray &data);

    // Reads until `count` bytes have arrived or `timeoutMs` elapses, whichever
    // comes first; a short return means the timeout won. The timeout is over
    // the whole call, not per byte.
    QByteArray read(int count, int timeoutMs);

    // Drops anything already buffered. Called before each command so a stale
    // response from a previous, timed-out exchange can't be mistaken for the
    // answer to this one.
    void flushInput();

    // USB-serial devices plausibly hosting a PN532, most likely first.
    //
    // This is an allowlist, not a blocklist: opening an arbitrary serial device
    // asserts DTR, which resets Arduino-class boards, and some devices (light
    // guns, 3D printers) misbehave when probed. Only ports matching a known
    // USB-serial bridge are ever opened.
    static QList<SerialPortInfo> enumerateCandidates();

private:
    int m_fd = -1;
    QString m_path;
};
