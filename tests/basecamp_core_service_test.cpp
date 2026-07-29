// srcdeps: BasecampCoreService.cpp

#include <QtTest>
#include <tuple>
#include <utility>
#include <vector>

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
    void scopedLifecycleRelayPreservesAddressAndDeduplicates();
    void unloadingOneInstanceReleasesOnlyItsScopedLifecycleSubscription();
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
    operations.subscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&,
                                                 BasecampModuleInstanceEventCallback) {
        return true;
    };
    operations.unsubscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&) {};
    operations.clearModuleInstanceEventSubscriptions = [] {};

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
    operations.subscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&,
                                                 BasecampModuleInstanceEventCallback) {
        return true;
    };
    operations.unsubscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&) {};
    operations.clearModuleInstanceEventSubscriptions = [] {};

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
    operations.subscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&,
                                                 BasecampModuleInstanceEventCallback) {
        return true;
    };
    operations.unsubscribeModuleInstanceEvent = [](const QString&, const QString&, const QString&) {};
    operations.clearModuleInstanceEventSubscriptions = [] {};
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

void BasecampCoreServiceTest::scopedLifecycleRelayPreservesAddressAndDeduplicates()
{
    struct Subscription {
        QString moduleName;
        QString instanceId;
        QString eventName;
        BasecampModuleInstanceEventCallback callback;
    };

    std::vector<Subscription> subscriptions;
    std::vector<std::tuple<QString, QString, QString>> unsubscribed;
    BasecampCoreRuntimeOperations operations;
    operations.loadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.unloadModuleInstance = [](const QString&, const QString&) { return true; };
    operations.isModuleInstanceLoaded = [](const QString&, const QString&) { return true; };
    operations.moduleInstancesInfo = [] { return QStringLiteral("[]"); };
    operations.getModuleInstanceMethods = [](const QString&, const QString&) {
        return QStringLiteral("[]");
    };
    operations.getModuleInstanceEvents = [](const QString&, const QString&) {
        return QStringLiteral("[ {\"name\": \"nodeChanged\", \"type\": \"event\"} ]");
    };
    operations.callModuleInstanceMethod = [](const QString&, const QString&, const QString&,
                                             const QString&) {
        return QStringLiteral("{\"result\":null}");
    };
    operations.subscribeModuleInstanceEvent = [&subscriptions](
        const QString& moduleName,
        const QString& instanceId,
        const QString& eventName,
        BasecampModuleInstanceEventCallback callback) {
        subscriptions.push_back(Subscription { moduleName, instanceId, eventName, std::move(callback) });
        return true;
    };
    operations.unsubscribeModuleInstanceEvent = [&unsubscribed](const QString& moduleName,
                                                                 const QString& instanceId,
                                                                 const QString& eventName) {
        unsubscribed.emplace_back(moduleName, instanceId, eventName);
    };
    operations.clearModuleInstanceEventSubscriptions = [] {};

    BasecampCoreService service(std::move(operations));
    const nlohmann::json address = nlohmann::json::array({
        "lez_indexer_module", "testnet:0101010101010101", "nodeChanged",
    });
    const nlohmann::json ingressUnavailable = service.callMethodStd(
        "subscribeModuleInstanceEvent", address);
    QCOMPARE(ingressUnavailable.at("code").get<std::string>(),
             std::string("EVENT_INGRESS_UNAVAILABLE"));
    QCOMPARE(subscriptions.size(), std::size_t(0));

    QString relayedEvent;
    QVariantList relayedArgs;
    int relayCount = 0;
    service.setEventListener([&](const QString& eventName, const QVariantList& args) {
        relayedEvent = eventName;
        relayedArgs = args;
        ++relayCount;
    });

    const QJsonArray methods = service.getMethods();
    bool hasRelayEvent = false;
    for (const QJsonValue& method : methods) {
        const QJsonObject metadata = method.toObject();
        if (metadata.value(QStringLiteral("name")).toString()
                == QStringLiteral("moduleInstanceEvent")) {
            QCOMPARE(metadata.value(QStringLiteral("type")).toString(), QStringLiteral("event"));
            hasRelayEvent = true;
        }
    }
    QVERIFY(hasRelayEvent);

    const nlohmann::json first = service.callMethodStd("subscribeModuleInstanceEvent", address);
    QCOMPARE(first.at("status").get<std::string>(), std::string("ok"));
    QVERIFY(!first.at("already_subscribed").get<bool>());
    QCOMPARE(subscriptions.size(), std::size_t(1));

    const nlohmann::json duplicate = service.callMethodStd("subscribeModuleInstanceEvent", address);
    QCOMPARE(duplicate.at("status").get<std::string>(), std::string("ok"));
    QVERIFY(duplicate.at("already_subscribed").get<bool>());
    QCOMPARE(subscriptions.size(), std::size_t(1));

    subscriptions.at(0).callback(QStringLiteral("lez_indexer_module"),
                                 QStringLiteral("testnet:0101010101010101"),
                                 QStringLiteral("nodeChanged"),
                                 QVariantList { QStringLiteral("running") });
    QCOMPARE(relayCount, 1);
    QCOMPARE(relayedEvent, QStringLiteral("moduleInstanceEvent"));
    QCOMPARE(relayedArgs.size(), 1);
    const QVariantMap envelope = relayedArgs.at(0).toMap();
    QCOMPARE(envelope.value(QStringLiteral("schema")).toString(),
             QStringLiteral("logos.basecamp_host.module_event"));
    QCOMPARE(envelope.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(envelope.value(QStringLiteral("module_name")).toString(),
             QStringLiteral("lez_indexer_module"));
    QCOMPARE(envelope.value(QStringLiteral("instance_id")).toString(),
             QStringLiteral("testnet:0101010101010101"));
    QCOMPARE(envelope.value(QStringLiteral("event_name")).toString(), QStringLiteral("nodeChanged"));
    QCOMPARE(envelope.value(QStringLiteral("args")).toList(),
             QVariantList { QStringLiteral("running") });

    subscriptions.at(0).callback(QStringLiteral("lez_indexer_module"),
                                 QStringLiteral("testnet:other"),
                                 QStringLiteral("nodeChanged"), QVariantList());
    QCOMPARE(relayCount, 1);

    const nlohmann::json unsupported = service.callMethodStd(
        "subscribeModuleInstanceEvent",
        nlohmann::json::array({"lez_indexer_module", "testnet:0101010101010101", "newBlock"}));
    QCOMPARE(unsupported.at("code").get<std::string>(), std::string("EVENT_NOT_SUPPORTED"));

    const nlohmann::json removed = service.callMethodStd("unsubscribeModuleInstanceEvent", address);
    QCOMPARE(removed.at("status").get<std::string>(), std::string("ok"));
    QVERIFY(removed.at("removed").get<bool>());
    QCOMPARE(unsubscribed.size(), std::size_t(1));
    QCOMPARE(std::get<0>(unsubscribed.at(0)), QStringLiteral("lez_indexer_module"));
    QCOMPARE(std::get<1>(unsubscribed.at(0)), QStringLiteral("testnet:0101010101010101"));
    QCOMPARE(std::get<2>(unsubscribed.at(0)), QStringLiteral("nodeChanged"));

    subscriptions.at(0).callback(QStringLiteral("lez_indexer_module"),
                                 QStringLiteral("testnet:0101010101010101"),
                                 QStringLiteral("nodeChanged"), QVariantList());
    QCOMPARE(relayCount, 1);
}

void BasecampCoreServiceTest::unloadingOneInstanceReleasesOnlyItsScopedLifecycleSubscription()
{
    struct Subscription {
        QString moduleName;
        QString instanceId;
        QString eventName;
        BasecampModuleInstanceEventCallback callback;
    };

    std::vector<Subscription> subscriptions;
    std::vector<std::tuple<QString, QString, QString>> unsubscribed;
    int clearCalls = 0;
    int relayCount = 0;
    {
        BasecampCoreRuntimeOperations operations;
        operations.loadModuleInstance = [](const QString&, const QString&) { return true; };
        operations.unloadModuleInstance = [](const QString&, const QString&) { return true; };
        operations.isModuleInstanceLoaded = [](const QString&, const QString&) { return true; };
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
        operations.subscribeModuleInstanceEvent = [&subscriptions](
            const QString& moduleName,
            const QString& instanceId,
            const QString& eventName,
            BasecampModuleInstanceEventCallback callback) {
            subscriptions.push_back(Subscription { moduleName, instanceId, eventName, std::move(callback) });
            return true;
        };
        operations.unsubscribeModuleInstanceEvent = [&unsubscribed](const QString& moduleName,
                                                                     const QString& instanceId,
                                                                     const QString& eventName) {
            unsubscribed.emplace_back(moduleName, instanceId, eventName);
        };
        operations.clearModuleInstanceEventSubscriptions = [&clearCalls] { ++clearCalls; };

        BasecampCoreService service(std::move(operations));
        service.setEventListener([&relayCount](const QString&, const QVariantList&) { ++relayCount; });
        const nlohmann::json alpha = nlohmann::json::array({
            "lez_indexer_module", "testnet:0101010101010101", "nodeChanged",
        });
        const nlohmann::json beta = nlohmann::json::array({
            "lez_indexer_module", "testnet:8888888888888888", "nodeChanged",
        });
        QCOMPARE(service.callMethodStd("subscribeModuleInstanceEvent", alpha).at("status").get<std::string>(),
                 std::string("ok"));
        QCOMPARE(service.callMethodStd("subscribeModuleInstanceEvent", beta).at("status").get<std::string>(),
                 std::string("ok"));
        QCOMPARE(subscriptions.size(), std::size_t(2));

        const nlohmann::json unloaded = service.callMethodStd(
            "unloadModuleInstance", nlohmann::json::array({"lez_indexer_module", "testnet:0101010101010101"}));
        QCOMPARE(unloaded.at("status").get<std::string>(), std::string("ok"));
        QCOMPARE(unsubscribed.size(), std::size_t(1));
        QCOMPARE(std::get<1>(unsubscribed.at(0)), QStringLiteral("testnet:0101010101010101"));

        subscriptions.at(0).callback(QStringLiteral("lez_indexer_module"),
                                     QStringLiteral("testnet:0101010101010101"),
                                     QStringLiteral("nodeChanged"), QVariantList());
        subscriptions.at(1).callback(QStringLiteral("lez_indexer_module"),
                                     QStringLiteral("testnet:8888888888888888"),
                                     QStringLiteral("nodeChanged"), QVariantList());
        QCOMPARE(relayCount, 1);
    }
    QCOMPARE(clearCalls, 1);
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
