#include "WeatherBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QtMath>
#include <QDebug>

namespace {

const char *kModuleId   = "com.240mp.weather";
const char *kForecastUrl = "https://api.open-meteo.com/v1/forecast";
const char *kGeocodeUrl  = "https://geocoding-api.open-meteo.com/v1/search";

// Open-Meteo publishes data roughly every 15 minutes.
constexpr int kRefreshMs = 10 * 60 * 1000;

// WMO 4677 weather interpretation codes, as short uppercase strings.
//
// Note these merge two things a METAR keeps separate: sky condition (CLEAR,
// OVERCAST) and present weather (RAIN, FOG, THUNDERSTORM). One code, so one
// condition line — which is what the WeatherStar layout wants anyway.
QString conditionForCode(int code) {
    switch (code) {
    case 0:  return QStringLiteral("CLEAR");
    case 1:  return QStringLiteral("MAINLY CLEAR");
    case 2:  return QStringLiteral("PARTLY CLOUDY");
    case 3:  return QStringLiteral("OVERCAST");
    case 45: case 48:            return QStringLiteral("FOG");
    case 51: case 53: case 55:   return QStringLiteral("DRIZZLE");
    case 56: case 57:            return QStringLiteral("FREEZING DRIZZLE");
    case 61: case 63: case 65:   return QStringLiteral("RAIN");
    case 66: case 67:            return QStringLiteral("FREEZING RAIN");
    case 71: case 73: case 75:   return QStringLiteral("SNOW");
    case 77:                     return QStringLiteral("SNOW GRAINS");
    case 80: case 81: case 82:   return QStringLiteral("SHOWERS");
    case 85: case 86:            return QStringLiteral("SNOW SHOWERS");
    case 95:                     return QStringLiteral("THUNDERSTORM");
    case 96: case 99:            return QStringLiteral("THUNDERSTORM HAIL");
    default: break;
    }
    return QStringLiteral("UNKNOWN");
}

// Short forms for the Extended Forecast columns, which are a third of the screen
// wide. Long single words are the problem — WordWrap can't break "THUNDERSTORM",
// so it runs into the next column. The original had the same split: full wording
// on Current Conditions, abbreviations across the three-day columns.
//
// These also read as a *forecast* rather than an observation: a clear day ahead
// is SUNNY, whereas conditions right now are CLEAR.
QString conditionShortForCode(int code) {
    switch (code) {
    case 0: case 1:              return QStringLiteral("SUNNY");
    case 2:                      return QStringLiteral("PARTLY CLOUDY");  // wraps to two lines
    case 3:                      return QStringLiteral("CLOUDY");
    case 45: case 48:            return QStringLiteral("FOG");
    case 51: case 53: case 55:   return QStringLiteral("DRIZZLE");
    case 56: case 57:            return QStringLiteral("FRZ DRIZZLE");
    case 61: case 63: case 65:   return QStringLiteral("RAIN");
    case 66: case 67:            return QStringLiteral("FRZ RAIN");
    case 71: case 73: case 75:   return QStringLiteral("SNOW");
    case 77:                     return QStringLiteral("SNOW");
    case 80: case 81: case 82:   return QStringLiteral("SHOWERS");
    case 85: case 86:            return QStringLiteral("SNOW SHOWERS");
    case 95: case 96: case 99:   return QStringLiteral("T'STORMS");
    default: break;
    }
    return QStringLiteral("UNKNOWN");
}

// ── Location disambiguation ──────────────────────────────────────────────────
//
// Open-Meteo's geocoder has no country/region filter (countryCode, country_code
// and country are all silently ignored) and orders purely by population. So
// "Caldwell, NJ, USA" returns Caldwell **Idaho** first, and picking the first
// result shows confidently wrong weather with no error anywhere — the worst
// failure this module can have. Everything below exists to prevent that.
//
// Results carry: name, admin1 (region, spelled out), country, country_code,
// population. A qualifier is matched against those in several ways because
// people write locations in several ways.

// Aliases people actually type for countries whose ISO code isn't obvious.
QString countryAlias(const QString &q) {
    static const QHash<QString, QString> kAliases = {
        { "USA", "US" }, { "U.S.A.", "US" }, { "U.S.", "US" }, { "AMERICA", "US" },
        { "UNITED STATES OF AMERICA", "US" },
        { "UK", "GB" }, { "BRITAIN", "GB" }, { "GREAT BRITAIN", "GB" },
        { "ENGLAND", "GB" }, { "SCOTLAND", "GB" }, { "WALES", "GB" },
        { "HOLLAND", "NL" }, { "UAE", "AE" }, { "SOUTH KOREA", "KR" },
    };
    return kAliases.value(q.toUpper());
}

// US states and DC. Needed because "City, ST" is the dominant way Americans
// write a location, and the abbreviations aren't derivable from the spelled-out
// name the API returns (TX from Texas, CA from California…).
QString expandUsState(const QString &q) {
    static const QHash<QString, QString> kStates = {
        {"AL","Alabama"},{"AK","Alaska"},{"AZ","Arizona"},{"AR","Arkansas"},
        {"CA","California"},{"CO","Colorado"},{"CT","Connecticut"},{"DE","Delaware"},
        {"DC","District of Columbia"},{"FL","Florida"},{"GA","Georgia"},{"HI","Hawaii"},
        {"ID","Idaho"},{"IL","Illinois"},{"IN","Indiana"},{"IA","Iowa"},
        {"KS","Kansas"},{"KY","Kentucky"},{"LA","Louisiana"},{"ME","Maine"},
        {"MD","Maryland"},{"MA","Massachusetts"},{"MI","Michigan"},{"MN","Minnesota"},
        {"MS","Mississippi"},{"MO","Missouri"},{"MT","Montana"},{"NE","Nebraska"},
        {"NV","Nevada"},{"NH","New Hampshire"},{"NJ","New Jersey"},{"NM","New Mexico"},
        {"NY","New York"},{"NC","North Carolina"},{"ND","North Dakota"},{"OH","Ohio"},
        {"OK","Oklahoma"},{"OR","Oregon"},{"PA","Pennsylvania"},{"RI","Rhode Island"},
        {"SC","South Carolina"},{"SD","South Dakota"},{"TN","Tennessee"},{"TX","Texas"},
        {"UT","Utah"},{"VT","Vermont"},{"VA","Virginia"},{"WA","Washington"},
        {"WV","West Virginia"},{"WI","Wisconsin"},{"WY","Wyoming"},
    };
    return kStates.value(q.toUpper());
}

// "New South Wales" -> "NSW". Free generalisation that covers multi-word regions
// worldwide (BC, NSW, NT…) without another table.
QString initialsOf(const QString &s) {
    QString out;
    const QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &w : words)
        if (!w.isEmpty()) out += w.at(0).toUpper();
    return out;
}

bool qualifierMatches(const QJsonObject &r, const QString &qualifier) {
    const QString q       = qualifier.trimmed();
    if (q.isEmpty()) return true;
    const QString admin1  = r["admin1"].toString();
    const QString country = r["country"].toString();
    const QString code    = r["country_code"].toString();

    if (code.compare(q, Qt::CaseInsensitive) == 0)                       return true;
    if (countryAlias(q).compare(code, Qt::CaseInsensitive) == 0)         return true;
    if (admin1.compare(q, Qt::CaseInsensitive) == 0)                     return true;
    if (country.compare(q, Qt::CaseInsensitive) == 0)                    return true;
    if (expandUsState(q).compare(admin1, Qt::CaseInsensitive) == 0
            && !admin1.isEmpty())                                        return true;
    if (!admin1.isEmpty() && initialsOf(admin1).compare(q, Qt::CaseInsensitive) == 0)
                                                                         return true;
    // Substring only for longer qualifiers: the API returns "The Netherlands",
    // so an exact test fails for "Amsterdam, Netherlands" — but a substring test
    // on a 2-letter code would match "Paris, US" against "Australia".
    if (q.size() >= 4 && (admin1.contains(q, Qt::CaseInsensitive)
                          || country.contains(q, Qt::CaseInsensitive)))  return true;
    return false;
}

// 16-point compass, matching how the original displayed wind.
QString cardinal(double degrees) {
    static const char *points[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int idx = int(qRound(degrees / 22.5)) % 16;
    if (idx < 0) idx += 16;
    return QString::fromLatin1(points[idx]);
}

} // namespace

WeatherBackend::WeatherBackend(const QString &appRoot, const QString &dataRoot,
                               QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
    , m_refresh(new QTimer(this)) {
    m_refresh->setInterval(kRefreshMs);
    connect(m_refresh, &QTimer::timeout, this, [this]() { fetchWeather(); });
}

QString WeatherBackend::location_file_path() const {
    return m_dataRoot + QStringLiteral("/weather_location.txt");
}

QJsonObject WeatherBackend::loadConfig() const {
    QFile f(m_dataRoot + QStringLiteral("/config.json"));
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return {};
}

QJsonObject WeatherBackend::moduleConfig() const {
    return loadConfig()["modules"].toObject()[kModuleId].toObject();
}

bool WeatherBackend::useUsUnits() const {
    return moduleConfig()["units"].toString(QStringLiteral("Metric"))
               .compare(QLatin1String("US"), Qt::CaseInsensitive) == 0;
}

void WeatherBackend::onSettingChanged(const QString &moduleId, const QString &key,
                                      const QVariant &value) {
    Q_UNUSED(value)
    if (moduleId != QLatin1String(kModuleId)) return;
    // Units change the requested values, not just their presentation, so a
    // switch has to re-fetch rather than reformat.
    if (key == QLatin1String("units") && m_resolved)
        fetchWeather();
}

void WeatherBackend::getDisplays() {
    emit dynamicOptionsReady(QStringLiteral("displays"), QVariantList{
        QVariantMap{ { "id", "current"  }, { "label", "CURRENT CONDITIONS" } },
        QVariantMap{ { "id", "extended" }, { "label", "EXTENDED FORECAST"  } },
        QVariantMap{ { "id", "almanac"  }, { "label", "ALMANAC"            } },
    });
}

void WeatherBackend::emitError(const QString &reason) {
    QTimer::singleShot(0, this, [this, reason]() { emit locationError(reason); });
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void WeatherBackend::start() {
    if (m_resolved) {
        fetchWeather();
        m_refresh->start();
        return;
    }
    resolveLocation();
}

void WeatherBackend::stop() {
    m_refresh->stop();
}

// ── Location ─────────────────────────────────────────────────────────────────

void WeatherBackend::resolveLocation() {
    QFile f(location_file_path());
    if (!f.exists())                                      { emitError(QStringLiteral("missing"));    return; }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))   { emitError(QStringLiteral("unreadable")); return; }

    // First non-empty, non-comment line wins. '#' comments exist so the file we
    // tell users to create can document its own format.
    QString line;
    while (!f.atEnd()) {
        const QString candidate = QString::fromUtf8(f.readLine()).trimmed();
        if (candidate.isEmpty() || candidate.startsWith(QLatin1Char('#'))) continue;
        line = candidate;
        break;
    }
    if (line.isEmpty()) { emitError(QStringLiteral("empty")); return; }

    // Explicit coordinates, checked before geocoding so someone who knows
    // exactly where they want the forecast never depends on a name lookup. This
    // is the reliable escape hatch when a place name is ambiguous.
    //
    //     40.8398, -74.2765            -> labelled with the coordinates
    //     40.8398, -74.2765, CALDWELL  -> labelled "CALDWELL"
    //
    // The optional third segment exists because there is no way to recover a
    // name from coordinates: Open-Meteo's geocoder is forward-only (it returns
    // an error for lat/lon), and the forecast response carries only a timezone
    // — "America/New_York" would label a New Jersey town "NEW YORK", which is
    // worse than showing the numbers. A real reverse geocoder means a
    // third-party service with its own usage policy, so the user names it.
    const QStringList parts = line.split(QLatin1Char(','));
    if (parts.size() >= 2) {
        bool okLat = false, okLon = false;
        const double lat = parts.at(0).trimmed().toDouble(&okLat);
        const double lon = parts.at(1).trimmed().toDouble(&okLon);
        if (okLat && okLon && lat >= -90.0 && lat <= 90.0
                           && lon >= -180.0 && lon <= 180.0) {
            const QString label = parts.mid(2).join(QLatin1Char(',')).trimmed();
            m_locationName = label.isEmpty()
                ? QStringLiteral("%1, %2").arg(lat, 0, 'f', 4).arg(lon, 0, 'f', 4)
                : label.toUpper();
            m_lat = lat;
            m_lon = lon;
            m_resolved = true;
            m_resolvedFrom = line;
            qInfo("[Weather] using explicit coordinates -> %s (%.4f, %.4f)",
                  qPrintable(m_locationName), m_lat, m_lon);
            fetchWeather();
            m_refresh->start();
            return;
        }
    }

    geocode(line);
}

void WeatherBackend::geocode(const QString &rawLine) {
    // Open-Meteo's geocoder matches a bare place name, so send only the part
    // before the first comma. Anything after it disambiguates the results, which
    // matters for a worldwide userbase — "Paris, France" and "Paris, Texas" are
    // both reasonable inputs.
    const QStringList segments = rawLine.split(QLatin1Char(','));
    const QString name = segments.first().trimmed();
    // Every segment after the name is a qualifier — "Caldwell, NJ, USA" has two,
    // and both matter. Using only the first one is what let Idaho win.
    QStringList qualifiers;
    for (int i = 1; i < segments.size(); ++i) {
        const QString q = segments.at(i).trimmed();
        if (!q.isEmpty()) qualifiers << q;
    }

    QUrl url(QString::fromLatin1(kGeocodeUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"),     name);
    q.addQueryItem(QStringLiteral("count"),    QStringLiteral("10"));
    q.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    q.addQueryItem(QStringLiteral("format"),   QStringLiteral("json"));
    url.setQuery(q);

    QNetworkReply *reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, rawLine, name, qualifiers]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[Weather] geocode failed: %s", qPrintable(reply->errorString()));
            emitError(QStringLiteral("network"));
            return;
        }
        const QJsonArray results =
            QJsonDocument::fromJson(reply->readAll()).object()["results"].toArray();
        if (results.isEmpty()) { emitError(QStringLiteral("notfound")); return; }

        // Score every result against every qualifier and take the best. Results
        // arrive ordered by population, so a plain first-match would always pick
        // the biggest city of that name regardless of the state or country the
        // user asked for.
        QJsonObject chosen = results.first().toObject();
        if (!qualifiers.isEmpty()) {
            int bestScore = -1;
            for (const QJsonValue &v : results) {
                const QJsonObject o = v.toObject();
                int score = 0;
                for (const QString &q : qualifiers)
                    if (qualifierMatches(o, q)) ++score;
                // Strictly greater keeps the population ordering as the
                // tie-break, which is the right default among equal matches.
                if (score > bestScore) { bestScore = score; chosen = o; }
            }
            if (bestScore == 0) {
                qWarning("[Weather] no result matched any qualifier in \"%s\" — "
                         "falling back to the most populous match",
                         qPrintable(rawLine));
            } else if (bestScore < qualifiers.size()) {
                qWarning("[Weather] only %d of %lld qualifiers matched for \"%s\"",
                         bestScore, qualifiers.size(), qPrintable(rawLine));
            }
        }

        m_locationName = chosen["name"].toString(name).toUpper();
        m_lat = chosen["latitude"].toDouble();
        m_lon = chosen["longitude"].toDouble();
        m_resolved = true;
        m_resolvedFrom = rawLine;
        qInfo("[Weather] resolved \"%s\" -> %s, %s, %s (%.4f, %.4f)",
              qPrintable(rawLine), qPrintable(m_locationName),
              qPrintable(chosen["admin1"].toString()),
              qPrintable(chosen["country_code"].toString()), m_lat, m_lon);

        fetchWeather();
        m_refresh->start();
    });
}

// ── Weather ──────────────────────────────────────────────────────────────────

void WeatherBackend::fetchWeather() {
    if (!m_resolved) return;
    const bool us = useUsUnits();

    QUrl url(QString::fromLatin1(kForecastUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"),  QString::number(m_lat, 'f', 4));
    q.addQueryItem(QStringLiteral("longitude"), QString::number(m_lon, 'f', 4));
    q.addQueryItem(QStringLiteral("current"),
                   QStringLiteral("temperature_2m,relative_humidity_2m,dew_point_2m,"
                                  "pressure_msl,wind_speed_10m,wind_direction_10m,"
                                  "visibility,weather_code"));
    q.addQueryItem(QStringLiteral("daily"),
                   QStringLiteral("temperature_2m_min,temperature_2m_max,"
                                  "weather_code,sunrise,sunset"));
    q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("3"));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("temperature_unit"),
                   us ? QStringLiteral("fahrenheit") : QStringLiteral("celsius"));
    q.addQueryItem(QStringLiteral("wind_speed_unit"),
                   us ? QStringLiteral("mph") : QStringLiteral("kmh"));
    url.setQuery(q);

    QNetworkReply *reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, us]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[Weather] fetch failed: %s", qPrintable(reply->errorString()));
            emit fetchError(reply->errorString());
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject cur  = root["current"].toObject();
        if (cur.isEmpty()) {
            emit fetchError(QStringLiteral("empty response"));
            return;
        }

        m_utcOffset = root["utc_offset_seconds"].toInt();

        const double windSpeed = cur["wind_speed_10m"].toDouble();
        // Visibility is always metres regardless of the unit parameters, so it
        // is the one field converted by hand.
        const double visMetres  = cur["visibility"].toDouble();

        QVariantMap m;
        m["condition"]   = conditionForCode(cur["weather_code"].toInt());
        m["temperature"] = QString::number(qRound(cur["temperature_2m"].toDouble())) + QStringLiteral("°");
        m["humidity"]    = QString::number(qRound(cur["relative_humidity_2m"].toDouble())) + QStringLiteral("%");
        m["dewPoint"]    = QString::number(qRound(cur["dew_point_2m"].toDouble())) + QStringLiteral("°");
        m["pressure"]    = us
            ? QString::number(cur["pressure_msl"].toDouble() * 0.0295299830714, 'f', 2)
            : QString::number(cur["pressure_msl"].toDouble(), 'f', 1) + QStringLiteral(" MB");
        m["wind"] = QStringLiteral("%1 %2")
                        .arg(cardinal(cur["wind_direction_10m"].toDouble()))
                        .arg(qRound(windSpeed));
        m["visibility"] = us
            ? QString::number(qRound(visMetres / 1609.344)) + QStringLiteral(" MI.")
            : QString::number(qRound(visMetres / 1000.0))   + QStringLiteral(" KM");

        m_current = m;
        buildForecast(root["daily"].toObject());
        buildAlmanac(root["daily"].toObject());
        m_hasData = true;
        emit dataChanged();
    });
}

void WeatherBackend::buildForecast(const QJsonObject &daily) {
    m_forecast.clear();
    const QJsonArray times = daily["time"].toArray();
    const QJsonArray mins  = daily["temperature_2m_min"].toArray();
    const QJsonArray maxs  = daily["temperature_2m_max"].toArray();
    const QJsonArray codes = daily["weather_code"].toArray();
    if (times.isEmpty()) return;

    for (int i = 0; i < times.size(); ++i) {
        const QDate date = QDate::fromString(times.at(i).toString(), Qt::ISODate);
        QVariantMap day;
        day["name"] = date.isValid()
            ? QLocale::c().dayName(date.dayOfWeek(), QLocale::LongFormat).toUpper()
            : QString();
        day["condition"] = conditionShortForCode(codes.at(i).toInt());
        day["lo"] = QString::number(qRound(mins.at(i).toDouble()));
        day["hi"] = QString::number(qRound(maxs.at(i).toDouble()));

        m_forecast.append(day);
    }
}

// ── Moon phases ──────────────────────────────────────────────────────────────
//
// Meeus, "Astronomical Algorithms", ch. 49, truncated to the principal periodic
// terms (the planetary A1..A14 corrections contribute well under an hour and are
// omitted). Accuracy of a few minutes, which matters more than it sounds: the
// naive mean-synodic approximation is off by up to ~0.6 days, enough to move a
// phase across midnight and print the wrong date — it disagreed with a reference
// frame on two of the four phases.
//
// Open-Meteo has no moon data, so this is computed rather than fetched.
namespace {

double phaseJde(double k, double phase) {
    k += phase;
    const double T = k / 1236.85;
    double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * T * T
               - 0.000000150 * T * T * T + 0.00000000073 * T * T * T * T;

    const double E  = 1 - 0.002516 * T - 0.0000074 * T * T;
    const double M  = qDegreesToRadians(2.5534 + 29.10535670 * k
                                        - 0.0000014 * T * T - 0.00000011 * T * T * T);
    const double Mp = qDegreesToRadians(201.5643 + 385.81693528 * k
                                        + 0.0107582 * T * T + 0.00001238 * T * T * T);
    const double F  = qDegreesToRadians(160.7108 + 390.67050284 * k
                                        - 0.0016118 * T * T - 0.00000227 * T * T * T);
    const double Om = qDegreesToRadians(124.7746 - 1.56375588 * k
                                        + 0.0020672 * T * T + 0.00000215 * T * T * T);

    const bool isQuarter = (phase == 0.25 || phase == 0.75);
    if (!isQuarter) {
        const bool isNew = (phase == 0.0);
        jde += (isNew ? -0.40720 : -0.40614) * qSin(Mp)
             + (isNew ?  0.17241 :  0.17302) * E * qSin(M)
             + (isNew ?  0.01608 :  0.01614) * qSin(2 * Mp)
             + (isNew ?  0.01039 :  0.01043) * qSin(2 * F)
             + (isNew ?  0.00739 :  0.00734) * E * qSin(Mp - M)
             + (isNew ? -0.00514 : -0.00515) * E * qSin(Mp + M)
             + (isNew ?  0.00208 :  0.00209) * E * E * qSin(2 * M)
             - 0.00111 * qSin(Mp - 2 * F) - 0.00057 * qSin(Mp + 2 * F)
             + 0.00056 * E * qSin(2 * Mp + M) - 0.00042 * qSin(3 * Mp)
             + 0.00042 * E * qSin(M + 2 * F) + 0.00038 * E * qSin(M - 2 * F)
             - 0.00024 * E * qSin(2 * Mp - M) - 0.00017 * qSin(Om)
             - 0.00007 * qSin(Mp + 2 * M);
    } else {
        jde += -0.62801 * qSin(Mp) + 0.17172 * E * qSin(M)
             - 0.01183 * E * qSin(Mp + M) + 0.00862 * qSin(2 * Mp)
             + 0.00804 * qSin(2 * F) + 0.00454 * E * qSin(Mp - M)
             + 0.00204 * E * E * qSin(2 * M) - 0.00180 * qSin(Mp - 2 * F)
             - 0.00070 * qSin(Mp + 2 * F) - 0.00040 * qSin(3 * Mp)
             - 0.00034 * E * qSin(2 * Mp - M) + 0.00032 * E * qSin(M + 2 * F)
             + 0.00032 * E * qSin(M - 2 * F) - 0.00028 * E * E * qSin(Mp + 2 * M)
             + 0.00027 * E * qSin(2 * Mp + M) - 0.00017 * qSin(Om);
        const double W = 0.00306 - 0.00038 * E * qCos(M) + 0.00026 * qCos(Mp)
                       - 0.00002 * qCos(Mp - M) + 0.00002 * qCos(Mp + M)
                       + 0.00002 * qCos(2 * F);
        jde += (phase == 0.25) ? W : -W;
    }
    return jde;
}

QDateTime jdToUtc(double jd) {
    return QDateTime::fromMSecsSinceEpoch(
        qint64((jd - 2440587.5) * 86400.0 * 1000.0), Qt::UTC);
}

} // namespace

void WeatherBackend::buildAlmanac(const QJsonObject &daily) {
    QVariantMap almanac;
    const bool h12 = useTwelveHour();
    const QString timeFmt = h12 ? QStringLiteral("h:mm AP") : QStringLiteral("HH:mm");

    // Sunrise/sunset for the first two days. timezone=auto means these are
    // already local to the forecast location.
    const QJsonArray times   = daily["time"].toArray();
    const QJsonArray sunrise = daily["sunrise"].toArray();
    const QJsonArray sunset  = daily["sunset"].toArray();
    QVariantList days;
    for (int i = 0; i < qMin(2, times.size()); ++i) {
        const QDate date = QDate::fromString(times.at(i).toString(), Qt::ISODate);
        const QDateTime rise = QDateTime::fromString(sunrise.at(i).toString(), Qt::ISODate);
        const QDateTime set  = QDateTime::fromString(sunset.at(i).toString(),  Qt::ISODate);
        days.append(QVariantMap{
            { "name", date.isValid()
                  ? QLocale::c().dayName(date.dayOfWeek(), QLocale::LongFormat).toUpper()
                  : QString() },
            { "sunrise", rise.isValid() ? QLocale::c().toString(rise, timeFmt).toUpper() : QString() },
            { "sunset",  set.isValid()  ? QLocale::c().toString(set,  timeFmt).toUpper() : QString() },
        });
    }
    almanac["days"] = days;

    // Next occurrence of each principal phase, in chronological order.
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const double jdNow = nowUtc.toMSecsSinceEpoch() / 86400000.0 + 2440587.5;
    const QDate today = nowUtc.date();
    const double kBase = std::floor((today.year() + today.dayOfYear() / 365.25 - 2000.0) * 12.3685);

    QList<QPair<double, QString>> found;
    const QList<QPair<double, QString>> wanted = {
        { 0.00, QStringLiteral("NEW")   }, { 0.25, QStringLiteral("FIRST") },
        { 0.50, QStringLiteral("FULL")  }, { 0.75, QStringLiteral("LAST")  },
    };
    for (const auto &w : wanted) {
        for (int dk = -2; dk <= 3; ++dk) {
            const double jde = phaseJde(kBase + dk, w.first);
            if (jde > jdNow) { found.append({ jde, w.second }); break; }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
                  return a.first < b.first;
              });

    QVariantList moons;
    for (const auto &f : found) {
        // Render in the forecast location's timezone, not UTC. A phase at
        // 02:23 UTC is the previous evening in New York — getting this wrong
        // shifts the printed date by a day for roughly a third of all phases.
        const QDate local = jdToUtc(f.first).addSecs(m_utcOffset).date();
        moons.append(QVariantMap{
            { "name", f.second },
            { "date", QLocale::c().toString(local, QStringLiteral("MMM d")).toUpper() },
        });
    }
    almanac["moons"] = moons;

    m_almanac = almanac;
}

bool WeatherBackend::useTwelveHour() const {
    return moduleConfig()["hours_format"].toString(QStringLiteral("24-hour"))
               .startsWith(QLatin1String("12"));
}
