#include "../ChannelTuner.h"
#include "TestHarness.h"

#include <QByteArray>
#include <QString>

using namespace vchan;
using vtest::check;
using vtest::checkEq;
using vtest::section;

namespace {

constexpr qint64 kBase = 1787000000000LL;

ChannelSchedule makeSchedule(int n, qint64 durMs) {
    QString slotJson;
    for (int i = 0; i < n; ++i) {
        if (i) slotJson += QLatin1String(",");
        slotJson += QStringLiteral(
                        R"({"start":%1,"dur":%2,"kind":"programme","src":"local",)"
                        R"("ref":"ep%3.mkv"})")
                        .arg(kBase + i * durMs).arg(durMs).arg(i);
    }
    const QByteArray json =
        QStringLiteral(R"({"channel":1,"generated_at":%1,"slots":[%2]})")
            .arg(kBase - 1000).arg(slotJson).toUtf8();
    return ChannelSchedule::fromJson(json);
}
}

int runChannelTunerTests() {
    const ChannelSchedule sched = makeSchedule(10, 60000);

    section("tune in");
    {
        const Decision d = decideTuneIn(sched, kBase + 90000);
        check(d.play, "playable time plays");
        checkEq(d.slotIndex, 1, "correct slot");
        checkEq(d.offsetMs, 30000, "correct offset");
        check(!d.window.isEmpty(), "window populated");
        checkEq(d.window.first(), 1, "window starts at the current slot");
        check(!d.needsRegeneration, "no regeneration needed while playing");

        const Decision atStart = decideTuneIn(sched, kBase);
        check(atStart.play, "exact start plays");
        checkEq(atStart.offsetMs, 0, "offset zero at the very start");
    }

    section("tune in: joining on a boundary");
    {
        const Decision late = decideTuneIn(sched, kBase + 60000 + 4000);
        check(late.play, "a few seconds late still plays");
        checkEq(late.slotIndex, 1, "still the slot the clock is in");
        checkEq(late.offsetMs, 0, "the opening is not eaten");

        const Decision inside = decideTuneIn(sched, kBase + 60000 + 30000);
        checkEq(inside.offsetMs, 30000, "a real seek is still honoured");

        const Decision sliver = decideTuneIn(sched, kBase + 120000 - 2000);
        check(sliver.play, "the tail of a slot still plays something");
        checkEq(sliver.slotIndex, 2, "it is the next slot, not the fragment");
        checkEq(sliver.offsetMs, 0, "and it starts at its beginning");

        const ChannelSchedule one = makeSchedule(1, 60000);
        const Decision endOfAll = decideTuneIn(one, kBase + 60000 - 2000);
        check(!endOfAll.play, "the tail of the last slot does not play");
        check(endOfAll.needsRegeneration, "it asks for a rebuild instead");

        const Decision earlyEof =
            decideAfterPlayback(sched, kBase + 120000 - 2000, EndReason::Eof, 1, 0);
        check(earlyEof.play, "an early eof still plays something");
        checkEq(earlyEof.slotIndex, 2, "the next slot, not the one that ended");
        checkEq(earlyEof.offsetMs, 0, "from its start");
    }

    section("tune in: off-air cases");
    {
        const Decision past = decideTuneIn(sched, kBase + 600000);
        check(!past.play, "past horizon does not play");
        check(past.reason == Status::PastHorizon, "reason is past horizon");
        check(past.needsRegeneration, "past horizon asks for regeneration");

        const Decision before = decideTuneIn(sched, kBase - 5000);
        check(!before.play, "before start does not play");
        check(!before.needsRegeneration, "before start does not regenerate");

        const Decision unsane = decideTuneIn(sched, 1000);
        check(!unsane.play, "1970 clock does not play");
        check(unsane.reason == Status::ClockUnsane, "reason is clock");
        check(!unsane.needsRegeneration, "bad clock must NOT trigger regeneration");

        const Decision none = decideTuneIn(ChannelSchedule{}, kBase);
        check(!none.play, "no schedule does not play");
        check(none.needsRegeneration, "no schedule asks for regeneration");
    }

    section("after playback");
    {
        const Decision stopped =
            decideAfterPlayback(sched, kBase + 90000, EndReason::Stopped, 1, 0);
        check(!stopped.play, "stopped does not resume");

        const Decision eof =
            decideAfterPlayback(sched, kBase + 150000, EndReason::Eof, 1, 0);
        check(eof.play, "eof continues the channel");
        checkEq(eof.slotIndex, 2, "eof lands on whatever is on now");

        const Decision failed =
            decideAfterPlayback(sched, kBase + 90000, EndReason::Failed, 1, 0);
        check(failed.play, "failure retries the next item");
        check(failed.slotIndex > 1, "failure skips past the failed slot");
        checkEq(failed.slotIndex, 2, "failure advances exactly one slot");

        const Decision giveUp =
            decideAfterPlayback(sched, kBase + 90000, EndReason::Failed, 1,
                                kMaxConsecutiveFailures - 1);
        check(!giveUp.play, "gives up after too many consecutive failures");
        check(giveUp.needsRegeneration, "giving up asks for regeneration");

        const Decision failedAtEnd =
            decideAfterPlayback(sched, kBase + 570000, EndReason::Failed, 9, 0);
        check(!failedAtEnd.play, "failure on the last slot goes off air");
        check(failedAtEnd.needsRegeneration, "and asks for regeneration");
    }

    section("after playback: an item that ends early does not replay itself");
    {
        const Decision early =
            decideAfterPlayback(sched, kBase + 70000, EndReason::Eof, 1, 0);
        check(early.play, "an early end keeps the channel on");
        checkEq(early.slotIndex, 2, "and moves to the next item, not itself");
        checkEq(early.offsetMs, 0, "joining the next item at its start");

        const Decision earlyAtEnd =
            decideAfterPlayback(sched, kBase + 545000, EndReason::Eof, 9, 0);
        check(!earlyAtEnd.play, "an early end on the last slot goes off air");
        check(earlyAtEnd.needsRegeneration, "and asks for regeneration");

        const Decision onTime =
            decideAfterPlayback(sched, kBase + 120000, EndReason::Eof, 1, 0);
        check(onTime.play, "an on-time end continues");
        checkEq(onTime.slotIndex, 2, "on the slot that is actually on now");

        const Decision unknown =
            decideAfterPlayback(sched, kBase + 70000, EndReason::Eof, -1, 0);
        check(unknown.play, "an end with an unknown slot still plays");
        checkEq(unknown.slotIndex, 1, "and resolves by the clock as before");
    }

    section("exhaustive: every decision is actionable");
    {
        const ChannelSchedule schedules[] = {
            sched,
            makeSchedule(1, 1000),
            ChannelSchedule{},
            ChannelSchedule::fromJson("garbage"),
        };

        const EndReason reasons[] = { EndReason::Eof, EndReason::Stopped, EndReason::Failed };

        const qint64 instants[] = {
            0, 1, 1000,
            kBase - 10000, kBase, kBase + 1,
            kBase + 59999, kBase + 60000,
            kBase + 300000,
            kBase + 599999, kBase + 600000,
            kBase + 99999999,
        };

        int decisions = 0;
        bool allActionable = true;
        bool playAlwaysWellFormed = true;
        bool offAirAlwaysExplained = true;

        for (const ChannelSchedule &s : schedules) {
            for (qint64 now : instants) {
                Decision ds[1 + 3 * 3];
                int n = 0;
                ds[n++] = decideTuneIn(s, now);
                for (EndReason r : reasons)
                    for (int fails : { 0, 1, kMaxConsecutiveFailures - 1 })
                        for (int failedIdx : { -1 })
                            ds[n++] = decideAfterPlayback(s, now, r, failedIdx, fails);

                for (int i = 0; i < n; ++i) {
                    const Decision &d = ds[i];
                    ++decisions;

                    if (d.play) {
                        if (d.slotIndex < 0 || d.slotIndex >= s.slotCount())
                            playAlwaysWellFormed = false;
                        if (d.window.isEmpty())
                            playAlwaysWellFormed = false;
                        if (d.offsetMs < 0)
                            playAlwaysWellFormed = false;
                        if (!d.window.isEmpty() && d.window.first() != d.slotIndex)
                            playAlwaysWellFormed = false;
                    } else {
                        if (offAirMessage(d.reason).isEmpty())
                            offAirAlwaysExplained = false;
                    }

                    if (d.play != true && d.play != false)
                        allActionable = false;
                }
            }
        }

        check(decisions > 300, "swept a meaningful number of combinations");
        check(allActionable, "every decision has a terminal action");
        check(playAlwaysWellFormed, "every play decision is a usable loadAndPlay");
        check(offAirAlwaysExplained, "every off-air decision has a message");

        bool neverRepeatsFailure = true;
        for (int failedIdx = 0; failedIdx < sched.slotCount(); ++failedIdx) {
            const qint64 mid = kBase + failedIdx * 60000 + 30000;
            const Decision d = decideAfterPlayback(sched, mid, EndReason::Failed, failedIdx, 0);
            if (d.play && d.slotIndex <= failedIdx) neverRepeatsFailure = false;
        }
        check(neverRepeatsFailure, "a failed slot is never handed back to mpv");
    }

    section("end reason parsing");
    {
        check(endReasonFromString("eof")     == EndReason::Eof,     "eof");
        check(endReasonFromString("failed")  == EndReason::Failed,  "failed");
        check(endReasonFromString("stopped") == EndReason::Stopped, "stopped");
        check(endReasonFromString("EOF")     == EndReason::Eof,     "case insensitive");
        check(endReasonFromString("")        == EndReason::Stopped, "empty is stopped");
        check(endReasonFromString("weird")   == EndReason::Stopped, "unknown is stopped");
    }

    return 0;
}
