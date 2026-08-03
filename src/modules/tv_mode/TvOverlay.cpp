#include "TvOverlay.h"

#include <QStringList>
#include <cmath>

namespace TvOverlay {

namespace {

constexpr const char *kBlack = "&H00000000";

struct SafeArea {
    int x0, x1, y0, y1;
};

// Symmetric variant for centred layouts — see Style::symmetricX().
SafeArea safeAreaCentred(const Style &st) {
    const int insetX = int(std::lround(CanvasW * st.symmetricX()));
    const int insetY = int(std::lround(CanvasH * st.symmetricY()));
    return { insetX, CanvasW - insetX, insetY, CanvasH - insetY };
}

SafeArea safeArea(const Style &st) {
    return { int(std::lround(CanvasW * st.leftFrac())),
             CanvasW - int(std::lround(CanvasW * st.rightFrac())),
             int(std::lround(CanvasH * st.topFrac())),
             CanvasH - int(std::lround(CanvasH * st.bottomFrac())) };
}

// Common override tags: retro font, phosphor fill, and a soft bloom. The blur is
// not only cosmetic — softened edges markedly reduce interlace flicker on a 480i
// display, where hard thin lines shimmer between fields.
QString styleTags(const Style &style, int size, int alpha = 0) {
    const QString color = hexToAss(style.color, alpha);
    const QString alphaHex =
        QString::number(alpha, 16).rightJustified(2, '0').toUpper();
    QString tags = QStringLiteral("\\fn%1\\b1\\fs%2\\c%3\\1a&H%4&")
                       .arg(style.font)
                       .arg(size)
                       .arg(color, alphaHex);

    // Outline colour is the legibility lever: black separates the text from
    // whatever is behind it, green produces NostalgiaBox's bloom.
    const QString outline = style.bloom ? color : QString::fromLatin1(kBlack);
    tags += QStringLiteral("\\bord%1\\3c%2\\4c%3\\shad0")
                .arg(QString::number(style.border, 'f', 1), outline,
                     QString::fromLatin1(kBlack));
    if (style.blur > 0.0)
        tags += QStringLiteral("\\blur%1").arg(QString::number(style.blur, 'f', 1));
    return tags;
}

}  // namespace

QString hexToAss(const QString &hexColor, int alpha) {
    QString h = hexColor;
    if (h.startsWith('#'))
        h.remove(0, 1);
    if (h.size() != 6)
        h = QStringLiteral("4DFF5A");
    const QString r = h.mid(0, 2), g = h.mid(2, 2), b = h.mid(4, 2);
    return QStringLiteral("&H%1%2%3%4")
        .arg(QString::number(alpha, 16).rightJustified(2, '0'), b, g, r)
        .toUpper();
}

QString escapeText(const QString &text) {
    QString out = text;
    out.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    // Braces would terminate the override block, so swap them for parentheses
    // rather than trying to escape them.
    out.replace(QLatin1Char('{'), QLatin1Char('('));
    out.replace(QLatin1Char('}'), QLatin1Char(')'));
    return out;
}

QString channelBanner(int number, const QString &name, const Style &style) {
    const SafeArea sa = safeArea(style);
    const QString num = QString::number(number).rightJustified(2, '0');

    // \an9 = anchored top-right, so the block grows leftward from the safe edge.
    const QString numberLine =
        QStringLiteral("{\\an9\\pos(%1,%2)%3}CH %4")
            .arg(sa.x1).arg(sa.y0).arg(styleTags(style, style.numberSize), num);

    // Name sits a line below, offset proportionally to the number's size rather
    // than a fixed constant, so changing numberSize keeps the pair together.
    const int nameY = sa.y0 + int(std::lround(style.numberSize * 1.18));
    const QString nameLine =
        QStringLiteral("{\\an9\\pos(%1,%2)%3}%4")
            .arg(sa.x1).arg(nameY)
            .arg(styleTags(style, style.nameSize), escapeText(name));

    return QStringList{ numberLine, nameLine }.join(QLatin1Char('\n'));
}

QString calibrationPattern(const Style &style) {
    const QString color = hexToAss(style.color);
    QStringList parts;

    // A drawing with a transparent fill (\1a&HFF&) and a visible border renders
    // as an outline, which is what we want — a filled box would hide the video.
    const int percents[] = { 5, 10, 15, 20 };
    for (int pct : percents) {
        const double f = pct / 100.0;
        const int x0 = int(std::lround(CanvasW * f));
        const int y0 = int(std::lround(CanvasH * f));
        const int w  = CanvasW - 2 * x0;
        const int h  = CanvasH - 2 * y0;

        parts << QStringLiteral(
                     "{\\an7\\pos(%1,%2)\\p1\\1a&HFF&\\bord2\\3c%3\\4c%4\\shad0}"
                     "m 0 0 l %5 0 l %5 %6 l 0 %6{\\p0}")
                     .arg(x0).arg(y0).arg(color, QString::fromLatin1(kBlack))
                     .arg(w).arg(h);

        // Label every edge, not just a corner: overscan is usually uneven, so
        // each side has to be readable on its own.
        const QString tag = styleTags(style, 18);
        const int cxBox = x0 + w / 2, cyBox = y0 + h / 2;
        parts << QStringLiteral("{\\an4\\pos(%1,%2)%3}%4").arg(x0 + 3).arg(cyBox).arg(tag).arg(pct);
        parts << QStringLiteral("{\\an6\\pos(%1,%2)%3}%4").arg(x0 + w - 3).arg(cyBox).arg(tag).arg(pct);
        parts << QStringLiteral("{\\an8\\pos(%1,%2)%3}%4").arg(cxBox).arg(y0 + 2).arg(tag).arg(pct);
        parts << QStringLiteral("{\\an2\\pos(%1,%2)%3}%4").arg(cxBox).arg(y0 + h - 2).arg(tag).arg(pct);
    }

    // Centre cross, to check the picture is not off-centre on the tube.
    const int cx = CanvasW / 2, cy = CanvasH / 2, arm = 20;
    parts << QStringLiteral(
                 "{\\an7\\pos(%1,%2)\\p1\\1a&HFF&\\bord2\\3c%3\\shad0}"
                 "m 0 %4 l %5 %4{\\p0}")
                 .arg(cx - arm).arg(cy).arg(color).arg(0).arg(arm * 2);
    parts << QStringLiteral(
                 "{\\an7\\pos(%1,%2)\\p1\\1a&HFF&\\bord2\\3c%3\\shad0}"
                 "m %4 0 l %4 %5{\\p0}")
                 .arg(cx).arg(cy - arm).arg(color).arg(0).arg(arm * 2);

    // Report the value currently configured, so the comparison is concrete.
    parts << QStringLiteral("{\\an5\\pos(%1,%2)%3}L%4 R%5 T%6 B%7")
                 .arg(cx).arg(cy + 40)
                 .arg(styleTags(style, 22))
                 .arg(int(std::lround(style.leftFrac()   * 100)))
                 .arg(int(std::lround(style.rightFrac()  * 100)))
                 .arg(int(std::lround(style.topFrac()    * 100)))
                 .arg(int(std::lround(style.bottomFrac() * 100)));

    return parts.join(QLatin1Char('\n'));
}

QString message(const QString &text, const Style &style, MessagePos pos) {
    const SafeArea sa = safeArea(style);
    if (pos == MessagePos::CentreLeft) {
        // \an4 = middle-left, anchored to the safe edge.
        return QStringLiteral("{\\an4\\pos(%1,%2)%3}%4")
            .arg(sa.x0).arg(CanvasH / 2)
            .arg(styleTags(style, style.messageSize), escapeText(text));
    }
    return QStringLiteral("{\\an8\\pos(%1,%2)%3}%4")
        .arg(CanvasW / 2).arg(sa.y0)
        .arg(styleTags(style, style.messageSize), escapeText(text));
}

// ---------------------------------------------------------------------------
// TV guide
// ---------------------------------------------------------------------------

namespace {

// VCR OSD Mono is monospace, so character count is a reliable width budget.
constexpr double kCharWidthRatio = 0.60;

QString elide(const QString &text, int pixelWidth, int fontSize) {
    const int maxChars = int(pixelWidth / (fontSize * kCharWidthRatio));
    if (maxChars <= 1)
        return QString();
    if (text.size() <= maxChars)
        return text;
    return text.left(qMax(1, maxChars - 1)) + QStringLiteral("…");
}

// Flat filled rectangle, used for the backing panel and the selection block.
QString fillRect(int x, int y, int w, int h, const QString &assColor, int alpha) {
    return QStringLiteral(
               "{\\an7\\pos(%1,%2)\\p1\\c%3\\1a&H%4&\\bord0\\shad0}"
               "m 0 0 l %5 0 l %5 %6 l 0 %6{\\p0}")
        .arg(x).arg(y).arg(assColor)
        .arg(QString::number(alpha, 16).rightJustified(2, '0').toUpper())
        .arg(w).arg(h);
}

// Plain text with no outline — the guide sits on its own solid panel, so the
// legibility outline the banner needs would just look heavy here.
QString flatText(int x, int y, const QString &align, int size,
                 const QString &assColor, const QString &font, const QString &text) {
    return QStringLiteral("{\\an%1\\pos(%2,%3)\\fn%4\\b1\\fs%5\\c%6\\1a&H00&\\bord0\\shad0}%7")
        .arg(align).arg(x).arg(y).arg(font).arg(size).arg(assColor, text);
}

}  // namespace

QString guideGrid(const QVector<GuideRow> &rows,
                  const QStringList &columnLabels,
                  int firstRow, int visibleRows,
                  int selRow, int selCol,
                  const QString &detailTitle,
                  const QString &detailSub,
                  const QString &optionText,
                  bool optionSelected,
                  const GuideTheme &theme,
                  const Style &style) {
    const SafeArea sa = safeAreaCentred(style);
    const QString primary   = hexToAss(theme.primary);
    const QString secondary = hexToAss(theme.secondary);
    const QString tertiary  = hexToAss(theme.tertiary);
    const QString surface   = hexToAss(theme.surface);
    const QString accent    = hexToAss(theme.accent);

    const int nCols   = qMax(1, columnLabels.size());
    const int chanW   = 150;
    const int gridX   = sa.x0 + chanW;
    const int colW    = (sa.x1 - gridX) / nCols;
    const int rowH    = 38;
    const int headerY = sa.y0 + 78;
    const int gridY   = sa.y0 + 98;

    QStringList out;

    // Backing panel. Mostly opaque so titles stay readable over moving video,
    // but not fully — a real box always let a little of the picture through.
    out << fillRect(sa.x0 - 8, sa.y0 - 6,
                    (sa.x1 - sa.x0) + 16,
                    (sa.y1 - sa.y0) + 12, surface, 0x1A);

    // Detail panel: the highlighted programme, in full. This is what makes the
    // truncation in the grid cells acceptable.
    out << flatText(sa.x0, sa.y0, "7", 26, primary, theme.font,
                    escapeText(elide(detailTitle, sa.x1 - sa.x0, 26)));
    out << flatText(sa.x0, sa.y0 + 34, "7", 17, secondary, theme.font,
                    escapeText(elide(detailSub, sa.x1 - sa.x0, 17)));

    // Column headers.
    for (int c = 0; c < nCols; ++c) {
        out << flatText(gridX + c * colW + 6, headerY, "7", 15, tertiary,
                        theme.font, escapeText(columnLabels[c]));
    }

    // Rows. Capped so the grid can never grow down into the hint line and
    // settings bar, however many visible rows the caller asks for.
    const int maxRows = qMax(1, ((sa.y1 - 44) - gridY) / rowH);
    for (int i = 0; i < qMin(visibleRows, maxRows); ++i) {
        const int r = firstRow + i;
        if (r < 0 || r >= rows.size())
            break;
        const GuideRow &row = rows[r];
        const int y = gridY + i * rowH;

        const QString chanLabel =
            QString::number(row.number).rightJustified(2, '0') + "  " + row.name;
        const bool rowSelected = (r == selRow);
        // selCol == -1 is the channel-name cell, left of the first time slot.
        // It gets the same accent-block treatment as a programme cell so it
        // reads as a selectable thing rather than a row label.
        const bool nameSelected = rowSelected && selCol < 0;
        if (nameSelected)
            out << fillRect(sa.x0 - 2, y + 2, chanW - 4, rowH - 6, accent, 0x00);
        out << flatText(sa.x0 + 4, y + 8, "7", 17,
                        nameSelected ? surface : (rowSelected ? primary : secondary),
                        theme.font,
                        escapeText(elide(chanLabel, chanW - 10, 17)));

        for (int c = 0; c < nCols; ++c) {
            const int x = gridX + c * colW;
            const bool selected = rowSelected && c == selCol;
            if (selected) {
                // Selection is an accent block with inverted text — the same
                // treatment 240-MP's list views use for the current row.
                out << fillRect(x + 2, y + 2, colW - 4, rowH - 6, accent, 0x00);
            }
            const QString cell = c < row.cells.size() ? row.cells[c] : QString();
            out << flatText(x + 8, y + 8, "7", 17,
                            selected ? surface : primary, theme.font,
                            escapeText(elide(cell, colW - 16, 17)));
        }
    }

    // Hints get their own line above the bar. Sharing one line with the order
    // toggle meant the two could collide and clip each other once the toggle's
    // label grew — and a right-aligned block is the first thing a tube's
    // overscan eats.
    //
    // ASCII only, deliberately: VCR OSD Mono has no arrow glyphs, so ▲▼◀▶ fall
    // back to the bundled Unifont and render double-width, which is what made
    // the widths blow past their budget in the first place.
    const int hintY = sa.y1 - 36;
    out << flatText(sa.x0, hintY, "7", 13, tertiary, theme.font,
                    QStringLiteral("UP/DN: CHAN   L/R: SLOT+NAME   SELECT: WATCH   BACK: CLOSE"));

    // Settings bar along the bottom. The order toggle lives here rather than
    // above the grid so the channel rows start at the top, where the eye goes.
    const int barY   = sa.y1 - 16;
    const int barFs  = 15;
    if (optionSelected) {
        // Size the highlight to the label rather than a fixed width, so a long
        // channel name cannot spill out of its own block.
        const int w = int(optionText.size() * barFs * kCharWidthRatio) + 12;
        out << fillRect(sa.x0 - 2, barY - 3, qMin(w, sa.x1 - sa.x0 + 4), 21, accent, 0x00);
    }
    out << flatText(sa.x0 + 2, barY, "7", barFs,
                    optionSelected ? surface : secondary, theme.font,
                    escapeText(optionText));

    return out.join(QLatin1Char('\n'));
}

// Row geometry lives here, in one place, so the capacity queries below and the
// renderers themselves can never disagree about how many rows fit.
namespace {
constexpr int kOptionRowH  = 30;
constexpr int kOptionListY = 56;
constexpr int kEpisodeRowH  = 26;
constexpr int kEpisodeListY = 62;

int capacityFor(const Style &style, int listYOffset, int rowH) {
    const SafeArea sa = safeAreaCentred(style);
    const int listY = sa.y0 + listYOffset;
    // The -44 keeps the hint line clear at the bottom.
    return qMax(1, ((sa.y1 - 44) - listY) / rowH);
}
}  // namespace

int optionListCapacity(const Style &style) {
    return capacityFor(style, kOptionListY, kOptionRowH);
}

int episodeListCapacity(const Style &style) {
    return capacityFor(style, kEpisodeListY, kEpisodeRowH);
}

QString episodeList(const QString &channelLabel,
                    const QStringList &titles,
                    int firstRow, int visibleRows, int selRow,
                    const GuideTheme &theme,
                    const Style &style) {
    const SafeArea sa = safeAreaCentred(style);
    const QString primary   = hexToAss(theme.primary);
    const QString secondary = hexToAss(theme.secondary);
    const QString tertiary  = hexToAss(theme.tertiary);
    const QString surface   = hexToAss(theme.surface);
    const QString accent    = hexToAss(theme.accent);

    // Tighter than the guide's 38: no second line per row to make room for, and
    // a long channel wants as many titles on screen as the tube will take.
    const int rowH  = kEpisodeRowH;
    const int listY = sa.y0 + kEpisodeListY;

    QStringList out;
    out << fillRect(sa.x0 - 8, sa.y0 - 6,
                    (sa.x1 - sa.x0) + 16,
                    (sa.y1 - sa.y0) + 12, surface, 0x1A);

    out << flatText(sa.x0, sa.y0, "7", 24, primary, theme.font,
                    escapeText(elide(channelLabel, sa.x1 - sa.x0, 24)));
    out << flatText(sa.x0, sa.y0 + 32, "7", 15, secondary, theme.font,
                    escapeText(QStringLiteral("%1 EPISODES").arg(titles.size())));

    const int maxRows = episodeListCapacity(style);
    for (int i = 0; i < qMin(visibleRows, maxRows); ++i) {
        const int r = firstRow + i;
        if (r < 0 || r >= titles.size())
            break;
        const int y = listY + i * rowH;
        const bool selected = (r == selRow);
        if (selected)
            out << fillRect(sa.x0 - 2, y + 1, (sa.x1 - sa.x0) + 4, rowH - 4, accent, 0x00);
        // A leading index keeps the list scannable when titles are long and
        // near-identical, which is the normal case for a scene-release library.
        const QString label = QString::number(r + 1).rightJustified(3, ' ')
                              + "  " + titles[r];
        out << flatText(sa.x0 + 4, y + 4, "7", 15,
                        selected ? surface : primary, theme.font,
                        escapeText(elide(label, (sa.x1 - sa.x0) - 10, 15)));
    }

    // Whether a break airs in front of the pick lives on the guide's settings
    // page, not here — this list is for choosing an episode.
    const int hintY = sa.y1 - 36;
    out << flatText(sa.x0, hintY, "7", 13, tertiary, theme.font,
                    QStringLiteral("UP/DN: EPISODE   SELECT: WATCH   BACK: GUIDE"));

    return out.join(QLatin1Char('\n'));
}

QString optionList(const QString &title,
                   const QStringList &labels,
                   const QStringList &values,
                   int selRow,
                   int firstRow, int visibleRows,
                   const QString &hint,
                   bool compact,
                   const GuideTheme &theme,
                   const Style &style) {
    const SafeArea sa = safeAreaCentred(style);
    const QString primary   = hexToAss(theme.primary);
    const QString secondary = hexToAss(theme.secondary);
    const QString tertiary  = hexToAss(theme.tertiary);
    const QString surface   = hexToAss(theme.surface);
    const QString accent    = hexToAss(theme.accent);

    const int titleFs = compact ? 15 : 24;
    const int rowFs   = compact ? 13 : 17;
    const int rowH    = compact ? 20 : kOptionRowH;
    // Compact sits in the top-left corner over the pattern; full spans the
    // safe area like the guide does.
    const int panelW  = compact ? 250 : (sa.x1 - sa.x0);
    const int listY   = sa.y0 + (compact ? 26 : kOptionListY);

    // How many rows actually fit. The full-width variant is bounded by the safe
    // area (leaving the hint line clear); the compact one is bounded only by
    // what the caller asked for, since it is deliberately small.
    const int fits  = compact ? visibleRows : optionListCapacity(style);
    const int shown = qBound(0, qMin(visibleRows, fits),
                             qMax(0, labels.size() - qMax(0, firstRow)));

    QStringList out;
    if (compact) {
        // Just enough backing to stay readable over moving video, sized to the
        // rows on screen rather than the whole list.
        out << fillRect(sa.x0 - 6, sa.y0 - 6, panelW + 12,
                        (listY - sa.y0) + shown * rowH + 30, surface, 0x1A);
    } else {
        out << fillRect(sa.x0 - 8, sa.y0 - 6,
                        (sa.x1 - sa.x0) + 16,
                        (sa.y1 - sa.y0) + 12, surface, 0x1A);
    }

    out << flatText(sa.x0, sa.y0, "7", titleFs, primary, theme.font,
                    escapeText(elide(title, panelW, titleFs)));

    // Values are right-aligned against the panel edge so they form a column the
    // eye can run down, however uneven the labels are.
    const int valueX = sa.x0 + panelW - 4;
    for (int i = 0; i < shown; ++i) {
        const int r = firstRow + i;
        if (r < 0 || r >= labels.size())
            break;
        const int y = listY + i * rowH;
        const bool selected = (r == selRow);
        if (selected)
            out << fillRect(sa.x0 - 2, y + 1, panelW + 4, rowH - 4, accent, 0x00);
        out << flatText(sa.x0 + 4, y + 3, "7", rowFs,
                        selected ? surface : primary, theme.font,
                        escapeText(elide(labels[r], panelW - 90, rowFs)));
        out << flatText(valueX, y + 3, "9", rowFs,
                        selected ? surface : secondary, theme.font,
                        escapeText(values.value(r)));
    }

    if (!hint.isEmpty()) {
        const int hintY = compact ? listY + shown * rowH + 4
                                  : sa.y1 - 36;
        out << flatText(sa.x0, hintY, "7", 13, tertiary, theme.font, hint);
    }

    return out.join(QLatin1Char('\n'));
}

}  // namespace TvOverlay
