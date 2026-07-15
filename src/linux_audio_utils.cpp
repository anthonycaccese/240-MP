#include "linux_audio_utils.h"
#include <QFile>
#include <QDir>
#include <QRegularExpression>

// ALSA's "default" pcm can resolve to card 0, and on this hardware card 0 is
// whatever happened to enumerate first, including a USB dongle (e.g. the
// remote receiver) that exposes a stub audio interface with no real playback
// path. That sends mpv down a dmix/pipewire fallback chain that dead-ends
// ("couldn't open play stream") instead of reaching the actual HDMI output.
// Bypass "default" entirely and target the vc4-hdmi ALSA card directly,
// preferring whichever HDMI connector DRM reports as connected.
QString detectAlsaAudioDevice() {
    QFile cardsFile(QStringLiteral("/proc/asound/cards"));
    if (!cardsFile.open(QFile::ReadOnly | QFile::Text))
        return {};

    QStringList vc4Cards; // card names, in card-index order
    static const QRegularExpression re(QStringLiteral(R"(^\s*\d+\s+\[(\S+)\s*\]:\s*(.+)$)"));
    for (const QString &line : QString::fromUtf8(cardsFile.readAll()).split('\n')) {
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch() && m.captured(2).contains(QLatin1String("vc4-hdmi")))
            vc4Cards << m.captured(1).trimmed();
    }
    if (vc4Cards.isEmpty())
        return {};

    // Match vc4hdmiN to DRM connector HDMI-A-(N+1) and prefer one reporting
    // "connected"; falls back to the first HDMI card if none are found connected.
    for (int i = 0; i < vc4Cards.size(); ++i) {
        QDir drmDir(QStringLiteral("/sys/class/drm"));
        for (const QString &entry : drmDir.entryList({QString("card*-HDMI-A-%1").arg(i + 1)}, QDir::Dirs)) {
            QFile statusFile(drmDir.filePath(entry) + "/status");
            if (statusFile.open(QFile::ReadOnly | QFile::Text) &&
                statusFile.readAll().trimmed() == "connected") {
                return QStringLiteral("alsa/plughw:CARD=%1,DEV=0").arg(vc4Cards[i]);
            }
        }
    }
    return QStringLiteral("alsa/plughw:CARD=%1,DEV=0").arg(vc4Cards.first());
}
