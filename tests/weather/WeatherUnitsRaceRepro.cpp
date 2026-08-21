#include "modules/weather/WeatherBackend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <cstring>

namespace {

constexpr auto kWeatherModuleId = "com.240mp.weather";
constexpr auto kPrimaryLatitude = "51.5007";
constexpr auto kOtherLatitude = "48.8566";

class ControlledReply final : public QNetworkReply {
public:
    ControlledReply(QNetworkAccessManager::Operation operation,
                    const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent) {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void succeed(const QByteArray &body) {
        m_body = body;
        m_offset = 0;
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void fail(QNetworkReply::NetworkError error, const QString &message,
              int statusCode) {
        setError(error, message);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, statusCode);
        setFinished(true);
        emit finished();
    }

    void abort() override {
        if (!isFinished())
            fail(QNetworkReply::OperationCanceledError,
                 QStringLiteral("request aborted"), 0);
    }

    qint64 bytesAvailable() const override {
        return (m_body.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 available = m_body.size() - m_offset;
        if (available <= 0) return -1;
        const qint64 amount = qMin(maxSize, available);
        std::memcpy(data, m_body.constData() + m_offset,
                    static_cast<size_t>(amount));
        m_offset += amount;
        return amount;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class ControlledNetworkAccessManager final : public QNetworkAccessManager {
public:
    struct ForecastRequest {
        QString latitude;
        QString temperatureUnit;
        ControlledReply *reply = nullptr;
    };

    ControlledReply *takeForecast(const QString &latitude,
                                  const QString &temperatureUnit) {
        for (qsizetype i = 0; i < m_forecasts.size(); ++i) {
            const ForecastRequest request = m_forecasts.at(i);
            if (request.latitude == latitude
                && request.temperatureUnit == temperatureUnit) {
                m_forecasts.removeAt(i);
                return request.reply;
            }
        }
        return nullptr;
    }

    int pendingForecastCount() const { return m_forecasts.size(); }
    QStringList unexpectedUrls() const { return m_unexpectedUrls; }

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override {
        Q_UNUSED(outgoingData)
        auto *reply = new ControlledReply(operation, request, this);
        const QUrl url = request.url();

        if (url.host() == QLatin1String("api.open-meteo.com")
            && url.path() == QLatin1String("/v1/forecast")) {
            const QUrlQuery query(url);
            m_forecasts.append({
                query.queryItemValue(QStringLiteral("latitude")),
                query.queryItemValue(QStringLiteral("temperature_unit")),
                reply,
            });
            return reply;
        }

        if (url.host() == QLatin1String("api.weather.gov")) {
            QTimer::singleShot(0, reply, [reply]() {
                reply->fail(QNetworkReply::ContentNotFoundError,
                            QStringLiteral("fake non-US NWS response"), 404);
            });
            return reply;
        }

        m_unexpectedUrls.append(url.toString());
        QTimer::singleShot(0, reply, [reply]() {
            reply->fail(QNetworkReply::ProtocolInvalidOperationError,
                        QStringLiteral("unexpected network request"), 400);
        });
        return reply;
    }

private:
    QList<ForecastRequest> m_forecasts;
    QStringList m_unexpectedUrls;
};

QByteArray forecastResponse(int currentTemperature, int low, int high) {
    const QJsonArray dates{
        QStringLiteral("2026-08-21"),
        QStringLiteral("2026-08-22"),
        QStringLiteral("2026-08-23"),
    };
    const QJsonArray lows{ low, low + 1, low + 2 };
    const QJsonArray highs{ high, high + 1, high + 2 };
    const QJsonArray codes{ 0, 1, 2 };
    const QJsonArray sunrises{
        QStringLiteral("2026-08-21T06:00"),
        QStringLiteral("2026-08-22T06:01"),
        QStringLiteral("2026-08-23T06:02"),
    };
    const QJsonArray sunsets{
        QStringLiteral("2026-08-21T20:00"),
        QStringLiteral("2026-08-22T19:59"),
        QStringLiteral("2026-08-23T19:58"),
    };

    const QJsonObject current{
        { QStringLiteral("temperature_2m"), currentTemperature },
        { QStringLiteral("relative_humidity_2m"), 50 },
        { QStringLiteral("dew_point_2m"), currentTemperature - 2 },
        { QStringLiteral("pressure_msl"), 1000.0 },
        { QStringLiteral("wind_speed_10m"), 8.0 },
        { QStringLiteral("wind_direction_10m"), 90.0 },
        { QStringLiteral("visibility"), 10000.0 },
        { QStringLiteral("weather_code"), 0 },
        { QStringLiteral("is_day"), 1 },
    };
    const QJsonObject daily{
        { QStringLiteral("time"), dates },
        { QStringLiteral("temperature_2m_min"), lows },
        { QStringLiteral("temperature_2m_max"), highs },
        { QStringLiteral("weather_code"), codes },
        { QStringLiteral("sunrise"), sunrises },
        { QStringLiteral("sunset"), sunsets },
    };
    return QJsonDocument(QJsonObject{
        { QStringLiteral("utc_offset_seconds"), 0 },
        { QStringLiteral("current"), current },
        { QStringLiteral("daily"), daily },
    }).toJson(QJsonDocument::Compact);
}

QByteArray otherLocationResponse(int temperature) {
    return QJsonDocument(QJsonObject{
        { QStringLiteral("current"), QJsonObject{
              { QStringLiteral("temperature_2m"), temperature },
              { QStringLiteral("weather_code"), 0 },
              { QStringLiteral("wind_speed_10m"), 8.0 },
              { QStringLiteral("wind_direction_10m"), 90.0 },
              { QStringLiteral("is_day"), 1 },
          } },
    }).toJson(QJsonDocument::Compact);
}

void writeConfig(const QString &dataRoot, const QString &units) {
    const QJsonObject weather{
        { QStringLiteral("units"), units },
        { QStringLiteral("hours_format"), QStringLiteral("24-hour") },
        { QStringLiteral("music"), false },
    };
    const QJsonObject modules{
        { QString::fromLatin1(kWeatherModuleId), weather },
    };
    QFile file(dataRoot + QStringLiteral("/config.json"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(file.errorString()));
    QCOMPARE(file.write(QJsonDocument(QJsonObject{
        { QStringLiteral("modules"), modules },
    }).toJson()), qint64(file.size()));
}

void writeLocations(const QString &dataRoot, bool includeOther) {
    QFile file(dataRoot + QStringLiteral("/weather_location.txt"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
             qPrintable(file.errorString()));
    QByteArray contents("51.5007, -0.1246, PRIMARY\n");
    if (includeOther)
        contents += "48.8566, 2.3522, OTHER\n";
    QCOMPARE(file.write(contents), qint64(contents.size()));
}

QString configuredUnit(const QString &dataRoot) {
    QFile file(dataRoot + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("<unreadable>");
    return QJsonDocument::fromJson(file.readAll()).object()
        [QStringLiteral("modules")].toObject()
        [QString::fromLatin1(kWeatherModuleId)].toObject()
        [QStringLiteral("units")].toString(QStringLiteral("<missing>"));
}

QString compactJson(const QVariant &value) {
    return QString::fromUtf8(
        QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QString stateDump(const WeatherBackend &backend, const QString &dataRoot) {
    return QStringLiteral(
               "configuredUnit=%1\n"
               "tempUnitLabel=%2\n"
               "current=%3\n"
               "forecast=%4\n"
               "otherLocations=%5")
        .arg(configuredUnit(dataRoot), backend.tempUnitLabel(),
             compactJson(backend.current()), compactJson(backend.forecast()),
             compactJson(backend.otherLocations()));
}

QVariantMap firstForecast(const WeatherBackend &backend) {
    return backend.forecast().isEmpty()
        ? QVariantMap{}
        : backend.forecast().first().toMap();
}

QVariantMap firstOther(const WeatherBackend &backend) {
    return backend.otherLocations().isEmpty()
        ? QVariantMap{}
        : backend.otherLocations().first().toMap();
}

} // namespace

class WeatherUnitsRaceRepro final : public QObject {
    Q_OBJECT

private slots:
    void primaryLatestRequestWins() {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        writeConfig(dataRoot.path(), QStringLiteral("Metric"));
        writeLocations(dataRoot.path(), false);

        ControlledNetworkAccessManager network;
        WeatherBackend backend(dataRoot.path(), dataRoot.path(), nullptr, &network);

        backend.start();
        QCOMPARE(network.pendingForecastCount(), 1);

        writeConfig(dataRoot.path(), QStringLiteral("US"));
        backend.onSettingChanged(QString::fromLatin1(kWeatherModuleId),
                                 QStringLiteral("units"), QStringLiteral("US"));
        QCOMPARE(network.pendingForecastCount(), 2);

        auto *requestB = network.takeForecast(
            QString::fromLatin1(kPrimaryLatitude), QStringLiteral("fahrenheit"));
        QVERIFY(requestB);
        requestB->succeed(forecastResponse(50, 40, 60));
        QCOMPARE(backend.current().value(QStringLiteral("temperature")).toString(),
                 QStringLiteral("50°"));
        QCOMPARE(firstForecast(backend).value(QStringLiteral("lo")).toString(),
                 QStringLiteral("40"));
        QCOMPARE(firstForecast(backend).value(QStringLiteral("hi")).toString(),
                 QStringLiteral("60"));

        auto *requestA = network.takeForecast(
            QString::fromLatin1(kPrimaryLatitude), QStringLiteral("celsius"));
        QVERIFY(requestA);
        requestA->succeed(forecastResponse(10, 5, 15));

        const QVariantMap day = firstForecast(backend);
        const bool latestRequestWon =
            backend.current().value(QStringLiteral("temperature")).toString()
                == QStringLiteral("50°")
            && day.value(QStringLiteral("lo")).toString() == QStringLiteral("40")
            && day.value(QStringLiteral("hi")).toString() == QStringLiteral("60")
            && backend.tempUnitLabel() == QStringLiteral("°F")
            && configuredUnit(dataRoot.path()) == QStringLiteral("US");
        QVERIFY(network.unexpectedUrls().isEmpty());
        QVERIFY2(latestRequestWon, qPrintable(stateDump(backend, dataRoot.path())));
    }

    void otherLocationsLatestRequestWins() {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        writeConfig(dataRoot.path(), QStringLiteral("Metric"));
        writeLocations(dataRoot.path(), true);

        ControlledNetworkAccessManager network;
        WeatherBackend backend(dataRoot.path(), dataRoot.path(), nullptr, &network);

        backend.start();
        QCOMPARE(network.pendingForecastCount(), 2);

        writeConfig(dataRoot.path(), QStringLiteral("US"));
        backend.onSettingChanged(QString::fromLatin1(kWeatherModuleId),
                                 QStringLiteral("units"), QStringLiteral("US"));
        QCOMPARE(network.pendingForecastCount(), 4);

        // Keep the primary family on the new US snapshot so this case isolates
        // the independently racing Other Locations request family.
        auto *primaryA = network.takeForecast(
            QString::fromLatin1(kPrimaryLatitude), QStringLiteral("celsius"));
        auto *primaryB = network.takeForecast(
            QString::fromLatin1(kPrimaryLatitude), QStringLiteral("fahrenheit"));
        QVERIFY(primaryA);
        QVERIFY(primaryB);
        primaryA->succeed(forecastResponse(10, 5, 15));
        primaryB->succeed(forecastResponse(50, 40, 60));

        auto *requestB = network.takeForecast(
            QString::fromLatin1(kOtherLatitude), QStringLiteral("fahrenheit"));
        QVERIFY(requestB);
        requestB->succeed(otherLocationResponse(50));
        QCOMPARE(firstOther(backend).value(QStringLiteral("temp")).toString(),
                 QStringLiteral("50"));
        QCOMPARE(backend.tempUnitLabel(), QStringLiteral("°F"));

        auto *requestA = network.takeForecast(
            QString::fromLatin1(kOtherLatitude), QStringLiteral("celsius"));
        QVERIFY(requestA);
        requestA->succeed(otherLocationResponse(10));

        const QVariantMap other = firstOther(backend);
        const QVariantMap day = firstForecast(backend);
        const bool latestRequestWon =
            backend.current().value(QStringLiteral("temperature")).toString()
                == QStringLiteral("50°")
            && day.value(QStringLiteral("lo")).toString() == QStringLiteral("40")
            && day.value(QStringLiteral("hi")).toString() == QStringLiteral("60")
            && other.value(QStringLiteral("temp")).toString() == QStringLiteral("50")
            && backend.tempUnitLabel() == QStringLiteral("°F")
            && configuredUnit(dataRoot.path()) == QStringLiteral("US");
        QVERIFY(network.unexpectedUrls().isEmpty());
        QVERIFY2(latestRequestWon, qPrintable(stateDump(backend, dataRoot.path())));
    }
};

QTEST_GUILESS_MAIN(WeatherUnitsRaceRepro)

#include "WeatherUnitsRaceRepro.moc"
