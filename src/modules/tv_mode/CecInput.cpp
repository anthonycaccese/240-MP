#include "CecInput.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QProcess>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
// cec-client prints `key pressed: <name> (<code>)` at log level 8.
const QRegularExpression kKeyPressed(
    QStringLiteral(R"(key pressed:\s*(.+?)\s*(?:\(|$))"),
    QRegularExpression::CaseInsensitiveOption);
}  // namespace

CecInput::CecInput(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
{}

CecInput::~CecInput() { stop(); }

void CecInput::setTargetWindow(QQuickWindow *window) { m_window = window; }

bool CecInput::isRunning() const { return m_proc != nullptr; }

bool CecInput::enabledInConfig() const {
    // App-level "cec_input" in config.json. Absent means enabled: the real gate
    // is whether cec-client is installed, so this exists only to switch it off
    // without uninstalling cec-utils.
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QFile::ReadOnly))
        return true;
    const QJsonObject app = QJsonDocument::fromJson(f.readAll())
                                .object().value("app").toObject();
    if (!app.contains("cec_input"))
        return true;
    return app.value("cec_input").toVariant().toBool();
}

void CecInput::start() {
    if (m_proc)
        return;
    if (!enabledInConfig()) {
        qInfo("[CecInput] disabled by config (app.cec_input = false)");
        return;
    }
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("cec-client"));
    if (bin.isEmpty()) {
        // Expected on a composite setup, and on any box without cec-utils.
        qInfo("[CecInput] cec-client not found — HDMI-CEC input disabled "
              "(install cec-utils to enable; requires an HDMI connection)");
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyRead, this, &CecInput::onReadyRead);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &CecInput::onFinished);

    const QStringList args{
        "-t", "p",          // register as a Playback device, so the TV forwards keys
        "-o", "240-MP",     // the name the TV shows for this device
        "-d", "8",          // log level that includes key traffic
    };
    m_proc->start(bin, args);
    qInfo("[CecInput] HDMI-CEC input starting via %s", qPrintable(bin));
}

void CecInput::stop() {
    if (!m_proc)
        return;
    m_proc->disconnect();
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        m_proc->waitForFinished(2000);
        if (m_proc->state() != QProcess::NotRunning)
            m_proc->kill();
    }
    m_proc->deleteLater();
    m_proc = nullptr;
}

void CecInput::onFinished() {
    // cec-client exits when no CEC adapter is present — the normal case on a
    // composite setup. Report once and stay quiet rather than respawning.
    qInfo("[CecInput] cec-client exited — HDMI-CEC input inactive "
          "(no CEC adapter? CEC needs an HDMI connection)");
    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void CecInput::onReadyRead() {
    if (!m_proc)
        return;
    m_buffer.append(m_proc->readAll());
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(nl)).trimmed();
        m_buffer.remove(0, nl + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
    // Don't let a chatty adapter grow the buffer without bound if a line never
    // terminates.
    if (m_buffer.size() > 64 * 1024)
        m_buffer.clear();
}

void CecInput::handleLine(const QString &line) {
    const QRegularExpressionMatch m = kKeyPressed.match(line);
    if (!m.hasMatch())
        return;
    const int qtKey = qtKeyForCecKey(m.captured(1).trimmed().toLower());
    if (qtKey != 0)
        deliver(qtKey);
}

void CecInput::deliver(int qtKey) {
    // While the Qt window is inactive, fullscreen mpv holds OS focus and QML has
    // no activeFocusItem — drive mpv over IPC instead, exactly as InputManager
    // does for gamepads.
    if (!m_window || !m_window->isActive()) {
        const QString mpvKey = mpvKeyForQtKey(qtKey);
        if (!mpvKey.isEmpty())
            emit mpvKeyRequested(mpvKey);
        if (!m_window)
            return;
    }
    QCoreApplication::postEvent(
        m_window, new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier));
    QCoreApplication::postEvent(
        m_window, new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier));
}

int CecInput::qtKeyForCecKey(const QString &name) {
    // Names as libCEC prints them for the CEC user-control codes. Channel
    // up/down deliberately map to the arrows: inside a TV session those already
    // mean "change channel", and outside one they navigate a list.
    if (name == "up"       || name == "channel up")   return Qt::Key_Up;
    if (name == "down"     || name == "channel down") return Qt::Key_Down;
    if (name == "left")                               return Qt::Key_Left;
    if (name == "right")                              return Qt::Key_Right;
    if (name == "select"   || name == "ok"
        || name == "enter")                           return Qt::Key_Return;
    if (name == "exit"     || name == "back"
        || name == "return")                          return Qt::Key_Escape;
    if (name == "play"     || name == "pause"
        || name == "play/pause" || name == "pause play function"
        || name == "play function")                   return Qt::Key_Space;
    if (name == "stop")                               return Qt::Key_Escape;
    // Everything else (colour keys, numbers, volume — the TV handles its own
    // volume) is deliberately ignored.
    return 0;
}

QString CecInput::mpvKeyForQtKey(int qtKey) {
    switch (qtKey) {
    case Qt::Key_Up:     return QStringLiteral("UP");
    case Qt::Key_Down:   return QStringLiteral("DOWN");
    case Qt::Key_Left:   return QStringLiteral("LEFT");
    case Qt::Key_Right:  return QStringLiteral("RIGHT");
    case Qt::Key_Return: return QStringLiteral("ENTER");
    case Qt::Key_Escape: return QStringLiteral("ESC");
    case Qt::Key_Space:  return QStringLiteral("SPACE");
    default:             return QString();
    }
}
