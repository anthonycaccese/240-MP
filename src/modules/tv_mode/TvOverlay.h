#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

// The on-screen display: the green channel banner a cable box flashes when you
// change channels, drawn as ASS overlays through mpv's osd-overlay command.
//
// Ported from NostalgiaBox's `overlay.py` (MIT — see THIRD-PARTY.md), with the
// geometry re-derived for composite output:
//
//   * NostalgiaBox lays out a 960x720 4:3 frame centred inside a 1280x720 16:9
//     canvas, because it assumes an HDMI TV. On composite the whole screen IS
//     4:3, so the pillarbox offsets are gone and the canvas is the picture.
//   * The canvas is 640x480 rather than the framebuffer's actual 720x480. NTSC
//     pixels are not square (~8:9); drawing on a canvas that matched the raster
//     would stretch everything horizontally and turn circles into ellipses.
//     640x480 is square-pixel 4:3, and mpv maps it onto the real raster for us.
//   * Every coordinate and font size is NostalgiaBox's frame-relative value
//     scaled by exactly 2/3 (640/960 == 480/720).
//
// The volume bar is deliberately NOT ported: volume lives on the TV, which draws
// its own OSD (see the plan, §5b/§6).
namespace TvOverlay {

// Virtual canvas passed to osd-overlay as res_x/res_y.
constexpr int CanvasW = 640;
constexpr int CanvasH = 480;

// One overlay id per kind, so each can be replaced or cleared independently.
constexpr int IdChannel = 1;
constexpr int IdMessage = 4;
constexpr int IdCalibrate = 5;

struct Style {
    QString font  = QStringLiteral("VCR OSD Mono");  // bundled with 240-MP
    QString color = QStringLiteral("#4DFF5A");       // CRT phosphor green

    // Outline. NostalgiaBox draws a GREEN blurred border around green text —
    // bloom, which reads well on a sharp LCD. Over composite it is low-contrast
    // mush, and NTSC's narrow chroma bandwidth smears saturated green further.
    // A black outline is dramatically more legible on a tube, so that is the
    // default; set `bloom` to true for NostalgiaBox's original look.
    bool    bloom  = false;
    double  border = 2.0;   // outline width, in canvas units
    // A little blur kills aliased edges and, usefully, damps the inter-field
    // shimmer that hard thin lines produce on 480i. Too much and it smears —
    // NostalgiaBox's value of 4 was authored for a canvas 1.5x this one's size.
    double  blur   = 0.8;

    // Fraction of each edge treated as unsafe. Consumer tubes overscan, and how
    // much varies per set — 10% is the classic title-safe starting point, meant
    // to be calibrated against the pattern (plan, Phase 4).
    //
    // Per-edge rather than one number because tube overscan is rarely centred:
    // a set can clip noticeably more on the left than the right, and forcing a
    // symmetric inset then wastes space on three edges to clear the worst one.
    // A negative value means "use `safe`".
    double  safe        = 0.10;
    double  safeLeft    = -1.0;
    double  safeRight   = -1.0;
    double  safeTop     = -1.0;
    double  safeBottom  = -1.0;

    // Edge-anchored elements (the top-right banner, the centre-left sleep
    // readout) use the per-edge values directly.
    double  leftFrac()   const { return safeLeft   >= 0 ? safeLeft   : safe; }
    double  rightFrac()  const { return safeRight  >= 0 ? safeRight  : safe; }
    double  topFrac()    const { return safeTop    >= 0 ? safeTop    : safe; }
    double  bottomFrac() const { return safeBottom >= 0 ? safeBottom : safe; }

    // CENTRED elements — the guide panel — use the worst case of each opposing
    // pair instead, so they stay optically centred on the screen rather than
    // being shoved off-axis by an uneven inset, while still clearing the tighter
    // edge. Using the per-edge values directly here is what made the guide look
    // off-centre once left and right differed.
    double  symmetricX() const { return leftFrac() > rightFrac()  ? leftFrac() : rightFrac(); }
    double  symmetricY() const { return topFrac()  > bottomFrac() ? topFrac()  : bottomFrac(); }

    // Font sizes, in canvas units. Defaults are NostalgiaBox's scaled by 2/3,
    // except the show name, which was too small to read over composite at that
    // scale and is bumped.
    int     numberSize  = 59;   // "CH 03"
    int     nameSize    = 34;   // show name (2/3 of 40 would be 27 — too small)
    int     messageSize = 40;   // "NO SIGNAL" etc.
};

// "CH 03" plus the show name, flashed in the top-right of the safe area.
// Top-RIGHT specifically: this set parks its own "VIDEO 1" label top-left.
QString channelBanner(int number, const QString &name, const Style &style);

// Where a message sits. Top-centre for transient notices ("NO SIGNAL"); centre
// left for the sleep readout, which wants to stay clear of the channel banner.
enum class MessagePos { TopCentre, CentreLeft };

QString message(const QString &text, const Style &style,
                MessagePos pos = MessagePos::TopCentre);

// Overscan calibration: nested labelled rectangles at 5/10/15/20% inset, plus a
// centre cross. Whichever box is fully visible on the tube is the safe-area
// value to configure.
//
// Deliberately drawn through the same ASS path as the banner rather than as a
// video test card: that way it measures the exact coordinate space the OSD uses,
// including any scaling mpv applies between canvas and raster.
QString calibrationPattern(const Style &style);

// ---------------------------------------------------------------------------
// TV guide
// ---------------------------------------------------------------------------

// The guide is drawn over live video rather than in Qt, because on EGLFS mpv owns
// the framebuffer for the whole session — a Qt view would mean tearing the
// session down and rebuilding it. It uses 240-MP's own colour scheme, passed in
// from QML (the themes are defined there, including user-defined ones).
struct GuideTheme {
    QString primary, secondary, tertiary, surface, accent;
    QString font = QStringLiteral("VCR OSD Mono");
};

struct GuideRow {
    int         number = 0;
    QString     name;
    QStringList cells;   // upcoming episode titles, one per column
};

// `firstRow` is the top visible channel, so the caller can scroll a long lineup.
// `selRow` is absolute (an index into `rows`), `selCol` indexes into cells.
// The guide needs the same per-edge insets as everything else.
QString guideGrid(const QVector<GuideRow> &rows,
                  const QStringList &columnLabels,
                  int firstRow, int visibleRows,
                  int selRow, int selCol,
                  const QString &detailTitle,
                  const QString &detailSub,
                  // Options row, rendered on the header line. selRow == -1
                  // selects it, so it is reached by navigating up off the top.
                  const QString &optionText,
                  bool optionSelected,
                  const GuideTheme &theme,
                  const Style &style);

constexpr int IdGuide = 6;

// The full episode list for one channel, reached from the guide by navigating
// left off the first time slot onto the channel name. The guide answers "what
// is on"; this answers "what else is there", which a 500-episode channel needs
// and three upcoming slots cannot give.
//
// `selRow` indexes `titles` absolutely; `firstRow` scrolls, exactly as in
// guideGrid.
QString episodeList(const QString &channelLabel,
                    const QStringList &titles,
                    int firstRow, int visibleRows, int selRow,
                    const GuideTheme &theme,
                    const Style &style);

constexpr int IdEpisodes = 7;

// A titled list of label/value rows with one selected — the shape shared by the
// guide's settings page and the overscan readout, so both look like the same
// box rather than two different dialogs.
//
// `compact` drops the full-width backing panel and tucks the list into the top
// left at a smaller size, which is what lets the overscan readout sit over the
// calibration pattern without hiding the very boxes being measured.
// How many rows the full-width optionList / episodeList can actually show at
// the CURRENT safe area. Callers must ask rather than assume a constant: the
// safe area is adjustable at runtime from the overscan screen, and scroll state
// computed against a larger number lets the selected row fall off the bottom.
int optionListCapacity(const Style &style);
int episodeListCapacity(const Style &style);

// `firstRow`/`visibleRows` scroll the window exactly as in guideGrid, so a list
// longer than the safe area (every channel, say) pages instead of overflowing.
QString optionList(const QString &title,
                   const QStringList &labels,
                   const QStringList &values,
                   int selRow,
                   int firstRow, int visibleRows,
                   const QString &hint,
                   bool compact,
                   const GuideTheme &theme,
                   const Style &style);

constexpr int IdSettings = 8;

// Exposed for tests and for callers that want to escape their own text.
QString escapeText(const QString &text);
QString hexToAss(const QString &hexColor, int alpha = 0);

}  // namespace TvOverlay
