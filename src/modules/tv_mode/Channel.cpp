#include "Channel.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

QStringList defaultVideoExtensions() {
    // Matches the Local Files module's list so a folder that plays there also
    // scans as a channel here.
    return { "mp4", "mkv", "avi", "mov", "m4v", "webm", "wmv",
             "flv", "f4v", "mpg", "mpeg", "vob", "ts" };
}

int detectSeason(const QString &text) {
    // Same three shapes NostalgiaBox looks for, in the same order.
    static const QRegularExpression sxe(
        R"(s(\d{1,2})[ ._-]?e\d{1,3})", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression seasonWord(
        R"(\bseason[ ._-]*(\d{1,2})\b)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nxn(R"(\b(\d{1,2})x\d{1,3}\b)");

    for (const QRegularExpression &re : { sxe, seasonWord, nxn }) {
        const QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch())
            return m.captured(1).toInt();
    }
    return -1;
}

static bool isExcluded(const QString &relPath,
                       const QString &fileName,
                       const QVector<QRegularExpression> &patterns,
                       const QSet<int> &excludeSeasons) {
    for (const QRegularExpression &re : patterns) {
        if (re.match(relPath).hasMatch() || re.match(fileName).hasMatch())
            return true;
    }
    if (!excludeSeasons.isEmpty()) {
        const int season = detectSeason(relPath);
        if (season >= 0 && excludeSeasons.contains(season))
            return true;
    }
    return false;
}

QStringList scanEpisodes(const QString &root,
                         const QStringList &extensions,
                         bool recursive,
                         const QStringList &exclude,
                         const QSet<int> &excludeSeasons) {
    QStringList episodes;
    const QDir rootDir(root);
    if (root.isEmpty() || !rootDir.exists()) {
        qWarning("[tv_mode] channel folder does not exist: %s", qPrintable(root));
        return episodes;
    }

    QSet<QString> exts;
    for (const QString &e : extensions)
        exts.insert(e.toLower());

    QVector<QRegularExpression> patterns;
    patterns.reserve(exclude.size());
    for (const QString &pat : exclude) {
        patterns.append(QRegularExpression(
            QRegularExpression::wildcardToRegularExpression(pat),
            QRegularExpression::CaseInsensitiveOption));
    }

    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                    recursive ? QDirIterator::Subdirectories
                              : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo info(filePath);
        if (info.fileName().startsWith('.'))
            continue;
        if (!exts.contains(info.suffix().toLower()))
            continue;
        const QString rel = rootDir.relativeFilePath(filePath);
        if (isExcluded(rel, info.fileName(), patterns, excludeSeasons))
            continue;
        episodes.append(filePath);
    }

    // Stable, case-insensitive order. Episodes are shuffled for playback, but a
    // deterministic base order keeps rescans and the guide predictable.
    std::sort(episodes.begin(), episodes.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return episodes;
}

// ---------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------

Channel::Channel(const ChannelConfig &config,
                 const QStringList &episodes,
                 const QString &tuneInMode,
                 double startOffsetMin,
                 double startOffsetMax,
                 const QString &order)
    : m_config(config)
    , m_episodes(episodes)
    , m_tuneInMode(tuneInMode)
    , m_startOffsetMin(qMax(0.0, startOffsetMin))
    , m_startOffsetMax(qMax(qMax(0.0, startOffsetMin), startOffsetMax))
    , m_order(order)
    , m_bag(episodes)
{}

QStringList Channel::upcoming(int count) const {
    QStringList out;
    if (m_episodes.isEmpty() || count <= 0)
        return out;
    if (m_order == QLatin1String("sequential")) {
        for (int i = 0; i < count; ++i)
            out.append(m_episodes[(m_seqIndex + i) % m_episodes.size()]);
    } else {
        // A shuffle bag's future is genuinely undecided, so a guide can only show
        // what it will draw next — report the remaining bag in draw order and top
        // up from the full list once it runs out.
        out = m_bag.peek(count);
        for (int i = 0; out.size() < count && i < m_episodes.size(); ++i)
            out.append(m_episodes[i]);
    }
    return out;
}

PlayRequest Channel::playSpecific(const QString &path) {
    PlayRequest req;
    if (!m_episodes.contains(path))
        return req;
    req.path  = path;
    req.start = 0.0;   // a deliberate pick starts at the beginning
    const int idx = m_episodes.indexOf(path);
    if (idx >= 0) {
        m_seqIndex = (idx + 1) % m_episodes.size();
        m_lastPlayedIndex = idx;
    }
    return req;
}

PlayRequest Channel::nextInOrder() {
    PlayRequest req;
    if (m_order == QLatin1String("sequential")) {
        req.path = m_episodes[m_seqIndex % m_episodes.size()];
        m_seqIndex = (m_seqIndex + 1) % m_episodes.size();
    } else {
        req.path = m_bag.next();
    }
    m_lastPlayedIndex = m_episodes.indexOf(req.path);
    if (m_startOffsetMax > m_startOffsetMin) {
        req.start = m_startOffsetMin
                  + QRandomGenerator::global()->generateDouble()
                      * (m_startOffsetMax - m_startOffsetMin);
    } else {
        req.start = m_startOffsetMin;
    }
    return req;
}

PlayRequest Channel::tuneIn() {
    if (isEmpty())
        return PlayRequest();
    if (m_tuneInMode == QLatin1String("resume") && !m_resumePath.isEmpty()) {
        PlayRequest req;
        req.path  = m_resumePath;
        req.start = m_resumePosition;
        return req;
    }
    return nextInOrder();
}

PlayRequest Channel::advance() {
    if (isEmpty())
        return PlayRequest();
    // A natural end always rolls the rotation, even in resume mode — resume is
    // about returning to a channel, not about replaying what just finished.
    return nextInOrder();
}

void Channel::buildSchedule(const QVector<double> &durations, double epoch) {
    m_schedule.clear();
    m_scheduleDurations.clear();
    m_scheduleCycle = 0.0;
    m_scheduleEpoch = epoch;
    if (m_episodes.isEmpty() || durations.size() != m_episodes.size())
        return;

    // The running order follows the channel's configured ordering, so a
    // sequential channel broadcasts its series in order.
    QVector<int> order;
    order.reserve(m_episodes.size());
    for (int i = 0; i < m_episodes.size(); ++i)
        order.append(i);
    if (m_order != QLatin1String("sequential")) {
        for (int i = order.size() - 1; i > 0; --i)
            order.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
    }

    for (int idx : order) {
        m_schedule.append(m_episodes[idx]);
        const double d = qMax(1.0, durations[idx]);
        m_scheduleDurations.append(d);
        m_scheduleCycle += d;
    }
}

void Channel::setOrder(const QString &order) {
    if (order == m_order || m_episodes.isEmpty())
        return;
    // Coming back to sequential after shuffling, continue the series from the
    // episode last aired rather than jumping back to the first one.
    if (order == QLatin1String("sequential") && m_lastPlayedIndex >= 0)
        m_seqIndex = (m_lastPlayedIndex + 1) % m_episodes.size();
    m_order = order;
}

PlayRequest Channel::scheduledAt(double when) {
    PlayRequest req;
    if (m_schedule.isEmpty() || m_scheduleCycle <= 0.0)
        return req;
    double elapsed = std::fmod(when - m_scheduleEpoch, m_scheduleCycle);
    if (elapsed < 0.0)
        elapsed += m_scheduleCycle;
    for (int i = 0; i < m_schedule.size(); ++i) {
        if (elapsed < m_scheduleDurations[i]) {
            req.path  = m_schedule[i];
            req.start = elapsed;
            m_lastPlayedIndex = m_episodes.indexOf(req.path);
            return req;
        }
        elapsed -= m_scheduleDurations[i];
    }
    // Floating-point safety net.
    req.path = m_schedule.last();
    return req;
}

void Channel::remember(const QString &path, double position) {
    m_resumePath     = path;
    m_resumePosition = qMax(0.0, position);
}

// ---------------------------------------------------------------------------
// ChannelLineup
// ---------------------------------------------------------------------------

ChannelLineup::ChannelLineup(const QVector<Channel> &channels)
    : m_channels(channels)
{
    std::sort(m_channels.begin(), m_channels.end(),
              [](const Channel &a, const Channel &b) {
                  return a.number() < b.number();
              });
}

Channel *ChannelLineup::current() {
    if (m_channels.isEmpty())
        return nullptr;
    return &m_channels[m_index];
}

const Channel *ChannelLineup::current() const {
    if (m_channels.isEmpty())
        return nullptr;
    return &m_channels[m_index];
}

Channel *ChannelLineup::up() {
    if (m_channels.isEmpty())
        return nullptr;
    m_index = (m_index + 1) % m_channels.size();
    return current();
}

Channel *ChannelLineup::down() {
    if (m_channels.isEmpty())
        return nullptr;
    m_index = (m_index - 1 + m_channels.size()) % m_channels.size();
    return current();
}

bool ChannelLineup::hasNumber(int number) const {
    for (const Channel &c : m_channels)
        if (c.number() == number)
            return true;
    return false;
}

Channel *ChannelLineup::selectNumber(int number) {
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].number() == number) {
            m_index = i;
            return current();
        }
    }
    return nullptr;
}

Channel *ChannelLineup::selectIndex(int index) {
    if (m_channels.isEmpty())
        return nullptr;
    m_index = ((index % m_channels.size()) + m_channels.size()) % m_channels.size();
    return current();
}
