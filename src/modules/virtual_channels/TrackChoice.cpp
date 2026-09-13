#include "TrackChoice.h"

#include <QVariantMap>

namespace vchan {

SubtitleMode subtitleModeFromString(const QString &label) {
    const QString l = label.trimmed().toLower();
    if (l == QLatin1String("off"))                return SubtitleMode::Off;
    if (l == QLatin1String("always"))             return SubtitleMode::Always;
    if (l == QLatin1String("with foreign audio")) return SubtitleMode::WithForeignAudio;
    return SubtitleMode::AsSet;
}

const QVector<Language> &languages() {
    static const QVector<Language> all{
        {"eng", nullptr, "en", "ENGLISH"},    {"spa", nullptr, "es", "SPANISH"},
        {"fra", "fre",   "fr", "FRENCH"},     {"deu", "ger",   "de", "GERMAN"},
        {"ita", nullptr, "it", "ITALIAN"},    {"por", nullptr, "pt", "PORTUGUESE"},
        {"nld", "dut",   "nl", "DUTCH"},      {"swe", nullptr, "sv", "SWEDISH"},
        {"nor", nullptr, "no", "NORWEGIAN"},  {"dan", nullptr, "da", "DANISH"},
        {"fin", nullptr, "fi", "FINNISH"},    {"pol", nullptr, "pl", "POLISH"},
        {"ces", "cze",   "cs", "CZECH"},      {"hun", nullptr, "hu", "HUNGARIAN"},
        {"ron", "rum",   "ro", "ROMANIAN"},   {"ell", "gre",   "el", "GREEK"},
        {"tur", nullptr, "tr", "TURKISH"},    {"rus", nullptr, "ru", "RUSSIAN"},
        {"ukr", nullptr, "uk", "UKRAINIAN"},  {"heb", nullptr, "he", "HEBREW"},
        {"ara", nullptr, "ar", "ARABIC"},     {"hin", nullptr, "hi", "HINDI"},
        {"tha", nullptr, "th", "THAI"},       {"vie", nullptr, "vi", "VIETNAMESE"},
        {"ind", nullptr, "id", "INDONESIAN"}, {"jpn", nullptr, "ja", "JAPANESE"},
        {"kor", nullptr, "ko", "KOREAN"},     {"zho", "chi",   "zh", "CHINESE"},
    };
    return all;
}

bool languageUnknown(const QString &code) {
    return code.isEmpty() || code == QLatin1String("und");
}

namespace {

// Every spelling of a language counts as the one language.
QString canonical(const QString &code) {
    for (const Language &l : languages()) {
        if ((l.alt && code == QLatin1String(l.alt)) || code == QLatin1String(l.iso1)
            || code == QString::fromLatin1(l.label).toLower())
            return QLatin1String(l.code);
    }
    return code;
}

QString languageOf(const QVariant &stream) {
    return stream.toMap().value(QStringLiteral("language")).toString().toLower();
}

int firstInLanguage(const QVariantList &streams, const QString &code, int from,
                    bool forcedToo = true) {
    if (code.isEmpty()) return -1;
    const QString want = canonical(code.toLower());
    for (int i = from; i < streams.size(); ++i) {
        if (canonical(languageOf(streams[i])) != want) continue;
        if (!forcedToo && streams[i].toMap().value(QStringLiteral("forced")).toBool()) continue;
        return i;
    }
    return -1;
}

int firstUnknown(const QVariantList &streams, int from) {
    for (int i = from; i < streams.size(); ++i)
        if (languageUnknown(languageOf(streams[i]))) return i;
    return -1;
}

}  // namespace

TrackChoice chooseTracks(const QVariantList &audioStreams,
                         const QVariantList &subtitleStreams,
                         int partAudioIndex,
                         int partSubtitleIndex,
                         const TrackPreference &pref) {
    TrackChoice c;
    c.audioIndex    = partAudioIndex;
    c.subtitleIndex = partSubtitleIndex;

    const int wanted = firstInLanguage(audioStreams, pref.audioLanguage, 0);
    if (wanted >= 0) {
        c.audioIndex   = wanted;
        c.audioDecided = true;
    }

    switch (pref.subtitleMode) {
    case SubtitleMode::AsSet:
        return c;
    case SubtitleMode::Off:
        c.subtitleIndex   = 0;
        c.subtitleDecided = true;
        return c;
    case SubtitleMode::WithForeignAudio: {
        // Judged on the track that will play. Foreign means a language it is
        // known not to be; a track that says nothing about itself might be
        // the viewer's own, so it gets no subtitles either.
        c.subtitleDecided = true;
        const QString home = pref.audioLanguage.isEmpty() ? pref.subtitleLanguage
                                                          : pref.audioLanguage;
        const QString playing = languageOf(audioStreams.value(c.audioIndex));
        if (languageUnknown(playing)
            || canonical(playing) == canonical(home.toLower())) {
            c.subtitleIndex = 0;
            return c;
        }
        break;
    }
    case SubtitleMode::Always:
        c.subtitleDecided = true;
        break;
    }

    // The language asked for, a full track before a forced one, which carries
    // only the lines the film itself would caption; failing that, a track that
    // has not said which language it is, which may well be it. One that is
    // known to be some other language is not shown.
    int pick = firstInLanguage(subtitleStreams, pref.subtitleLanguage, 1, false);
    if (pick < 0) pick = firstInLanguage(subtitleStreams, pref.subtitleLanguage, 1, true);
    if (pick < 0) pick = firstUnknown(subtitleStreams, 1);
    c.subtitleIndex = pick < 0 ? 0 : pick;
    return c;
}

namespace {

// A file's tags may use any spelling, so mpv is given them all.
QString spellings(const QString &code) {
    const QString c = canonical(code.toLower());
    for (const Language &l : languages()) {
        if (c != QLatin1String(l.code)) continue;
        QStringList all{QString::fromLatin1(l.code), QString::fromLatin1(l.iso1)};
        if (l.alt) all.insert(1, QString::fromLatin1(l.alt));
        return all.join(QLatin1Char(','));
    }
    return c;
}

}  // namespace

// The same preference in mpv's terms, which differ from chooseTracks in two
// places mpv has no option for: the fallback is mpv's "default" -- the language
// asked for, else a track the file flags as default, else none, never an
// unlabelled track -- and With Foreign Audio shows subtitles over audio that
// says nothing about its language, where chooseTracks holds them back. The
// forced-track fallback is said outright because the player's own default for
// a file would put a forced track ahead of a full one.
QStringList mpvTrackArgs(const TrackPreference &pref) {
    QStringList args;
    if (!pref.audioLanguage.isEmpty())
        args << QStringLiteral("--alang=%1").arg(spellings(pref.audioLanguage));
    switch (pref.subtitleMode) {
    case SubtitleMode::AsSet:
        break;
    case SubtitleMode::Off:
        args << QStringLiteral("--sid=no");
        break;
    case SubtitleMode::Always:
        args << QStringLiteral("--sid=auto")
             << QStringLiteral("--slang=%1").arg(spellings(pref.subtitleLanguage))
             << QStringLiteral("--subs-with-matching-audio=yes")
             << QStringLiteral("--subs-fallback=default")
             << QStringLiteral("--subs-fallback-forced=yes");
        break;
    case SubtitleMode::WithForeignAudio:
        args << QStringLiteral("--sid=auto")
             << QStringLiteral("--slang=%1").arg(spellings(pref.subtitleLanguage))
             << QStringLiteral("--subs-with-matching-audio=no")
             << QStringLiteral("--subs-fallback=default")
             << QStringLiteral("--subs-fallback-forced=yes");
        break;
    }
    return args;
}

}  // namespace vchan
