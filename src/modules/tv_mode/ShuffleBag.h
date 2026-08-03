#pragma once
#include <QRandomGenerator>
#include <QStringList>

// Draws every item once, in random order, before any item repeats — then
// reshuffles. This is what makes a channel feel like it has a real rotation
// instead of a random-number generator that can play the same episode twice in
// a row.
//
// Ported from NostalgiaBox's `playlist.ShuffleBag` (MIT — see THIRD-PARTY.md).
class ShuffleBag {
public:
    ShuffleBag() = default;
    explicit ShuffleBag(const QStringList &items) : m_items(items) { refill(); }

    bool isEmpty() const { return m_items.isEmpty(); }
    int  size()    const { return m_items.size(); }

    // The next item in the rotation. Returns a null string only when the bag was
    // built from an empty list.
    QString next() {
        if (m_items.isEmpty())
            return QString();
        if (m_bag.isEmpty())
            refill();
        m_last = m_bag.takeLast();
        return m_last;
    }

    // The next `count` draws, without consuming them. Only the current bag is
    // knowable — past its end the order genuinely isn't decided yet, so this
    // returns fewer than asked rather than inventing a future.
    QStringList peek(int count) const {
        QStringList out;
        for (int i = m_bag.size() - 1; i >= 0 && out.size() < count; --i)
            out.append(m_bag[i]);
        return out;
    }

private:
    void refill() {
        m_bag = m_items;
        // Fisher-Yates.
        for (int i = m_bag.size() - 1; i > 0; --i) {
            const int j = QRandomGenerator::global()->bounded(i + 1);
            m_bag.swapItemsAt(i, j);
        }
        // Don't let a reshuffle replay the episode that just finished: next()
        // draws from the end, so if the tail matches the last draw, move it.
        if (m_bag.size() > 1 && m_bag.last() == m_last)
            m_bag.swapItemsAt(m_bag.size() - 1, 0);
    }

    QStringList m_items;   // every episode on the channel
    QStringList m_bag;     // what's left to draw this rotation
    QString     m_last;    // most recent draw, for the reshuffle guard
};
