#include "PcscDriver.h"

#include <QDebug>
#include <QStringList>

#include <cstring>

#if defined(MP240_NFC_PCSC_AVAILABLE) && (defined(Q_OS_LINUX) || defined(Q_OS_MAC))
#include <PCSC/winscard.h>
// Not pulled in by winscard.h on macOS; provides the DWORD/LONG typedefs that
// keep the SCard* calls portable (pcsclite widens them to long on 64-bit Linux).
#include <PCSC/wintypes.h>
#define NFC_PCSC_AVAILABLE
#endif

bool PcscDriver::compiledIn() {
#ifdef NFC_PCSC_AVAILABLE
    return true;
#else
    return false;
#endif
}

#ifdef NFC_PCSC_AVAILABLE

namespace {

// Substrings that identify a *contactless* reader. PC/SC enumerates contact
// readers too — a laptop smartcard slot, or a YubiKey in CCID mode — and those
// answer the Get-UID APDU with an error rather than a UID, so they must not win
// over a real NFC reader when both are plugged in.
//
// This is a ranking, not a filter: an unrecognised reader is still used when
// it's the only one present, which is what generalizes this driver beyond the
// ACR122U it originally hardcoded. "PICC" catches the contactless slot in the
// dual-interface naming convention (e.g. "... CL Reader PICC 00 00").
const char *const kContactlessHints[] = {
    "ACR122", "ACR12", "ACR13", "ACS",     "SCL3711", "SCM Micro",
    "uTrust", "Identiv", "PN53", "Contactless", "PICC",
};

bool looksContactless(const QString &name) {
    for (const char *hint : kContactlessHints) {
        if (name.contains(QLatin1String(hint), Qt::CaseInsensitive)) return true;
    }
    return false;
}

} // namespace

QString PcscDriver::findReader() {
    if (!m_context) {
        SCARDCONTEXT newCtx = 0;
        if (SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &newCtx) != SCARD_S_SUCCESS) {
            return {};
        }
        m_context = static_cast<uintptr_t>(newCtx);
    }

    // DWORD/LONG (not uint32_t/int32_t): pcsclite on 64-bit Linux types these
    // as unsigned long/long, while macOS's PCSC framework uses 32-bit types.
    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);
    DWORD cchReaders = 0;
    LONG rv = SCardListReaders(ctx, nullptr, nullptr, &cchReaders);
    if (rv != SCARD_S_SUCCESS || cchReaders == 0) {
        // Covers both "no readers" and stale contexts (pcscd restart, last
        // reader unplugged); release so the next poll re-establishes.
        SCardReleaseContext(ctx);
        m_context = 0;
        return {};
    }

    char *mszReaders = new char[cchReaders];
    rv = SCardListReaders(ctx, nullptr, mszReaders, &cchReaders);
    if (rv != SCARD_S_SUCCESS) {
        delete[] mszReaders;
        SCardReleaseContext(ctx);
        m_context = 0;
        return {};
    }

    QString firstReader;
    QString contactlessReader;
    QStringList allReaders;
    for (char *p = mszReaders; *p; p += strlen(p) + 1) {
        const QString readerName = QString::fromUtf8(p);
        allReaders.append(readerName);
        if (firstReader.isEmpty()) firstReader = readerName;
        if (contactlessReader.isEmpty() && looksContactless(readerName))
            contactlessReader = readerName;
    }

    delete[] mszReaders;

    if (nfcDebugEnabled() && allReaders != m_lastListedReaders) {
        m_lastListedReaders = allReaders;
        qDebug("[NfcReader] PC/SC readers: %s", qPrintable(allReaders.join(", ")));
    }

    return contactlessReader.isEmpty() ? firstReader : contactlessReader;
}

bool PcscDriver::cardPresent(const QString &readerName) {
    // Ask for the reader's state instead of blindly calling SCardConnect every
    // poll: connecting while a card is mid-insertion/removal is what tends to
    // wedge ctkpcscd on macOS. MUTE means a card is present but unresponsive
    // (still settling); wait for a clean PRESENT before connecting.
    QByteArray name = readerName.toUtf8();
    SCARD_READERSTATE state;
    memset(&state, 0, sizeof(state));
    state.szReader = name.constData();
    state.dwCurrentState = SCARD_STATE_UNAWARE;

    LONG rv = SCardGetStatusChange(static_cast<SCARDCONTEXT>(m_context), 0, &state, 1);
    if (rv != SCARD_S_SUCCESS) return false;

    return (state.dwEventState & SCARD_STATE_PRESENT) &&
           !(state.dwEventState & SCARD_STATE_MUTE);
}

QString PcscDriver::readCardUid(const QString &readerName) {
    SCARDCONTEXT ctx = static_cast<SCARDCONTEXT>(m_context);
    const QByteArray name = readerName.toUtf8();

    SCARDHANDLE cardHandle = 0;
    DWORD dwActiveProtocol = 0;
    LONG rv = SCardConnect(ctx,
                              name.constData(),
                              SCARD_SHARE_SHARED,
                              SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                              &cardHandle,
                              &dwActiveProtocol);
    if (rv != SCARD_S_SUCCESS) return {};

    // FF CA 00 00 00 — the PC/SC "Get Data (UID)" pseudo-APDU. Part of the
    // PC/SC part 3 spec, not an ACS extension, so every CCID contactless
    // reader answers it.
    unsigned char sendBuffer[] = {0xFF, 0xCA, 0x00, 0x00, 0x00};
    unsigned char recvBuffer[256];
    DWORD recvLength = sizeof(recvBuffer);

    SCARD_IO_REQUEST ioRequest = {dwActiveProtocol, sizeof(SCARD_IO_REQUEST)};

    rv = SCardTransmit(cardHandle,
                       &ioRequest,
                       sendBuffer, sizeof(sendBuffer),
                       nullptr,
                       recvBuffer, &recvLength);

    SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);

    if (rv != SCARD_S_SUCCESS || recvLength < 2) return {};

    // A contact-only reader with a card in it (a YubiKey in CCID mode, say)
    // lands here with 6D 00 / 6A 81 rather than 90 00. Returning empty makes it
    // look like an idle reader, which is the right outcome: harmless, and a
    // real contactless reader outranks it in findReader() anyway.
    unsigned char sw1 = recvBuffer[recvLength - 2];
    unsigned char sw2 = recvBuffer[recvLength - 1];
    if (sw1 != 0x90 || sw2 != 0x00) return {};

    QByteArray uidBytes(reinterpret_cast<const char*>(recvBuffer), recvLength - 2);
    return uidBytes.toHex(':').toUpper();
}

PcscDriver::~PcscDriver() {
    close();
}

void PcscDriver::close() {
    if (m_context) {
        SCardReleaseContext(static_cast<SCARDCONTEXT>(m_context));
        m_context = 0;
    }
    m_readerName.clear();
    m_lastListedReaders.clear();
}

bool PcscDriver::ensureConnected() {
    const QString reader = findReader();
    m_readerName = reader;
    return !reader.isEmpty();
}

QString PcscDriver::pollUid(bool &ok) {
    // Re-resolve every tick rather than trusting the cached name: this is how
    // an unplug is noticed, and SCardListReaders is cheap. A changed name means
    // a different reader took over, which the worker handles as a reconnect.
    const QString reader = findReader();
    if (reader.isEmpty() || reader != m_readerName) {
        m_readerName = reader;
        ok = false;
        return {};
    }

    ok = true;
    if (!cardPresent(reader)) return {};
    return readCardUid(reader);
}

#else // !NFC_PCSC_AVAILABLE

PcscDriver::~PcscDriver() = default;
void PcscDriver::close() { m_readerName.clear(); }
bool PcscDriver::ensureConnected() { return false; }
QString PcscDriver::pollUid(bool &ok) { ok = false; return {}; }

#endif // NFC_PCSC_AVAILABLE
