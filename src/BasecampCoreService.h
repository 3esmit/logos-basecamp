#pragma once

#include <functional>
#include <memory>
#include <string>

#include <QString>
#include <QVariantList>

#include "logos_provider_object.h"

using BasecampModuleInstanceEventCallback = std::function<void(
    const QString& moduleName,
    const QString& instanceId,
    const QString& eventName,
    const QVariantList& args)>;

// The Basecamp process owns the liblogos runtime. Core modules run in child
// hosts, so they need a narrow in-process provider to request an explicit
// runtime address without linking a second liblogos runtime into the child.
// Every operation below keeps module and instance identity separate.
struct BasecampCoreRuntimeOperations {
    std::function<bool(const QString&, const QString&)> loadModuleInstance;
    std::function<bool(const QString&, const QString&)> unloadModuleInstance;
    std::function<bool(const QString&, const QString&)> isModuleInstanceLoaded;
    std::function<QString()> moduleInstancesInfo;
    std::function<QString(const QString&, const QString&)> getModuleInstanceMethods;
    std::function<QString(const QString&, const QString&)> getModuleInstanceEvents;
    std::function<QString(const QString&, const QString&, const QString&, const QString&)>
        callModuleInstanceMethod;
    std::function<bool(const QString&, const QString&, const QString&,
                       BasecampModuleInstanceEventCallback)>
        subscribeModuleInstanceEvent;
    std::function<void(const QString&, const QString&, const QString&)>
        unsubscribeModuleInstanceEvent;
    std::function<void()> clearModuleInstanceEventSubscriptions;
};

class BasecampCoreService final : public LogosProviderBase
{
public:
    explicit BasecampCoreService(BasecampCoreRuntimeOperations operations);
    ~BasecampCoreService() override;

    QVariant callMethod(const QString& methodName, const QVariantList& args) override;
    QJsonArray getMethods() override;
    QString providerName() const override;
    QString providerVersion() const override;

    nlohmann::json callMethodStd(const std::string& methodName,
                                 const nlohmann::json& args) override;
    std::vector<LogosMethodMetadata> getMethodsStd() override;
    void setEventListener(EventCallback callback) override;
    void setEventListenerStd(UniversalEventCallback callback) override;

private:
    struct RelayState;

    static nlohmann::json error(const char* code, const std::string& message);
    static bool validAddressSegment(const std::string& value);
    static nlohmann::json parseArrayResponse(const QString& response,
                                             const char* operation);
    static nlohmann::json parseObjectResponse(const QString& response,
                                              const char* operation);
    bool hasRuntimeOperations() const;
    bool hasScopedEventOperations() const;
    nlohmann::json subscribeModuleInstanceEvent(const nlohmann::json& args);
    nlohmann::json unsubscribeModuleInstanceEvent(const nlohmann::json& args);
    void removeModuleInstanceEventSubscriptions(const QString& moduleName,
                                                const QString& instanceId) noexcept;
    void clearModuleInstanceEventSubscriptions() noexcept;

    BasecampCoreRuntimeOperations operations_;
    std::shared_ptr<RelayState> relayState_;
};
