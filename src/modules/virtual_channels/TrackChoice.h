#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace vchan {

// Plex's own three ways with subtitles, plus leaving each programme as its
// source has it -- the Plex part, the server's default, the file's own -- which
// is what every channel did before the setting existed.
enum class SubtitleMode {
    AsSet,
    Off,
    Always,
    WithForeignAudio
};

SubtitleMode subtitleModeFromString(const QString &label);

struct TrackPreference {
    QString      audioLanguage;      // ISO 639-2; empty leaves the source's choice
    SubtitleMode subtitleMode = SubtitleMode::AsSet;
    QString      subtitleLanguage;
};

// Indexes into the stream lists as the source reported them. Subtitle index 0
// is the OFF every list here opens with. The two flags say whether a setting,
// rather than the source, decided.
struct TrackChoice {
    int  audioIndex       = 0;
    int  subtitleIndex    = 0;
    bool audioDecided     = false;
    bool subtitleDecided  = false;
};

// A language as its tags spell it. Servers and files send any of ISO 639-2/T,
// 639-2/B, 639-1 and now and then the plain English name; all are read as one.
struct Language {
    const char *code;    // ISO 639-2/T
    const char *alt;     // the 639-2/B spelling where it differs, or nullptr
    const char *iso1;    // the two-letter code
    const char *label;
};

const QVector<Language> &languages();

bool languageUnknown(const QString &code);

TrackChoice chooseTracks(const QVariantList &audioStreams,
                         const QVariantList &subtitleStreams,
                         int partAudioIndex,
                         int partSubtitleIndex,
                         const TrackPreference &pref);

// The same preference said in mpv's own selection options, for a file mpv
// reads itself and picks the tracks of.
QStringList mpvTrackArgs(const TrackPreference &pref);
}
