#include "../TrackChoice.h"
#include "TestHarness.h"

#include <QVariantMap>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::section;

namespace {

QVariantMap track(const char *id, const char *language) {
    return QVariantMap{{"id", QString::fromLatin1(id)}, {"language", QString::fromLatin1(language)}};
}

// A Plex subtitle list, OFF first, as the backend sees it.
QVariantList subs(std::initializer_list<QVariantMap> real) {
    QVariantList l{track("0", "")};
    for (const QVariantMap &t : real) l << t;
    return l;
}

TrackPreference prefs(const char *audio, SubtitleMode mode, const char *sub = "eng") {
    TrackPreference p;
    p.audioLanguage    = QString::fromLatin1(audio);
    p.subtitleMode     = mode;
    p.subtitleLanguage = QString::fromLatin1(sub);
    return p;
}

void testNothingSetLeavesThePart() {
    section("Track choice: with nothing set, the part's tracks stand");
    const QVariantList audio{track("7", "por"), track("8", "eng")};
    const TrackChoice c = chooseTracks(audio, subs({track("20", "eng")}), 0, 1,
                                       prefs("", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 0, "audio as the part has it");
    checkEq(c.subtitleIndex, 1, "subtitles as the part has them");
    check(!c.audioDecided && !c.subtitleDecided, "nothing was decided here");
}

void testAudioLanguage() {
    section("Track choice: the audio language asked for wins when the programme has it");
    const QVariantList audio{track("7", "por"), track("8", "eng"), track("9", "eng")};
    TrackChoice c = chooseTracks(audio, subs({}), 0, 0, prefs("eng", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 1, "the first track in that language");
    check(c.audioDecided, "and it was the setting that decided");

    c = chooseTracks(audio, subs({}), 0, 0, prefs("jpn", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 0, "a language the programme lacks leaves the part's track");
    check(!c.audioDecided, "which nobody decided");

    c = chooseTracks({track("7", "ger")}, subs({}), 0, 0, prefs("deu", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 0, "both spellings of a language are the one language");
    check(c.audioDecided, "so the setting decided");

    c = chooseTracks({track("7", "por"), track("8", "en")}, subs({}), 0, 0, prefs("eng", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 1, "a two-letter tag is read");
    c = chooseTracks({track("7", "por"), track("8", "English")}, subs({}), 0, 0, prefs("eng", SubtitleMode::AsSet));
    checkEq(c.audioIndex, 1, "and so is the plain name a source falls back to");
}

void testSubtitlesOff() {
    section("Track choice: Off is off whatever the part shows");
    const TrackChoice c = chooseTracks({track("7", "eng")}, subs({track("20", "eng")}), 0, 1,
                                       prefs("", SubtitleMode::Off));
    checkEq(c.subtitleIndex, 0, "off");
    check(c.subtitleDecided, "decided");
}

void testSubtitlesAlways() {
    section("Track choice: Always shows the language asked for, else an unlabelled track, else nothing");
    const QVariantList audio{track("7", "eng")};
    TrackChoice c = chooseTracks(audio, subs({track("20", "por"), track("21", "eng")}), 0, 0,
                                 prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 2, "the track in the language asked for");

    c = chooseTracks(audio, subs({track("20", "por"), track("21", "")}), 0, 0,
                     prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 2, "an unlabelled track may well be it");

    c = chooseTracks(audio, subs({track("20", "por"), track("21", "und")}), 0, 0,
                     prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 2, "undetermined counts as unlabelled");

    c = chooseTracks(audio, subs({track("20", "por"), track("21", "spa")}), 0, 1,
                     prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 0, "tracks known to be other languages are not shown");
    check(c.subtitleDecided, "and that was decided, not left to the part");

    c = chooseTracks(audio, subs({}), 0, 0, prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 0, "no tracks, nothing to show");

    QVariantMap forced = track("20", "eng");
    forced["forced"] = true;
    c = chooseTracks(audio, subs({forced, track("21", "eng")}), 0, 0, prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 2, "a full track is preferred to a forced one in the same language");
    c = chooseTracks(audio, subs({forced}), 0, 0, prefs("", SubtitleMode::Always));
    checkEq(c.subtitleIndex, 1, "a forced track is still that language when it is all there is");
}

void testSubtitlesWithForeignAudio() {
    section("Track choice: With Foreign Audio judges the track that will play");
    const QVariantList english{track("7", "eng")};
    const QVariantList portuguese{track("7", "por")};
    const QVariantList unlabelled{track("7", "")};
    const QVariantList both{track("7", "por"), track("8", "eng")};
    const QVariantList sub{track("20", "eng")};
    const auto foreign = prefs("eng", SubtitleMode::WithForeignAudio);

    TrackChoice c = chooseTracks(english, subs({sub[0].toMap()}), 0, 0, foreign);
    checkEq(c.subtitleIndex, 0, "audio in the viewer's language: no subtitles");
    check(c.subtitleDecided, "decided");

    c = chooseTracks(portuguese, subs({sub[0].toMap()}), 0, 0, foreign);
    checkEq(c.subtitleIndex, 1, "foreign audio: subtitles");

    c = chooseTracks(unlabelled, subs({sub[0].toMap()}), 0, 0, foreign);
    checkEq(c.subtitleIndex, 0, "audio that says nothing about itself might be the viewer's: none");

    c = chooseTracks(both, subs({sub[0].toMap()}), 0, 0, foreign);
    checkEq(c.audioIndex, 1, "the English track is chosen");
    checkEq(c.subtitleIndex, 0, "so the audio is not foreign");

    c = chooseTracks(both, subs({sub[0].toMap()}), 0, 0, prefs("jpn", SubtitleMode::WithForeignAudio, "jpn"));
    checkEq(c.audioIndex, 0, "no Japanese audio: the part's track plays");
    checkEq(c.subtitleIndex, 0, "Portuguese is foreign, but there are no Japanese subtitles");

    // Audio left to Plex: foreign is measured against the subtitle language.
    c = chooseTracks(both, subs({sub[0].toMap()}), 0, 0, prefs("", SubtitleMode::WithForeignAudio));
    checkEq(c.audioIndex, 0, "the part's Portuguese plays");
    checkEq(c.subtitleIndex, 1, "and English subtitles come with it");
    c = chooseTracks(both, subs({sub[0].toMap()}), 1, 0, prefs("", SubtitleMode::WithForeignAudio));
    checkEq(c.subtitleIndex, 0, "a part already on English needs none");

    c = chooseTracks({}, subs({sub[0].toMap()}), 0, 0, foreign);
    checkEq(c.subtitleIndex, 0, "no audio tracks at all: nothing to judge, none shown");
}

void testModeLabels() {
    section("Track choice: the setting's labels, and anything else, read as modes");
    check(subtitleModeFromString("Off") == SubtitleMode::Off, "Off");
    check(subtitleModeFromString("always") == SubtitleMode::Always, "case does not matter");
    check(subtitleModeFromString("With Foreign Audio") == SubtitleMode::WithForeignAudio, "With Foreign Audio");
    check(subtitleModeFromString("As Set In Plex") == SubtitleMode::AsSet, "As Set In Plex");
    check(subtitleModeFromString("") == SubtitleMode::AsSet, "unset is as set");
    check(subtitleModeFromString("garbage") == SubtitleMode::AsSet, "and so is anything unknown");
}

void testMpvArgs() {
    section("Track choice: the same preference in mpv's own options, for files it reads itself");
    QStringList a = mpvTrackArgs(prefs("", SubtitleMode::AsSet));
    check(a.isEmpty(), "nothing set, nothing said");

    a = mpvTrackArgs(prefs("deu", SubtitleMode::Off));
    check(a.contains("--alang=deu,ger,de"), "audio language, in every spelling");
    check(a.contains("--sid=no"), "off is no subtitle track");
    check(!a.join(" ").contains("--slang"), "and no language to look for");

    a = mpvTrackArgs(prefs("", SubtitleMode::Always, "por"));
    check(a.contains("--slang=por,pt"), "the subtitle language");
    check(a.contains("--subs-with-matching-audio=yes"), "shown whatever the audio is");
    check(a.contains("--subs-fallback=default"), "else the file's default track, else none");
    check(a.contains("--subs-fallback-forced=yes"), "and a forced track is not put ahead of a full one");

    a = mpvTrackArgs(prefs("eng", SubtitleMode::WithForeignAudio));
    check(a.contains("--subs-with-matching-audio=no"), "not shown when the audio is already that language");
    check(a.contains("--slang=eng,en") && a.contains("--alang=eng,en"), "both languages named");
}

}  // namespace

int runTrackChoiceTests() {
    testMpvArgs();
    testNothingSetLeavesThePart();
    testAudioLanguage();
    testSubtitlesOff();
    testSubtitlesAlways();
    testSubtitlesWithForeignAudio();
    testModeLabels();
    return 0;
}
