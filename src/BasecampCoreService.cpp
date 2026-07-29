#include "BasecampCoreService.h"

#include <cstddef>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "logos_json_convert.h"

namespace {

constexpr std::string_view kScopedLifecycleEvent = "nodeChanged";
constexpr std::string_view kScopedRelayEvent = "moduleInstanceEvent";
constexpr std::size_t kMaxScopedEventPayloadBytes = 64U * 1024U;

struct ScopedEventAddress {
    QString moduleName;
    QString instanceId;
    QString eventName;

    bool operator<(const ScopedEventAddress& other) const noexcept
    {
        if (moduleName != other.moduleName) return moduleName < other.moduleName;
        if (instanceId != other.instanceId) return instanceId < other.instanceId;
        return eventName < other.eventName;
    }
};

bool hasExactStringArguments(const nlohmann::json& args, std::size_t count)
{
    if (!args.is_array() || args.size() != count) {
        return false;
    }
    for (const nlohmann::json& value : args) {
        if (!value.is_string()) {
            return false;
        }
    }
    return true;
}

} // namespace

struct BasecampCoreService::RelayState {
    std::mutex mutex;
    UniversalEventCallback listener;
    std::set<ScopedEventAddress> subscriptions;
    bool closed = false;
};

BasecampCoreService::BasecampCoreService(BasecampCoreRuntimeOperations operations)
    : operations_(std::move(operations))
    , relayState_(std::make_shared<RelayState>())
{
}

BasecampCoreService::~BasecampCoreService()
{
    clearModuleInstanceEventSubscriptions();
}

QVariant BasecampCoreService::callMethod(const QString& methodName,
                                         const QVariantList& args)
{
    return callMethodStdBridge(methodName, args);
}

QJsonArray BasecampCoreService::getMethods()
{
    QJsonArray methods = getMethodsStdBridge();
    if (hasRuntimeOperations() && hasScopedEventOperations()) {
        methods.append(QJsonObject {
            { QStringLiteral("name"), QString::fromUtf8(kScopedRelayEvent.data(),
                                                           kScopedRelayEvent.size()) },
            { QStringLiteral("type"), QStringLiteral("event") },
        });
    }
    return methods;
}

QString BasecampCoreService::providerName() const
{
    return QStringLiteral("core_service");
}

QString BasecampCoreService::providerVersion() const
{
    return QStringLiteral("1.0.0");
}

nlohmann::json BasecampCoreService::error(const char* code,
                                          const std::string& message)
{
    return nlohmann::json{
        {"status", "error"},
        {"code", code},
        {"message", message},
    };
}

bool BasecampCoreService::validAddressSegment(const std::string& value)
{
    return !value.empty() && value.find('\0') == std::string::npos;
}

nlohmann::json BasecampCoreService::parseArrayResponse(const QString& response,
                                                        const char* operation)
{
    const nlohmann::json parsed = nlohmann::json::parse(
        response.toStdString(), nullptr, false);
    if (!parsed.is_array()) {
        return error("RUNTIME_RESPONSE_INVALID",
                     std::string(operation) + " returned invalid runtime JSON");
    }
    return parsed;
}

nlohmann::json BasecampCoreService::parseObjectResponse(const QString& response,
                                                         const char* operation)
{
    const nlohmann::json parsed = nlohmann::json::parse(
        response.toStdString(), nullptr, false);
    if (!parsed.is_object()) {
        return error("RUNTIME_RESPONSE_INVALID",
                     std::string(operation) + " returned invalid runtime JSON");
    }
    return parsed;
}

bool BasecampCoreService::hasRuntimeOperations() const
{
    return static_cast<bool>(operations_.loadModuleInstance)
        && static_cast<bool>(operations_.unloadModuleInstance)
        && static_cast<bool>(operations_.isModuleInstanceLoaded)
        && static_cast<bool>(operations_.moduleInstancesInfo)
        && static_cast<bool>(operations_.getModuleInstanceMethods)
        && static_cast<bool>(operations_.getModuleInstanceEvents)
        && static_cast<bool>(operations_.callModuleInstanceMethod);
}

bool BasecampCoreService::hasScopedEventOperations() const
{
    return static_cast<bool>(operations_.subscribeModuleInstanceEvent)
        && static_cast<bool>(operations_.unsubscribeModuleInstanceEvent)
        && static_cast<bool>(operations_.clearModuleInstanceEventSubscriptions);
}

nlohmann::json BasecampCoreService::callMethodStd(const std::string& methodName,
                                                   const nlohmann::json& args)
{
    if (methodName == "getHostCapabilities") {
        if (!args.is_array() || !args.empty()) {
            return error("INVALID_ARGS", "getHostCapabilities expects no arguments");
        }
        return nlohmann::json{
            {"schema", "logos.basecamp_host"},
            {"version", 1},
            {"scoped_module_instances", hasRuntimeOperations()},
            {"direct_scoped_clients", hasRuntimeOperations()},
            {"direct_scoped_events", hasRuntimeOperations() && hasScopedEventOperations()},
        };
    }

    if (!hasRuntimeOperations()) {
        return error("HOST_UNAVAILABLE", "Basecamp runtime service is unavailable");
    }

    if (methodName == "getModuleInstances") {
        if (!args.is_array() || !args.empty()) {
            return error("INVALID_ARGS", "getModuleInstances expects no arguments");
        }
        return parseArrayResponse(operations_.moduleInstancesInfo(), "getModuleInstances");
    }

    if (methodName == "loadModuleInstance" || methodName == "unloadModuleInstance"
        || methodName == "isModuleInstanceLoaded" || methodName == "getModuleInstanceInterface") {
        if (!hasExactStringArguments(args, 2)
            || !validAddressSegment(args[0].get<std::string>())
            || !validAddressSegment(args[1].get<std::string>())) {
            return error("INVALID_ARGS", "module_name and instance_id must be non-empty strings");
        }

        const QString moduleName = QString::fromStdString(args[0].get<std::string>());
        const QString instanceId = QString::fromStdString(args[1].get<std::string>());
        if (methodName == "loadModuleInstance") {
            if (!operations_.loadModuleInstance(moduleName, instanceId)) {
                return error("MODULE_INSTANCE_LOAD_FAILED",
                             "Basecamp could not load the requested module instance");
            }
            return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                                  {"instance_id", args[1]}};
        }
        if (methodName == "unloadModuleInstance") {
            if (!operations_.unloadModuleInstance(moduleName, instanceId)) {
                return error("MODULE_INSTANCE_UNLOAD_FAILED",
                             "Basecamp could not unload the requested module instance");
            }
            removeModuleInstanceEventSubscriptions(moduleName, instanceId);
            return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                                  {"instance_id", args[1]}};
        }
        if (methodName == "isModuleInstanceLoaded") {
            return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                                  {"instance_id", args[1]},
                                  {"loaded", operations_.isModuleInstanceLoaded(moduleName, instanceId)}};
        }

        const nlohmann::json methods = parseArrayResponse(
            operations_.getModuleInstanceMethods(moduleName, instanceId),
            "getModuleInstanceInterface methods");
        if (!methods.is_array()) {
            return methods;
        }
        const nlohmann::json events = parseArrayResponse(
            operations_.getModuleInstanceEvents(moduleName, instanceId),
            "getModuleInstanceInterface events");
        if (!events.is_array()) {
            return events;
        }
        return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                              {"instance_id", args[1]}, {"methods", methods},
                              {"events", events}};
    }

    if (methodName == "callModuleInstanceMethod") {
        if (!args.is_array() || args.size() != 4 || !args[0].is_string()
            || !args[1].is_string() || !args[2].is_string() || !args[3].is_array()
            || !validAddressSegment(args[0].get<std::string>())
            || !validAddressSegment(args[1].get<std::string>())
            || !validAddressSegment(args[2].get<std::string>())) {
            return error("INVALID_ARGS",
                         "callModuleInstanceMethod expects module_name, instance_id, method, and args");
        }
        const QString response = operations_.callModuleInstanceMethod(
            QString::fromStdString(args[0].get<std::string>()),
            QString::fromStdString(args[1].get<std::string>()),
            QString::fromStdString(args[2].get<std::string>()),
            QString::fromStdString(args[3].dump()));
        return parseObjectResponse(response, "callModuleInstanceMethod");
    }

    if (methodName == "subscribeModuleInstanceEvent") {
        return subscribeModuleInstanceEvent(args);
    }

    if (methodName == "unsubscribeModuleInstanceEvent") {
        return unsubscribeModuleInstanceEvent(args);
    }

    return error("METHOD_NOT_FOUND", "Unknown Basecamp runtime service method");
}

std::vector<LogosMethodMetadata> BasecampCoreService::getMethodsStd()
{
    const auto parameter = [](const char* name, const char* type) {
        return nlohmann::json{{"name", name}, {"type", type}};
    };
    const auto method = [&parameter](const char* name, nlohmann::json parameters,
                                     const char* returnType) {
        LogosMethodMetadata metadata;
        metadata.name = name;
        metadata.parameters = std::move(parameters);
        metadata.returnType = returnType;
        return metadata;
    };

    const nlohmann::json address = nlohmann::json::array({
        parameter("module_name", "string"),
        parameter("instance_id", "string"),
    });
    return {
        method("getHostCapabilities", nlohmann::json::array(), "LogosMap"),
        method("getModuleInstances", nlohmann::json::array(), "LogosList"),
        method("loadModuleInstance", address, "LogosMap"),
        method("unloadModuleInstance", address, "LogosMap"),
        method("isModuleInstanceLoaded", address, "LogosMap"),
        method("getModuleInstanceInterface", address, "LogosMap"),
        method("callModuleInstanceMethod", nlohmann::json::array({
            parameter("module_name", "string"),
            parameter("instance_id", "string"),
            parameter("method", "string"),
            parameter("args", "LogosList"),
        }), "LogosMap"),
        method("subscribeModuleInstanceEvent", nlohmann::json::array({
            parameter("module_name", "string"),
            parameter("instance_id", "string"),
            parameter("event_name", "string"),
        }), "LogosMap"),
        method("unsubscribeModuleInstanceEvent", nlohmann::json::array({
            parameter("module_name", "string"),
            parameter("instance_id", "string"),
            parameter("event_name", "string"),
        }), "LogosMap"),
    };
}

void BasecampCoreService::setEventListenerStd(UniversalEventCallback callback)
{
    const std::shared_ptr<RelayState> state = relayState_;
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->closed) {
        state->listener = std::move(callback);
    }
}

void BasecampCoreService::setEventListener(EventCallback callback)
{
    setEventListenerStdBridge(std::move(callback));
}

nlohmann::json BasecampCoreService::subscribeModuleInstanceEvent(const nlohmann::json& args)
{
    if (!hasRuntimeOperations() || !hasScopedEventOperations()) {
        return error("HOST_UNAVAILABLE", "Basecamp scoped event relay is unavailable");
    }
    if (!hasExactStringArguments(args, 3)
        || !validAddressSegment(args[0].get<std::string>())
        || !validAddressSegment(args[1].get<std::string>())
        || !validAddressSegment(args[2].get<std::string>())) {
        return error("INVALID_ARGS",
                     "subscribeModuleInstanceEvent expects module_name, instance_id, and event_name");
    }

    const QString moduleName = QString::fromStdString(args[0].get<std::string>());
    const QString instanceId = QString::fromStdString(args[1].get<std::string>());
    const QString eventName = QString::fromStdString(args[2].get<std::string>());
    if (eventName != QString::fromUtf8(kScopedLifecycleEvent.data(),
                                       kScopedLifecycleEvent.size())) {
        return error("EVENT_NOT_SUPPORTED", "only nodeChanged is relayable through core_service");
    }
    const ScopedEventAddress address { moduleName, instanceId, eventName };
    const std::shared_ptr<RelayState> state = relayState_;
    if (!state) {
        return error("HOST_UNAVAILABLE", "Basecamp scoped event relay is unavailable");
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed || !state->listener) {
            return error("EVENT_INGRESS_UNAVAILABLE",
                         "subscribe to core_service.moduleInstanceEvent before requesting a scoped event");
        }
        if (state->subscriptions.find(address) != state->subscriptions.end()) {
            return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                                  {"instance_id", args[1]}, {"event_name", args[2]},
                                  {"already_subscribed", true}};
        }
        state->subscriptions.insert(address);
    }

    const std::weak_ptr<RelayState> weakState(state);
    const bool subscribed = operations_.subscribeModuleInstanceEvent(
        moduleName,
        instanceId,
        eventName,
        [weakState, address](const QString& receivedModule,
                             const QString& receivedInstance,
                             const QString& receivedEvent,
                             const QVariantList& receivedArgs) {
            if (receivedModule != address.moduleName || receivedInstance != address.instanceId
                || receivedEvent != address.eventName) {
                return;
            }
            const std::shared_ptr<RelayState> relay = weakState.lock();
            if (!relay) return;

            UniversalEventCallback listener;
            {
                std::lock_guard<std::mutex> lock(relay->mutex);
                if (relay->closed || relay->subscriptions.find(address) == relay->subscriptions.end()
                    || !relay->listener) {
                    return;
                }
                listener = relay->listener;
            }

            try {
                nlohmann::json eventArgs = nlohmann::json::array();
                for (const QVariant& value : receivedArgs) {
                    eventArgs.push_back(logos::qvariantToNlohmann(value));
                }
                const nlohmann::json envelope {
                    {"schema", "logos.basecamp_host.module_event"},
                    {"version", 1},
                    {"module_name", address.moduleName.toStdString()},
                    {"instance_id", address.instanceId.toStdString()},
                    {"event_name", address.eventName.toStdString()},
                    {"args", std::move(eventArgs)},
                };
                const std::string payload = nlohmann::json::array({envelope}).dump();
                if (payload.size() > kMaxScopedEventPayloadBytes) return;
                listener(std::string(kScopedRelayEvent), payload);
            } catch (...) {
                // Event ingress is best effort. Do not unwind through a module callback.
            }
        });
    if (!subscribed) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->subscriptions.erase(address);
        return error("MODULE_INSTANCE_EVENT_SUBSCRIBE_FAILED",
                     "Basecamp could not subscribe to the requested module instance event");
    }

    return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                          {"instance_id", args[1]}, {"event_name", args[2]},
                          {"already_subscribed", false}};
}

nlohmann::json BasecampCoreService::unsubscribeModuleInstanceEvent(const nlohmann::json& args)
{
    if (!hasRuntimeOperations() || !hasScopedEventOperations()) {
        return error("HOST_UNAVAILABLE", "Basecamp scoped event relay is unavailable");
    }
    if (!hasExactStringArguments(args, 3)
        || !validAddressSegment(args[0].get<std::string>())
        || !validAddressSegment(args[1].get<std::string>())
        || !validAddressSegment(args[2].get<std::string>())) {
        return error("INVALID_ARGS",
                     "unsubscribeModuleInstanceEvent expects module_name, instance_id, and event_name");
    }

    const QString moduleName = QString::fromStdString(args[0].get<std::string>());
    const QString instanceId = QString::fromStdString(args[1].get<std::string>());
    const QString eventName = QString::fromStdString(args[2].get<std::string>());
    if (eventName != QString::fromUtf8(kScopedLifecycleEvent.data(),
                                       kScopedLifecycleEvent.size())) {
        return error("EVENT_NOT_SUPPORTED", "only nodeChanged is relayable through core_service");
    }
    const ScopedEventAddress address { moduleName, instanceId, eventName };
    const std::shared_ptr<RelayState> state = relayState_;
    if (!state) {
        return error("HOST_UNAVAILABLE", "Basecamp scoped event relay is unavailable");
    }

    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        removed = state->subscriptions.erase(address) != 0;
    }
    if (removed) {
        operations_.unsubscribeModuleInstanceEvent(moduleName, instanceId, eventName);
    }
    return nlohmann::json{{"status", "ok"}, {"module_name", args[0]},
                          {"instance_id", args[1]}, {"event_name", args[2]},
                          {"removed", removed}};
}

void BasecampCoreService::removeModuleInstanceEventSubscriptions(const QString& moduleName,
                                                                 const QString& instanceId) noexcept
{
    const std::shared_ptr<RelayState> state = relayState_;
    if (!state || !hasScopedEventOperations()) return;

    std::vector<ScopedEventAddress> removed;
    try {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (auto it = state->subscriptions.begin(); it != state->subscriptions.end();) {
                if (it->moduleName != moduleName || it->instanceId != instanceId) {
                    ++it;
                    continue;
                }
                removed.push_back(*it);
                it = state->subscriptions.erase(it);
            }
        }
        for (const ScopedEventAddress& address : removed) {
            operations_.unsubscribeModuleInstanceEvent(
                address.moduleName, address.instanceId, address.eventName);
        }
    } catch (...) {
        // Unload must not throw through the provider ABI.
    }
}

void BasecampCoreService::clearModuleInstanceEventSubscriptions() noexcept
{
    const std::shared_ptr<RelayState> state = relayState_;
    if (!state) return;
    try {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->closed = true;
            state->listener = {};
            state->subscriptions.clear();
        }
        if (hasScopedEventOperations()) {
            operations_.clearModuleInstanceEventSubscriptions();
        }
    } catch (...) {
        // Destruction must not throw through Basecamp's plugin teardown path.
    }
}
