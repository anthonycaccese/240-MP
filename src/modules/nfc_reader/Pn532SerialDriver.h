#pragma once
#include "NfcDriver.h"
#include "SerialPort.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QSet>

// PN532 over USB serial.
//
// This is the portable path: a PN532 USB module is the chip behind a
// CH340/CP210x/FTDI USB-serial bridge, so it needs no daemon and no driver
// package — only the usb-serial kernel module every distro already ships, plus
// read/write permission on the device node. That is what makes it work on an
// immutable distro like SteamOS, where the PC/SC stack (libpcsclite client →
// host pcscd → CCID driver) cannot be installed.
//
// Speaks the PN532 UART protocol directly rather than going through libnfc.
// Zaparoo reached the same conclusion in Core v2.6.0, replacing their libnfc
// PN532 driver with a native one to get rid of the C library dependency.
class Pn532SerialDriver : public NfcDriver {
public:
    QString id() const override { return QStringLiteral("pn532"); }
    bool ensureConnected() override;
    QString deviceName() const override { return m_deviceName; }
    QString pollUid(bool &ok) override;
    void close() override;

private:
    // Wake the chip and confirm it answers GetFirmwareVersion. This is the
    // handshake that promotes a candidate port to "this really is a PN532".
    bool probe(const QString &path);
    bool configure();

    // One command/response exchange: frame `cmd` + `params`, write it, consume
    // the ACK, then read the response frame and verify it answers `cmd`.
    // `response` receives the payload with TFI and the command byte stripped.
    bool transceive(quint8 cmd, const QByteArray &params, QByteArray &response, int timeoutMs = 600);

    SerialPort m_port;
    QString m_deviceName;

    // Ports that opened but failed the firmware handshake. Without this, a
    // device that merely shares a USB-serial bridge chip with a PN532 gets
    // reopened on every detection pass, which for an Arduino-class board means
    // a reset every couple of seconds. Entries are dropped once the port stops
    // being enumerated, so unplugging and replugging retries cleanly.
    QSet<QString> m_rejected;
};
