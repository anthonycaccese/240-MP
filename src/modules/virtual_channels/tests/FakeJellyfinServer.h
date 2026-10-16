#pragma once
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// The slice of the Jellyfin (and Emby, which has the same shape) backend the
// channels backend finds by introspection. The track choice rides on the
// playback request; whether the answer is the file or a transcode shows only
// in the URL, as it does with the real servers.
class FakeJellyfinServer : public QObject {
    Q_OBJECT
public:
    explicit FakeJellyfinServer(QObject *parent = nullptr) : QObject(parent) {}

    QStringList calls;
    QVariantMap detail;                  // what load_item_detail answers with
    bool transcodes = false;             // answer with a transcode URL
    QList<int> audioIndexes;             // audioStreamIndex of each request
    QList<int> subtitleIndexes;          // subtitleStreamIndex of each

    Q_INVOKABLE QString get_access_token() const { return QStringLiteral("jf-tok"); }

    // What MediaServerSource asks of a backend before it counts as present.
    Q_INVOKABLE void load_libraries() { calls << QStringLiteral("load_libraries"); }
    Q_INVOKABLE void load_items(const QString &, const QString &, const QString &) {}
    Q_INVOKABLE void load_seasons(const QString &) {}

    Q_INVOKABLE void load_item_detail(const QString &itemId) {
        calls << QStringLiteral("load_item_detail:") + itemId;
        later([this] { emit itemLoaded(detail); });
    }
    Q_INVOKABLE void get_playback_url(const QString &, const QString &, int audioStreamIndex,
                                      int subtitleStreamIndex, bool) {
        calls << QStringLiteral("get_playback_url");
        audioIndexes << audioStreamIndex;
        subtitleIndexes << subtitleStreamIndex;
        later([this] {
            emit streamUrlReady(transcodes ? QStringLiteral("http://jf/videos/1/master.m3u8")
                                           : QStringLiteral("http://jf/Videos/1/stream?static=true"));
        });
    }

    template <typename F> void later(F f) { QMetaObject::invokeMethod(this, f, Qt::QueuedConnection); }

    int count(const QString &prefix) const {
        int n = 0;
        for (const QString &c : calls) if (c.startsWith(prefix)) ++n;
        return n;
    }

signals:
    void itemLoaded(const QVariant &detail);
    void streamUrlReady(const QString &url);
    void itemsLoaded(const QVariant &items);
    void librariesLoaded(const QVariant &libraries);
};
