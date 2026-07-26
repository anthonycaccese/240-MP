#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>

class QNetworkAccessManager;
class QTimer;

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
    Q_PROPERTY(bool        hasData          READ hasData          NOTIFY dataChanged)
    // Location's own UTC offset, so the status-bar clock shows the time where
    // the weather is rather than where the device is.
    Q_PROPERTY(int         utcOffsetSeconds READ utcOffsetSeconds NOTIFY dataChanged)

public:
    explicit WeatherBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);

    QString     locationName()     const { return m_locationName; }
    QVariantMap  current()  const { return m_current; }
    QVariantList forecast() const { return m_forecast; }
    bool        hasData()          const { return m_hasData; }
    int         utcOffsetSeconds() const { return m_utcOffset; }

    // Absolute path to weather_location.txt, so the setup screen can say exactly
    // where to put it.
    Q_INVOKABLE QString location_file_path() const;

    // Resolve the location (if not already) and fetch, then keep refreshing
    // until stop(). Answers with dataChanged() or locationError().
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // options_slot for the "displays" multiselect. Ids match Weather.qml's
    // allScreens list.
    Q_INVOKABLE void getDisplays();

signals:
    void dataChanged();
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
    void fetchWeather();
    void buildForecast(const QJsonObject &daily);

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

    QVariantMap  m_current;
    QVariantList m_forecast;
    bool        m_hasData   = false;
    int         m_utcOffset = 0;
};
