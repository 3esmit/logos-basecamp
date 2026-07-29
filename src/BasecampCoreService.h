#pragma once

#include <functional>

#include <QString>

#include "logos_provider_object.h"

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
};

class BasecampCoreService final : public LogosProviderBase
{
public:
    explicit BasecampCoreService(BasecampCoreRuntimeOperations operations);

    QVariant callMethod(const QString& methodName, const QVariantList& args) override;
    QJsonArray getMethods() override;
    QString providerName() const override;
    QString providerVersion() const override;

    nlohmann::json callMethodStd(const std::string& methodName,
                                 const nlohmann::json& args) override;
    std::vector<LogosMethodMetadata> getMethodsStd() override;
    void setEventListenerStd(UniversalEventCallback callback) override;

private:
    static nlohmann::json error(const char* code, const std::string& message);
    static bool validAddressSegment(const std::string& value);
    static nlohmann::json parseArrayResponse(const QString& response,
                                             const char* operation);
    static nlohmann::json parseObjectResponse(const QString& response,
                                              const char* operation);
    bool hasRuntimeOperations() const;

    BasecampCoreRuntimeOperations operations_;
};
