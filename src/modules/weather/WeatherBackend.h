#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QTimer;
class QProcess;
class QLocalSocket;

// Backend for the weather module.
//
// Fetches Open-Meteo directly and exposes display-ready strings. Formatting
// lives here rather than in QML because every field's presentation depends on
// the Units setting, and the screens are literally fixed lines of text — so a
// property like current.temperature is already "66°" and the view stays dumb.
//
// The location comes from $DATA_ROOT/weather_location.txt: a plain file rather
// than a setting, because the manifest schema has no free-text type and an
// on-screen keyboard for a value you set once would be worse. A place name is
// geocoded via Open-Meteo (no API key); an explicit "lat, lon" is taken as-is.
class WeatherBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString     locationName     READ locationName     NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap current          READ current          NOTIFY dataChanged)
    // Three days, each a map of display-ready strings: name, condition, lo, hi.
    Q_PROPERTY(QVariantList forecast        READ forecast         NOTIFY dataChanged)
    // { days: [{name, sunrise, sunset}], moons: [{name, date}] }
    Q_PROPERTY(QVariantMap  almanac         READ almanac          NOTIFY dataChanged)
    // [{name, temp, condition, wind}] for the extra places listed in
    // weather_location.txt. Empty when none are listed, which drops the screen
    // out of the rotation rather than showing an empty table.
    Q_PROPERTY(QVariantList otherLocations  READ otherLocations   NOTIFY dataChanged)
    Q_PROPERTY(bool hasOtherLocations       READ hasOtherLocations NOTIFY dataChanged)
    // "°F" or "°C", for the table's column heading.
    Q_PROPERTY(QString tempUnitLabel        READ tempUnitLabel    NOTIFY dataChanged)
    Q_PROPERTY(bool        hasData          READ hasData          NOTIFY dataChanged)
    // Location's own UTC offset, so the status-bar clock shows the time where
    // the weather is rather than where the device is.
    Q_PROPERTY(int         utcOffsetSeconds READ utcOffsetSeconds NOTIFY dataChanged)
    // Whether background music is currently audible — running and not paused.
    // Nothing binds it yet; it exists so an on-screen indicator is a QML-only
    // change.
    Q_PROPERTY(bool musicPlaying READ musicPlaying NOTIFY musicStateChanged)

public:
    explicit WeatherBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);
    ~WeatherBackend() override;

    QString     locationName()     const { return m_locationName; }
    QVariantMap  current()  const { return m_current; }
    QVariantList forecast() const { return m_forecast; }
    QVariantMap  almanac()  const { return m_almanac; }
    QVariantList otherLocations() const { return m_otherLocations; }
    bool hasOtherLocations() const { return !m_otherLocations.isEmpty(); }
    QString tempUnitLabel() const;
    bool        hasData()          const { return m_hasData; }
    int         utcOffsetSeconds() const { return m_utcOffset; }

    bool musicPlaying() const;

    // Absolute path to weather_location.txt, so the setup screen can say exactly
    // where to put it.
    Q_INVOKABLE QString location_file_path() const;
    // Absolute path to the optional weather_music.txt playlist. The app never
    // writes this file — it is opt-in, and absent means the built-in list.
    Q_INVOKABLE QString music_file_path() const;

    // Resolve the location (if not already) and fetch, then keep refreshing
    // until stop(). Answers with dataChanged() or locationError().
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // Background music, as a plain mpv subprocess — no video, no screen takeover.
    // A no-op when the Music setting is off, so the view can call it
    // unconditionally.
    Q_INVOKABLE void startMusic();
    Q_INVOKABLE void stopMusic();
    // Pause/resume in place over mpv's IPC socket, so the current track survives
    // the toggle. Session-only: it deliberately does not write the setting.
    Q_INVOKABLE void toggleMusic();

    // options_slot for the "displays" multiselect. Ids match Weather.qml's
    // allScreens list.
    Q_INVOKABLE void getDisplays();

signals:
    void dataChanged();
    void musicStateChanged();
    // reason ∈ {missing, unreadable, empty, notfound, network}
    void locationError(const QString &reason);
    void fetchError(const QString &message);
    void dynamicOptionsReady(const QString &key, const QVariant &options);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    QJsonObject loadConfig() const;
    QJsonObject moduleConfig() const;
    bool        useUsUnits() const;

    void resolveLocation();
    void geocode(const QString &rawLine);
    // '#'-commented, one-entry-per-line config files: the location list and the
    // music playlist share the format, so they share the reader.
    QStringList readListFile(const QString &path) const;
    // Shared by the primary location and the extras.
    QStringList readLocationLines() const;
    // The music playlist: weather_music.txt if present, otherwise the built-in
    // list. Entries are mpv-ready — URLs percent-encoded, local paths absolute.
    QStringList musicPlaylist() const;
    static bool parseCoordLine(const QString &line, double *lat, double *lon, QString *label);
    void geocodeLine(const QString &line,
                     const std::function<void(bool, QString, double, double)> &done);
    void resolveOthers(const QStringList &lines);
    void fetchOthers();
    void fetchWeather();

    // Station observations (US only). Resolved once per location, then polled
    // alongside fetchWeather(). Every failure here is non-fatal: the module
    // keeps whatever Open-Meteo already put in m_current.
    //
    // Both the primary location and the Other Locations extras use the same two
    // steps, so they are parameterised rather than duplicated: resolve nearby
    // stations for a point, then walk them until one answers with something
    // current.
    void resolveStationsFor(double lat, double lon,
                            const std::function<void(const QStringList &)> &done);
    void fetchObservationChain(const QStringList &stations, int index,
                               const std::function<void(const QVariantMap &,
                                                        const QDateTime &,
                                                        const QString &)> &done);

    void resolveStations();
    void fetchObservation();
    void applyObservation();

    void resolveOtherStations();
    void fetchOtherObservations();
    void applyOtherObservations();
    void buildForecast(const QJsonObject &daily);
    void buildAlmanac(const QJsonObject &daily);
    bool useTwelveHour() const;

    // Always answer on the next event-loop turn, never synchronously — views
    // call start() from Component.onCompleted, and a synchronous reply there
    // fires while the Loader's item is still being constructed, so the
    // setSource() that swaps in the setup screen is silently dropped.
    void emitError(const QString &reason);

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer  *m_refresh = nullptr;

    // Resolved location, cached for the life of the process. Deliberately not
    // persisted: the project writes config.json only on direct user
    // interaction, and re-geocoding once per module entry is one small request.
    QString m_locationName;
    double  m_lat = 0.0;
    double  m_lon = 0.0;
    bool    m_resolved = false;
    QString m_resolvedFrom;
    // Whether the sun is up at the primary location, from Open-Meteo. Cached
    // because the station overlay needs it and an observation cannot supply it.
    bool    m_isDay = true;

    // Nearest reporting stations, nearest first, cached for the life of the
    // process for the same reason the location is: config.json is written only
    // on direct user interaction. Empty outside the US, and after any failure.
    QStringList m_stations;
    bool        m_stationsResolved = false;
    // Last good observation, kept raw (metric, as NWS always sends it) rather
    // than formatted, so a units change re-formats without a re-fetch. Applied
    // over m_current *after* every fetchWeather(), which rebuilds it wholesale
    // and would otherwise wipe the overlay depending on which reply lands last.
    QVariantMap m_obs;
    QDateTime   m_obsTime;
    QString     m_obsStation;

    // Same three things per extra location, keyed by index into m_otherPoints.
    // Sparse on purpose: an international extra simply has no entry, and the
    // model's value for that row stands.
    QHash<int, QStringList> m_otherStations;
    QHash<int, QVariantMap> m_otherObs;
    QHash<int, QDateTime>   m_otherObsTime;
    // Row → m_otherPoints index. The table skips extras the API returned
    // nothing for, so row order and point order are not the same list.
    QList<int>              m_otherRowPoint;
    // Daylight per extra, from the model row. Stations report sky and weather,
    // never whether the sun is up, so the overlay borrows it from underneath.
    QHash<int, bool>        m_otherIsDay;

    // Background music. Its own mpv, its own socket — entirely separate from
    // MpvController's, which owns the screen and must not be disturbed by it.
    QProcess     *m_music       = nullptr;
    QLocalSocket *m_musicIpc    = nullptr;
    QTimer       *m_musicConnect = nullptr;
    QString       m_musicSocketPath;
    bool          m_musicPaused = false;

    QVariantMap  m_current;
    QVariantList m_forecast;
    QVariantMap  m_almanac;
    QVariantList m_otherLocations;
    // Extra places from weather_location.txt, resolved once: {name, lat, lon}.
    QVariantList m_otherPoints;
    int          m_pendingOthers = 0;
    bool        m_hasData   = false;
    int         m_utcOffset = 0;
};
