#include "BasecampCoreService.h"

#include <cstddef>
#include <QJsonArray>

#include <utility>

namespace {

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

BasecampCoreService::BasecampCoreService(BasecampCoreRuntimeOperations operations)
    : operations_(std::move(operations))
{
}

QVariant BasecampCoreService::callMethod(const QString& methodName,
                                         const QVariantList& args)
{
    return callMethodStdBridge(methodName, args);
}

QJsonArray BasecampCoreService::getMethods()
{
    return getMethodsStdBridge();
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
            {"direct_scoped_clients", true},
            {"direct_scoped_events", true},
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
    };
}

void BasecampCoreService::setEventListenerStd(UniversalEventCallback callback)
{
    // Scoped runtime lifecycle events are consumed from the addressed module
    // directly. This control service emits no independent events.
    static_cast<void>(callback);
}
