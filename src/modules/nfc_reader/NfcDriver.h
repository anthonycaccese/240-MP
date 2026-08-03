#pragma once
#include <QString>
#include <QtGlobal>

// Verbose reader discovery logging. Auto-detection is the only way a reader is
// chosen (there is no picker setting), so when it picks nothing there is
// otherwise no way to tell why — which is unworkable on a machine you can only
// reach over SSH, like a Steam Deck. Follows the MP240_UPDATE_FEED_URL
// precedent of an env var rather than a user-visible setting.
inline bool nfcDebugEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("MP240_NFC_DEBUG");
    return enabled;
}

// One NFC reader transport. Drivers are owned by NfcPollWorker and live on its
// thread; none of them are thread-safe, and every method may block for as long
// as the underlying transport does. The worker's watchdog is what covers that.
//
// Deliberately narrower than a general NFC abstraction: 240-MP only ever reads
// a card's UID, so there is no write, no NDEF, and no capability negotiation.
class NfcDriver {
public:
    virtual ~NfcDriver() = default;

    // Stable short id, for logging: "pcsc", "pn532".
    virtual QString id() const = 0;

    // Find and open a device. Called on every detection pass while no driver is
    // connected, so it must be cheap when there is nothing to find. Returns
    // true once a device is open and ready to poll.
    virtual bool ensureConnected() = 0;

    // Human-readable name of the open device, shown in the UI. Empty when
    // closed.
    virtual QString deviceName() const = 0;

    // One poll tick against an already-connected device. Returns the card UID
    // as colon-separated uppercase hex, or an empty string when no card is on
    // the reader. Sets `ok` false on a transport error, which tells the worker
    // to close() this driver and fall back to detection.
    virtual QString pollUid(bool &ok) = 0;

    virtual void close() = 0;
};
