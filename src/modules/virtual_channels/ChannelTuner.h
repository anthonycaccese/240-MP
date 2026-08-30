#pragma once
#include "ChannelSchedule.h"

#include <QVector>

namespace vchan {

enum class EndReason {
    Eof,
    Stopped,
    Failed
};

EndReason endReasonFromString(const QString &reason);

inline constexpr int kMaxConsecutiveFailures = 8;

inline constexpr qint64 kJoinSlackMs = 10000;

struct Decision {
    bool play = false;

    Status reason = Status::NoSchedule;
    int    slotIndex = -1;
    qint64 offsetMs  = 0;
    QVector<int> window;

    bool needsRegeneration = false;
};

Decision decideTuneIn(const ChannelSchedule &schedule, qint64 nowMs);

Decision decideAfterPlayback(const ChannelSchedule &schedule,
                             qint64 nowMs,
                             EndReason reason,
                             int failedSlotIndex,
                             int consecutiveFailures);

QString offAirMessage(Status reason);
}
