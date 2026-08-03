#include "Pn532SerialDriver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

namespace {

// PN532 user manual (UM0701-02), §6.2 "Host controller communication protocol".
constexpr quint8 kHostToPn532 = 0xD4;
constexpr quint8 kPn532ToHost = 0xD5;

constexpr quint8 kCmdGetFirmwareVersion  = 0x02;
constexpr quint8 kCmdSAMConfiguration    = 0x14;
constexpr quint8 kCmdRFConfiguration     = 0x32;
constexpr quint8 kCmdInListPassiveTarget = 0x4A;
constexpr quint8 kCmdInRelease           = 0x52;

// Normal information frame: 00 00 FF LEN LCS TFI DATA... DCS 00
QByteArray buildFrame(quint8 cmd, const QByteArray &params) {
    QByteArray frame;
    frame.append('\x00');                       // preamble
    frame.append('\x00');                       // start code
    frame.append('\xFF');
    const int len = params.size() + 2;          // TFI + cmd + params
    frame.append(char(len));
    frame.append(char((0x100 - len) & 0xFF));   // length checksum

    quint8 sum = kHostToPn532 + cmd;
    frame.append(char(kHostToPn532));
    frame.append(char(cmd));
    for (char c : params) {
        frame.append(c);
        sum += quint8(c);
    }
    frame.append(char((0x100 - sum) & 0xFF));   // data checksum
    frame.append('\x00');                       // postamble
    return frame;
}

enum class FrameKind { None, Ack, Nack, Error, Data };

// Slide over incoming bytes until the 00 FF start code appears. Cheap boards
// emit extra preamble bytes and leftovers from a previous timed-out exchange,
// so resynchronising beats assuming the frame starts where we left off.
bool syncToStartCode(SerialPort &port, int timeoutMs) {
    QElapsedTimer elapsed;
    elapsed.start();
    quint8 prev = 0xFF;
    bool havePrev = false;
    while (elapsed.elapsed() < timeoutMs) {
        const QByteArray b = port.read(1, int(timeoutMs - elapsed.elapsed()));
        if (b.isEmpty()) return false;
        const quint8 cur = quint8(b.at(0));
        if (havePrev && prev == 0x00 && cur == 0xFF) return true;
        prev = cur;
        havePrev = true;
    }
    return false;
}

// Reads one frame. `payload` receives TFI + everything after it, so a data
// frame's payload[0] is 0xD5 and payload[1] is the response command byte.
FrameKind readFrame(SerialPort &port, QByteArray &payload, int timeoutMs) {
    payload.clear();
    QElapsedTimer elapsed;
    elapsed.start();

    if (!syncToStartCode(port, timeoutMs)) return FrameKind::None;

    const int remaining = qMax(1, timeoutMs - int(elapsed.elapsed()));
    const QByteArray header = port.read(2, remaining);
    if (header.size() < 2) return FrameKind::None;

    const quint8 len = quint8(header.at(0));
    const quint8 lcs = quint8(header.at(1));

    if (len == 0x00 && lcs == 0xFF) {
        port.read(1, 50);  // postamble
        return FrameKind::Ack;
    }
    if (len == 0xFF && lcs == 0x00) {
        port.read(1, 50);
        return FrameKind::Nack;
    }
    // 0xFF here would be an extended frame; none of the commands this driver
    // sends can provoke one, so treat it as a desync rather than parsing it.
    if (len == 0xFF) return FrameKind::None;
    if (quint8(len + lcs) != 0x00) return FrameKind::None;

    const int rest = qMax(1, timeoutMs - int(elapsed.elapsed()));
    const QByteArray body = port.read(len + 1, rest);  // TFI + data + DCS
    if (body.size() < len + 1) return FrameKind::None;

    quint8 sum = 0;
    for (int i = 0; i < len; ++i) sum += quint8(body.at(i));
    if (quint8(sum + quint8(body.at(len))) != 0x00) return FrameKind::None;

    port.read(1, 50);  // postamble

    payload = body.left(len);
    if (quint8(payload.at(0)) == 0x7F) return FrameKind::Error;
    return FrameKind::Data;
}

QString formatUid(const QByteArray &nfcid) {
    return nfcid.toHex(':').toUpper();
}

} // namespace

bool Pn532SerialDriver::transceive(quint8 cmd, const QByteArray &params,
                                   QByteArray &response, int timeoutMs) {
    if (!m_port.isOpen()) return false;

    m_port.flushInput();
    if (!m_port.write(buildFrame(cmd, params))) return false;

    // The chip ACKs first and answers second. Both have to arrive within the
    // budget, so the ACK gets a slice of it rather than the whole thing.
    QByteArray payload;
    if (readFrame(m_port, payload, qMin(timeoutMs, 200)) != FrameKind::Ack) return false;

    if (readFrame(m_port, payload, timeoutMs) != FrameKind::Data) return false;
    if (payload.size() < 2) return false;
    if (quint8(payload.at(0)) != kPn532ToHost) return false;
    if (quint8(payload.at(1)) != quint8(cmd + 1)) return false;

    response = payload.mid(2);
    return true;
}

bool Pn532SerialDriver::probe(const QString &path) {
    if (!m_port.open(path)) return false;

    static const QByteArray kWakeup =
        QByteArray("\x55\x55", 2) + QByteArray(14, '\x00');
    // Backoff between whole attempts. Deliberately NOT between the wake-up and
    // the command that follows it — see below.
    static const int kRetryDelayMs[] = {30, 100, 250};

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) QThread::msleep(kRetryDelayMs[attempt - 1]);

        m_port.flushInput();
        if (!m_port.write(kWakeup)) break;

        // Two hard requirements, both taken from libnfc's wire trace rather
        // than guessed. Getting either wrong produces total silence at every
        // baud rate on every OS, which is indistinguishable from dead hardware:
        //
        //  1. SAMConfiguration must be the FIRST command after the wake-up
        //     burst. The preamble only gets the chip far enough to receive;
        //     until it is configured it stays in LowVbat and ignores anything
        //     else, so probing with GetFirmwareVersion never gets a reply.
        //
        //  2. It must follow the burst IMMEDIATELY. Even a 100 ms pause lets
        //     the chip settle back down and the handshake never completes.
        //     libnfc issues the two writes back to back with nothing but an
        //     input flush between them, which is exactly what happens here
        //     since transceive() flushes before it writes. Do not add a
        //     "settle" delay in this gap; it looks harmless and breaks
        //     everything.
        //
        // Single parameter (normal mode), matching libnfc: the timeout and IRQ
        // bytes are only meaningful for virtual-card mode.
        QByteArray samResponse;
        if (!transceive(kCmdSAMConfiguration, QByteArray("\x01", 1), samResponse, 1000)) continue;

        QByteArray version;
        if (!transceive(kCmdGetFirmwareVersion, {}, version, 400)) continue;
        if (version.size() < 4) continue;

        // IC, Ver, Rev, Support — IC is 0x32 for a PN532.
        m_deviceName = QStringLiteral("PN532 v%1.%2 (%3)")
                           .arg(quint8(version.at(1)))
                           .arg(quint8(version.at(2)))
                           .arg(path);
        qInfo("[NfcReader] Found %s", qPrintable(m_deviceName));
        return true;
    }

    if (nfcDebugEnabled()) {
        // Report whatever did arrive, if anything. Total silence means nothing
        // is reaching the chip at all (wrong interface mode, miswired adapter,
        // or a USB-serial driver that enumerates but never puts bytes on the
        // wire); stray bytes mean something is talking but not PN532 HSU, which
        // usually points at the wrong baud rate or a different device.
        const QByteArray stray = m_port.read(64, 200);
        qDebug("[NfcReader] %s did not complete the PN532 handshake - not a PN532 (%s)",
               qPrintable(path),
               stray.isEmpty() ? "no bytes received at all"
                               : qPrintable(QStringLiteral("%1 stray bytes: %2")
                                                .arg(stray.size())
                                                .arg(QString::fromLatin1(stray.toHex(' ')))));
    }
    m_port.close();
    return false;
}

// Runs after a successful probe(), which has already issued SAMConfiguration —
// that one is part of the wake-up handshake, not optional setup.
bool Pn532SerialDriver::configure() {
    QByteArray response;

    // CfgItem 0x05 = MaxRetries {MxRtyATR, MxRtyPSL, MxRtyPassiveActivation}.
    //
    // Load-bearing: MxRtyPassiveActivation defaults to "retry forever", which
    // makes InListPassiveTarget block until a card appears. That would stall
    // the poll tick indefinitely and trip the backend's stall watchdog, so it
    // is pinned to a single attempt — the polling loop provides the retries.
    if (!transceive(kCmdRFConfiguration, QByteArray("\x05\xFF\x01\x01", 4), response))
        return false;

    return true;
}

bool Pn532SerialDriver::ensureConnected() {
    if (m_port.isOpen()) return true;

    // Escape hatch for the auto-detect-only design: a reader on a bridge chip
    // outside the allowlist, or a machine with several matching ports, can be
    // pinned without a rebuild.
    const QString forced = qEnvironmentVariable("MP240_NFC_SERIAL_DEVICE");
    if (!forced.isEmpty()) {
        if (!probe(forced)) return false;
        if (!configure()) {
            close();
            return false;
        }
        return true;
    }

    const QList<SerialPortInfo> candidates = SerialPort::enumerateCandidates();

    // Forget rejections for ports that have gone away, so unplugging whatever
    // was on /dev/ttyUSB0 and plugging in a reader is picked up.
    QSet<QString> present;
    for (const SerialPortInfo &info : candidates) present.insert(info.path);
    m_rejected.intersect(present);

    // Probing a device that turns out not to be a PN532 costs the full wake-up
    // retry ladder (~1 s), and the worker emits no sample while it runs. A
    // machine with several USB-serial devices would otherwise go quiet for long
    // enough on the first pass to trip the backend's stall watchdog, so the work
    // is spread across passes — rejections are remembered, so the list drains.
    int budget = 2;

    for (const SerialPortInfo &info : candidates) {
        if (m_rejected.contains(info.path)) continue;
        if (budget-- <= 0) break;
        if (nfcDebugEnabled()) {
            qDebug("[NfcReader] Probing %s (vid:pid %s, %s)", qPrintable(info.path),
                   info.vidPid.isEmpty() ? "unknown" : qPrintable(info.vidPid),
                   qPrintable(info.description));
        }
        if (!probe(info.path)) {
            m_rejected.insert(info.path);
            continue;
        }
        if (!configure()) {
            qWarning("[NfcReader] %s answered but could not be configured", qPrintable(info.path));
            close();
            m_rejected.insert(info.path);
            continue;
        }
        return true;
    }
    return false;
}

QString Pn532SerialDriver::pollUid(bool &ok) {
    if (!m_port.isOpen()) {
        ok = false;
        return {};
    }

    // The device node disappearing is the unplug signal; without this check a
    // read just times out and looks like a wedge.
    if (!QFile::exists(m_port.path())) {
        ok = false;
        return {};
    }

    QByteArray response;
    // 1 target, 106 kbps type A. Returns promptly with NbTg 0 when the field is
    // empty, because configure() pinned passive-activation retries to 1.
    if (!transceive(kCmdInListPassiveTarget, QByteArray("\x01\x00", 2), response, 800)) {
        ok = false;
        return {};
    }

    ok = true;
    if (response.isEmpty() || quint8(response.at(0)) == 0) return {};

    // NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID1...
    if (response.size() < 6) return {};
    const int idLength = quint8(response.at(5));
    if (idLength <= 0 || response.size() < 6 + idLength) return {};

    const QString uid = formatUid(response.mid(6, idLength));

    // Release the target so the RF field cycles. Without it the card stays
    // selected and the next InListPassiveTarget can keep reporting it after it
    // has physically been lifted, which would break removal detection.
    QByteArray releaseResponse;
    transceive(kCmdInRelease, QByteArray("\x00", 1), releaseResponse, 300);

    return uid;
}

void Pn532SerialDriver::close() {
    m_port.close();
    m_deviceName.clear();
}
