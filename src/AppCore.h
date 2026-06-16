#pragma once
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QJsonObject>
#include <QMap>
#include <QCoreApplication>

class QQmlContext;

struct ModuleEntry {
    QString id;
    QString name;
    QString folder;      // subdirectory under modules/
    QString entryQml;    // relative to module folder, e.g. "views/Root.qml"
    QString iconRel;     // relative to module folder, e.g. "assets/images/logo.svg"
    QVariantList settings;
};

class AppCore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
public:
    explicit AppCore(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    QString appVersion() const { return QCoreApplication::applicationVersion(); }

    // True when launched by the autostart systemd service (which injects MP240_AUTOSTART=1).
    // Gates the quit overlay's "Exit to Terminal" option, which only makes sense on a
    // headless RPi running under the service. See scripts/install.sh and 240mp-stop.
    Q_INVOKABLE bool isAutostartSession() const {
        return qEnvironmentVariableIsSet("MP240_AUTOSTART");
    }

    Q_INVOKABLE void scan_for_modules();
    Q_INVOKABLE QVariant get_settings();
    Q_INVOKABLE QVariant get_setting(const QString &moduleId, const QString &key);
    Q_INVOKABLE void save_setting(const QString &moduleId, const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant get_module_info(const QString &moduleId);
    Q_INVOKABLE QVariant get_module_settings_schema(const QString &moduleId);
    Q_INVOKABLE void invoke_module_action(const QString &moduleId, const QString &slotName);
    Q_INVOKABLE QVariant get_installed_modules();
    Q_INVOKABLE QVariantMap getCustomColorScheme() const;
    Q_INVOKABLE QVariantList listDirectories(const QString &path);
    Q_INVOKABLE QString parentDirectory(const QString &path);
    Q_INVOKABLE QString homePath();
    Q_INVOKABLE QString get_module_auth_state(const QString &moduleId);

    // Registers a module backend: stores it for action routing, exposes it to QML under
    // contextProperty, and connects its optional signals/slots by introspection (only
    // those the backend actually declares). The module ID is stated once, here.
    void registerModule(const QString &moduleId, const QString &contextProperty,
                        QObject *backend, QQmlContext *ctx);

signals:
    void modulesLoaded(const QVariantList &modules);
    void appSettingChanged(const QString &key, const QString &value);
    void moduleSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);
    void dynamicOptionsReady(const QString &moduleId, const QString &key, const QVariant &options);
    void moduleAuthStateChanged(const QString &moduleId);

private slots:
    // Receive a backend's signal and re-emit it with the module ID prepended, recovering
    // the module ID via sender() reverse-lookup. Lets registerModule connect any backend
    // generically, with no per-module forwarding lambdas.
    void onBackendDynamicOptions(const QString &key, const QVariant &options);
    void onBackendAuthStateChanged();

private:
    QJsonObject loadConfig() const;
    void saveConfig(const QJsonObject &config) const;
    QString moduleIdForBackend(QObject *backend) const;

    QString m_appRoot;
    QString m_dataRoot;
    QList<ModuleEntry> m_modules;
    QMap<QString, QObject*> m_backends;
};
