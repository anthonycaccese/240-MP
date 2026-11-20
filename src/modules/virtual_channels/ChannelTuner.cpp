#include "ChannelTuner.h"

namespace vchan {

EndReason endReasonFromString(const QString &reason) {
    const QString r = reason.trimmed().toLower();
    if (r == QLatin1String("eof"))    return EndReason::Eof;
    if (r == QLatin1String("failed")) return EndReason::Failed;
    return EndReason::Stopped;
}

namespace {

Decision offAir(Status reason, bool regenerate = false) {
    Decision d;
    d.play              = false;
    d.reason            = reason;
    d.needsRegeneration = regenerate;
    return d;
}

Decision playFrom(const ChannelSchedule &s, int index, qint64 offset) {
    Decision d;
    d.play      = true;
    d.reason    = Status::Ok;
    d.slotIndex = index;
    d.offsetMs  = offset;
    d.window    = s.runWindow(index);
    return d;
}

qint64 joinOffset(const ChannelSchedule &s, int index, qint64 nowMs) {
    const qint64 off = s.offsetInto(index, nowMs);
    return off < kJoinSlackMs ? 0 : off;
}

Decision advancePast(const ChannelSchedule &s, int index, qint64 nowMs) {
    const int next = index + 1;
    if (next >= s.slotCount()) return offAir(Status::PastHorizon, /*regenerate*/ true);
    return playFrom(s, next, joinOffset(s, next, nowMs));
}
}

Decision decideTuneIn(const ChannelSchedule &schedule, qint64 nowMs) {
    const Status st = schedule.statusAt(nowMs);

    switch (st) {
    case Status::Ok: {
        const int idx = schedule.indexAt(nowMs);
        if (idx < 0) return offAir(Status::OffAirGap);

        const Slot &here = schedule.slotList()[idx];
        const qint64 remaining = here.end() - nowMs;
        if (remaining < kJoinSlackMs && remaining < here.dur) {
            const int next = idx + 1;
            if (next >= schedule.slotCount())
                return offAir(Status::PastHorizon, /*regenerate*/ true);
            if (schedule.slotList()[next].start <= nowMs + kJoinSlackMs)
                return playFrom(schedule, next, 0);
        }

        return playFrom(schedule, idx, joinOffset(schedule, idx, nowMs));
    }

    case Status::PastHorizon:
        return offAir(st, /*regenerate*/ true);

    case Status::NoSchedule:
    case Status::Malformed:
        return offAir(st, /*regenerate*/ true);

    case Status::ClockUnsane:
    case Status::BeforeStart:
    case Status::OffAirGap:
        return offAir(st);
    }

    return offAir(Status::NoSchedule);
}

Decision decideAfterPlayback(const ChannelSchedule &schedule,
                             qint64 nowMs,
                             EndReason reason,
                             int failedSlotIndex,
                             int consecutiveFailures) {
    switch (reason) {
    case EndReason::Stopped:
        return offAir(Status::Ok);

    case EndReason::Eof: {
        const Decision d = decideTuneIn(schedule, nowMs);
        if (d.play && d.slotIndex == failedSlotIndex)
            return advancePast(schedule, failedSlotIndex, nowMs);
        return d;
    }

    case EndReason::Failed:
        break;
    }

    if (consecutiveFailures + 1 >= kMaxConsecutiveFailures)
        return offAir(Status::NoSchedule, /*regenerate*/ true);

    int next = schedule.indexAt(nowMs);
    if (next < 0) {
        return decideTuneIn(schedule, nowMs);
    }
    if (failedSlotIndex >= 0 && next <= failedSlotIndex)
        return advancePast(schedule, failedSlotIndex, nowMs);

    if (next >= schedule.slotCount())
        return offAir(Status::PastHorizon, /*regenerate*/ true);

    return playFrom(schedule, next, joinOffset(schedule, next, nowMs));
}

QString offAirMessage(Status reason) {
    switch (reason) {
    case Status::Ok:          return QStringLiteral("Off air");
    case Status::NoSchedule:  return QStringLiteral("No schedule for this channel");
    case Status::Malformed:   return QStringLiteral("Schedule unreadable");
    case Status::ClockUnsane: return QStringLiteral("Waiting for clock");
    case Status::BeforeStart: return QStringLiteral("Channel has not started");
    case Status::PastHorizon: return QStringLiteral("Schedule ended — rebuilding");
    case Status::OffAirGap:   return QStringLiteral("Off air");
    }
    return QStringLiteral("Off air");
}
}
