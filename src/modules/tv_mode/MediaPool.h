#pragma once
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Channel.h"
#include "ShuffleBag.h"

// A shared pool of short clips plus optional per-channel pools — the shape used
// by commercials, bumpers and channel idents alike.
//
// Resolution order for a given channel, most specific first:
//   1. an explicit path on the channel (from tv_channels.json)
//   2. <root>/<channel name>
//   3. loose files directly in <root>
//
// The shared pool is scanned NON-recursively on purpose: recursing would sweep
// every per-channel sub-folder back into the shared pool, so channel-specific
// clips would also air everywhere and quietly defeat the point.
class MediaPool {
public:
    // `explicitDirs` is index-aligned with `channels`; entries may be empty.
    void build(const QString &root,
               const QVector<Channel> &channels,
               const QStringList &explicitDirs,
               const QString &label) {
        m_shared = ShuffleBag();
        m_perChannel.clear();

        const QStringList exts = defaultVideoExtensions();
        if (!root.isEmpty()) {
            const QStringList shared = scanEpisodes(root, exts, false, {}, {});
            m_shared = ShuffleBag(shared);
            if (!shared.isEmpty())
                qInfo("[tv_mode] %lld shared %s", static_cast<long long>(shared.size()),
                      qPrintable(label));
        }

        m_perChannel.reserve(channels.size());
        for (int i = 0; i < channels.size(); ++i) {
            QString dir = explicitDirs.value(i);
            if (dir.isEmpty() && !root.isEmpty()) {
                const QString byName = root + "/" + channels[i].name();
                if (QFileInfo(byName).isDir())
                    dir = byName;
            }
            if (dir.isEmpty()) {
                m_perChannel.append(ShuffleBag());
                continue;
            }
            const QStringList own = scanEpisodes(dir, exts, true, {}, {});
            m_perChannel.append(ShuffleBag(own));
            if (!own.isEmpty())
                qInfo("[tv_mode] CH %d: %lld own %s", channels[i].number(),
                      static_cast<long long>(own.size()), qPrintable(label));
        }
    }

    // The channel's own pool when it has one, else the shared pool, else null.
    ShuffleBag *bagFor(int channelIndex) {
        if (channelIndex >= 0 && channelIndex < m_perChannel.size()
            && m_perChannel[channelIndex].size() > 0)
            return &m_perChannel[channelIndex];
        return m_shared.size() > 0 ? &m_shared : nullptr;
    }

    // One clip for this channel, or an empty string when the pool is empty.
    QString draw(int channelIndex) {
        ShuffleBag *bag = bagFor(channelIndex);
        return bag ? bag->next() : QString();
    }

private:
    ShuffleBag          m_shared;
    QVector<ShuffleBag> m_perChannel;
};
