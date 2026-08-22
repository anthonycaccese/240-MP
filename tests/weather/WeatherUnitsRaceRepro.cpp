#include "modules/weather/WeatherBackend.h"

#include <QFile>
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
    ControlledReply(const QUrl &url, QObject *parent)
        : QNetworkReply(parent) {
        setUrl(url);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void succeed(const QByteArray &body) {
        m_body = body;
        setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void fail(QNetworkReply::NetworkError error) {
        setError(error, QStringLiteral("fake network response"));
        setFinished(true);
        emit finished();
    }

    void abort() override {
        if (!isFinished())
            fail(QNetworkReply::OperationCanceledError);
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
        Q_UNUSED(operation)
        auto *reply = new ControlledReply(request.url(), this);
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

        if (url.host() != QLatin1String("api.weather.gov"))
            m_unexpectedUrls.append(url.toString());
        QTimer::singleShot(0, reply, [reply]() {
            reply->fail(QNetworkReply::ContentNotFoundError);
        });
        return reply;
    }

private:
    struct ForecastRequest {
        QString latitude;
        QString temperatureUnit;
        ControlledReply *reply = nullptr;
    };

    QList<ForecastRequest> m_forecasts;
    QStringList m_unexpectedUrls;
};

QByteArray forecastResponse(int currentTemperature, int low, int high) {
    return QStringLiteral(R"({
        "utc_offset_seconds": 0,
        "current": {"temperature_2m": %1, "weather_code": 0, "is_day": 1},
        "daily": {
            "time": ["2026-08-21"],
            "temperature_2m_min": [%2],
            "temperature_2m_max": [%3],
            "weather_code": [0],
            "sunrise": ["2026-08-21T06:00"],
            "sunset": ["2026-08-21T20:00"]
        }
    })").arg(currentTemperature).arg(low).arg(high).toUtf8();
}

QByteArray otherLocationResponse(int temperature) {
    return QStringLiteral(R"({
        "current": {"temperature_2m": %1, "weather_code": 0, "is_day": 1}
    })").arg(temperature).toUtf8();
}

void writeConfig(const QString &dataRoot, const QString &units) {
    const QByteArray contents = QStringLiteral(R"({
        "modules": {"com.240mp.weather": {
            "units": "%1", "hours_format": "24-hour", "music": false
        }}
    })").arg(units).toUtf8();
    QFile file(dataRoot + QStringLiteral("/config.json"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), qint64(contents.size()));
}

void writeLocations(const QString &dataRoot, bool includeOther) {
    QByteArray contents("51.5007, -0.1246, PRIMARY\n");
    if (includeOther) contents += "48.8566, 2.3522, OTHER\n";

    QFile file(dataRoot + QStringLiteral("/weather_location.txt"));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
             qPrintable(file.errorString()));
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

QVariantMap first(const QVariantList &values) {
    return values.isEmpty() ? QVariantMap{} : values.first().toMap();
}

void verifyFinalUsState(const WeatherBackend &backend, const QString &dataRoot,
                        bool checkOtherLocation) {
    const QVariantMap day = first(backend.forecast());
    bool latestRequestWon =
        backend.current().value(QStringLiteral("temperature")).toString()
            == QStringLiteral("50°")
        && day.value(QStringLiteral("lo")).toString() == QStringLiteral("40")
        && day.value(QStringLiteral("hi")).toString() == QStringLiteral("60")
        && backend.tempUnitLabel() == QStringLiteral("°F")
        && configuredUnit(dataRoot) == QStringLiteral("US");
    if (checkOtherLocation) {
        latestRequestWon = latestRequestWon
            && first(backend.otherLocations()).value(QStringLiteral("temp")).toString()
                == QStringLiteral("50");
    }
    QVERIFY2(latestRequestWon, qPrintable(stateDump(backend, dataRoot)));
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
        QCOMPARE(first(backend.forecast()).value(QStringLiteral("lo")).toString(),
                 QStringLiteral("40"));
        QCOMPARE(first(backend.forecast()).value(QStringLiteral("hi")).toString(),
                 QStringLiteral("60"));

        auto *requestA = network.takeForecast(
            QString::fromLatin1(kPrimaryLatitude), QStringLiteral("celsius"));
        QVERIFY(requestA);
        requestA->succeed(forecastResponse(10, 5, 15));

        QVERIFY(network.unexpectedUrls().isEmpty());
        verifyFinalUsState(backend, dataRoot.path(), false);
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

        // Keep primary Weather on its new US snapshot so this case isolates
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
        QCOMPARE(first(backend.otherLocations()).value(QStringLiteral("temp")).toString(),
                 QStringLiteral("50"));
        QCOMPARE(backend.tempUnitLabel(), QStringLiteral("°F"));

        auto *requestA = network.takeForecast(
            QString::fromLatin1(kOtherLatitude), QStringLiteral("celsius"));
        QVERIFY(requestA);
        requestA->succeed(otherLocationResponse(10));

        QVERIFY(network.unexpectedUrls().isEmpty());
        verifyFinalUsState(backend, dataRoot.path(), true);
    }
};

QTEST_GUILESS_MAIN(WeatherUnitsRaceRepro)

#include "WeatherUnitsRaceRepro.moc"
