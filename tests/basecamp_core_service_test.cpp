// srcdeps: BasecampCoreService.cpp

#include <QtTest>
#include <utility>

#include "BasecampCoreService.h"
#include "logos_api.h"
#include "token_manager.h"

class BasecampCoreServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void capabilitiesDescribeScopedDirectRouting();
    void explicitAddressOperationsNeverCollapseTheInstanceId();
    void malformedCallsReturnStructuredErrors();
    void capabilityIssuedTokensAuthorizeTheControlService();
};

void BasecampCoreServiceTest::capabilitiesDescribeScopedDirectRouting()
{
    BasecampCoreRuntimeOperations operations;
    operations.loadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.unloadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.isModuleInstanceLoaded = [](const QString&, const QString&) { return false; };
    operations.moduleInstancesInfo = [] { return QStringLiteral("[]"); };
    operations.getModuleInstanceMethods = [](const QString&, const QString&) {
        return QStringLiteral("[]");
    };
    operations.getModuleInstanceEvents = [](const QString&, const QString&) {
        return QStringLiteral("[]");
    };
    operations.callModuleInstanceMethod = [](const QString&, const QString&, const QString&,
                                             const QString&) {
        return QStringLiteral("{\"result\":null}");
    };

    BasecampCoreService service(std::move(operations));
    const nlohmann::json capabilities =
        service.callMethodStd("getHostCapabilities", nlohmann::json::array());

    QCOMPARE(QString::fromStdString(capabilities.at("schema").get<std::string>()),
             QStringLiteral("logos.basecamp_host"));
    QCOMPARE(capabilities.at("version").get<int>(), 1);
    QVERIFY(capabilities.at("scoped_module_instances").get<bool>());
    QVERIFY(capabilities.at("direct_scoped_clients").get<bool>());
    QVERIFY(capabilities.at("direct_scoped_events").get<bool>());
}

void BasecampCoreServiceTest::explicitAddressOperationsNeverCollapseTheInstanceId()
{
    QString loadedModule;
    QString loadedInstance;
    QString calledModule;
    QString calledInstance;
    QString calledMethod;
    QString calledArgs;
    BasecampCoreRuntimeOperations operations;
    operations.loadModuleInstance = [&](const QString& module, const QString& instance) {
        loadedModule = module;
        loadedInstance = instance;
        return true;
    };
    operations.unloadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.isModuleInstanceLoaded = [](const QString&, const QString&) { return true; };
    operations.moduleInstancesInfo = [] {
        return QStringLiteral("[{\"module_name\":\"lez_indexer_module\",\"instance_id\":\"zone-a\"}]");
    };
    operations.getModuleInstanceMethods = [](const QString&, const QString&) {
        return QStringLiteral("[{\"name\":\"nodeStatus\"}]");
    };
    operations.getModuleInstanceEvents = [](const QString&, const QString&) {
        return QStringLiteral("[{\"name\":\"nodeChanged\"}]");
    };
    operations.callModuleInstanceMethod = [&](const QString& module, const QString& instance,
                                              const QString& method, const QString& args) {
        calledModule = module;
        calledInstance = instance;
        calledMethod = method;
        calledArgs = args;
        return QStringLiteral("{\"result\":{\"state\":\"running\"}}");
    };

    BasecampCoreService service(std::move(operations));
    const nlohmann::json address = nlohmann::json::array({
        "lez_indexer_module", "testnet:0101010101010101",
    });
    const nlohmann::json load = service.callMethodStd("loadModuleInstance", address);
    QCOMPARE(QString::fromStdString(load.at("status").get<std::string>()), QStringLiteral("ok"));
    QCOMPARE(loadedModule, QStringLiteral("lez_indexer_module"));
    QCOMPARE(loadedInstance, QStringLiteral("testnet:0101010101010101"));

    const nlohmann::json interface = service.callMethodStd("getModuleInstanceInterface", address);
    QCOMPARE(interface.at("methods").at(0).at("name").get<std::string>(), std::string("nodeStatus"));
    QCOMPARE(interface.at("events").at(0).at("name").get<std::string>(), std::string("nodeChanged"));

    const nlohmann::json call = service.callMethodStd("callModuleInstanceMethod", nlohmann::json::array({
        "lez_indexer_module", "testnet:0101010101010101", "nodeStatus", nlohmann::json::array(),
    }));
    QCOMPARE(calledModule, QStringLiteral("lez_indexer_module"));
    QCOMPARE(calledInstance, QStringLiteral("testnet:0101010101010101"));
    QCOMPARE(calledMethod, QStringLiteral("nodeStatus"));
    QCOMPARE(calledArgs, QStringLiteral("[]"));
    QCOMPARE(call.at("result").at("state").get<std::string>(), std::string("running"));
}

void BasecampCoreServiceTest::malformedCallsReturnStructuredErrors()
{
    BasecampCoreRuntimeOperations operations;
    operations.loadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.unloadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.isModuleInstanceLoaded = [](const QString&, const QString&) { return false; };
    operations.moduleInstancesInfo = [] { return QStringLiteral("[]"); };
    operations.getModuleInstanceMethods = [](const QString&, const QString&) {
        return QStringLiteral("[]");
    };
    operations.getModuleInstanceEvents = [](const QString&, const QString&) {
        return QStringLiteral("[]");
    };
    operations.callModuleInstanceMethod = [](const QString&, const QString&, const QString&,
                                             const QString&) {
        return QStringLiteral("{\"result\":null}");
    };
    BasecampCoreService service(std::move(operations));
    const nlohmann::json unavailable = service.callMethodStd(
        "loadModuleInstance", nlohmann::json::array({"lez_indexer_module", ""}));
    QCOMPARE(unavailable.at("code").get<std::string>(), std::string("INVALID_ARGS"));

    const nlohmann::json malformed = service.callMethodStd(
        "callModuleInstanceMethod", nlohmann::json::array({"lez_indexer_module", "zone-a", "nodeStatus"}));
    QCOMPARE(malformed.at("code").get<std::string>(), std::string("INVALID_ARGS"));

    const nlohmann::json capabilitiesWithArgs = service.callMethodStd(
        "getHostCapabilities", nlohmann::json::array({"unexpected"}));
    QCOMPARE(capabilitiesWithArgs.at("code").get<std::string>(), std::string("INVALID_ARGS"));
}

void BasecampCoreServiceTest::capabilityIssuedTokensAuthorizeTheControlService()
{
    TokenManager::instance().clearAllTokens();
    LogosAPI api(QStringLiteral("basecamp_core_service_test"));
    BasecampCoreRuntimeOperations operations;
    BasecampCoreService service(std::move(operations));
    service.init(&api);

    QVERIFY(service.informModuleToken(QStringLiteral("logos_inspector"),
                                      QStringLiteral("issued-control-token")));
    QCOMPARE(TokenManager::instance().getToken(QStringLiteral("logos_inspector")),
             QStringLiteral("issued-control-token"));
    TokenManager::instance().clearAllTokens();
}

QTEST_APPLESS_MAIN(BasecampCoreServiceTest)

#include "basecamp_core_service_test.moc"
