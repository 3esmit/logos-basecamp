#include "CoreModuleManager.h"

#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QVariantList>

#include "logos_api_client.h"
#include "logos_object.h"
#include "logos_types.h"

// The logos_core_* C API is forward-declared here rather than in a shared
// header so this is the only translation unit that links against it
// directly. Anywhere else that needs core-plugin state goes through the
// typed wrappers above.
extern "C" {
    char* logos_core_get_module_stats();
    char** logos_core_get_known_modules();
    char** logos_core_get_loaded_modules();
    int logos_core_load_module(const char* module_name, bool with_dependencies);
    int logos_core_load_module_instance(const char* module_name,
                                        const char* instance_id,
                                        bool with_dependencies);
    int logos_core_unload_module(const char* module_name, bool with_dependents);
    int logos_core_unload_module_instance(const char* module_name,
                                          const char* instance_id,
                                          bool with_dependents);
    int logos_core_is_module_instance_loaded(const char* module_name,
                                             const char* instance_id);
    char* logos_core_get_module_instances_info();
    void logos_core_refresh_modules();
}

namespace {

// Drain a NULL-terminated char** handed back by the lib and release every
// entry plus the outer array. liblogos allocates these with new char[] /
// new char*[] (see module_manager.cpp::toNullTerminatedArray), so delete[]
// is the correct deallocator — do NOT use free(). Used by the knownModules
// / loadedModules wrappers.
QStringList drainCStringArray(char** arr)
{
    QStringList out;
    if (!arr) return out;
    for (char** p = arr; *p != nullptr; ++p) {
        out << QString::fromUtf8(*p);
        delete[] *p;
    }
    delete[] arr;
    return out;
}

QJsonValue variantToJsonValue(const QVariant& value)
{
    if (!value.isValid()) return QJsonValue();

    if (value.canConvert<LogosResult>()) {
        const LogosResult result = value.value<LogosResult>();
        QJsonObject object;
        object.insert(QStringLiteral("success"), result.success);
        object.insert(QStringLiteral("value"), variantToJsonValue(result.value));
        object.insert(QStringLiteral("error"), variantToJsonValue(result.error));
        return object;
    }

    switch (value.metaType().id()) {
    case QMetaType::QVariantMap: {
        QJsonObject object;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            object.insert(it.key(), variantToJsonValue(it.value()));
        }
        return object;
    }
    case QMetaType::QVariantList: {
        QJsonArray array;
        const QVariantList list = value.toList();
        for (const QVariant& item : list) {
            array.append(variantToJsonValue(item));
        }
        return array;
    }
    default:
        return QJsonValue::fromVariant(value);
    }
}

}

CoreModuleManager::CoreModuleManager(LogosAPI* logosAPI, QObject* parent)
    : QObject(parent)
    , m_logosAPI(logosAPI)
    , m_statsTimer(new QTimer(this))
{
    connect(m_statsTimer, &QTimer::timeout,
            this, &CoreModuleManager::updateModuleStats);
    m_statsTimer->start(2000);
}

CoreModuleManager::~CoreModuleManager()
{
    clearModuleInstanceEventSubscriptions();
    // Belt-and-braces: Qt parenting already stops/deletes the timer, but
    // calling stop() here guarantees no in-flight tick fires against a
    // half-destroyed object during child destruction of other siblings.
    if (m_statsTimer) m_statsTimer->stop();
}

QStringList CoreModuleManager::knownModules() const
{
    return drainCStringArray(logos_core_get_known_modules());
}

QStringList CoreModuleManager::loadedModules() const
{
    return drainCStringArray(logos_core_get_loaded_modules());
}

bool CoreModuleManager::loadModule(const QString& name)
{
    return logos_core_load_module(name.toUtf8().constData(), true) == 1;
}

bool CoreModuleManager::loadModuleInstance(const QString& moduleName,
                                           const QString& instanceId)
{
    if (moduleName.trimmed().isEmpty() || instanceId.trimmed().isEmpty()) {
        return false;
    }
    const QByteArray moduleUtf8 = moduleName.toUtf8();
    const QByteArray instanceUtf8 = instanceId.toUtf8();
    return logos_core_load_module_instance(moduleUtf8.constData(),
                                           instanceUtf8.constData(),
                                           true) == 1;
}

bool CoreModuleManager::unloadModule(const QString& name)
{
    return logos_core_unload_module(name.toUtf8().constData(), false) == 1;
}

bool CoreModuleManager::unloadModuleInstance(const QString& moduleName,
                                             const QString& instanceId)
{
    if (moduleName.trimmed().isEmpty() || instanceId.trimmed().isEmpty()) {
        return false;
    }
    const QByteArray moduleUtf8 = moduleName.toUtf8();
    const QByteArray instanceUtf8 = instanceId.toUtf8();
    const bool unloaded = logos_core_unload_module_instance(moduleUtf8.constData(),
                                                             instanceUtf8.constData(),
                                                             false) == 1;
    if (unloaded) {
        clearModuleInstanceEventSubscriptions(moduleName, instanceId);
    }
    return unloaded;
}

bool CoreModuleManager::isModuleInstanceLoaded(const QString& moduleName,
                                               const QString& instanceId) const
{
    if (moduleName.trimmed().isEmpty() || instanceId.trimmed().isEmpty()) {
        return false;
    }
    const QByteArray moduleUtf8 = moduleName.toUtf8();
    const QByteArray instanceUtf8 = instanceId.toUtf8();
    return logos_core_is_module_instance_loaded(moduleUtf8.constData(),
                                                 instanceUtf8.constData()) == 1;
}

QString CoreModuleManager::moduleInstancesInfo() const
{
    char* instancesJson = logos_core_get_module_instances_info();
    if (!instancesJson) {
        return QStringLiteral("[]");
    }
    const QString result = QString::fromUtf8(instancesJson);
    delete[] instancesJson;
    return result;
}

bool CoreModuleManager::unloadModuleWithDependents(const QString& name)
{
    return logos_core_unload_module(name.toUtf8().constData(), true) == 1;
}

QVariantMap CoreModuleManager::moduleStats(const QString& name) const
{
    return m_moduleStats.value(name);
}

void CoreModuleManager::refresh()
{
    // Re-scan all module directories via the lib, then let the Modules tab
    // re-read the composed list through Q_PROPERTY.
    logos_core_refresh_modules();
    emit coreModulesChanged();
}

QString CoreModuleManager::getMethods(const QString& moduleName)
{
    return getModuleInstanceMethods(moduleName, QString());
}

QString CoreModuleManager::getModuleInstanceMethods(const QString& moduleName,
                                                     const QString& instanceId)
{
    if (!m_logosAPI) {
        return "[]";
    }

    LogosAPIClient* client = m_logosAPI->getClient(moduleName, instanceId);
    if (!client || !client->isConnected()) {
        return "[]";
    }

    QVariant result = client->invokeRemoteMethod(moduleName, "getPluginMethods");
    if (result.canConvert<QJsonArray>()) {
        QJsonArray methods = result.toJsonArray();
        QJsonDocument doc(methods);
        return doc.toJson(QJsonDocument::Compact);
    }

    return "[]";
}

QString CoreModuleManager::getEvents(const QString& moduleName)
{
    return getModuleInstanceEvents(moduleName, QString());
}

QString CoreModuleManager::getModuleInstanceEvents(const QString& moduleName,
                                                    const QString& instanceId)
{
    if (!m_logosAPI) {
        return "[]";
    }

    LogosAPIClient* client = m_logosAPI->getClient(moduleName, instanceId);
    if (!client || !client->isConnected()) {
        return "[]";
    }

    QVariant result = client->invokeRemoteMethod(moduleName, "getPluginEvents");
    if (result.canConvert<QJsonArray>()) {
        QJsonArray events = result.toJsonArray();
        QJsonDocument doc(events);
        return doc.toJson(QJsonDocument::Compact);
    }

    return "[]";
}

QString CoreModuleManager::callMethod(const QString& moduleName,
                                      const QString& methodName,
                                      const QString& argsJson)
{
    return callModuleInstanceMethod(moduleName, QString(), methodName, argsJson);
}

QString CoreModuleManager::callModuleInstanceMethod(const QString& moduleName,
                                                    const QString& instanceId,
                                                    const QString& methodName,
                                                    const QString& argsJson)
{
    if (!m_logosAPI) {
        return "{\"error\": \"LogosAPI not available\"}";
    }

    LogosAPIClient* client = m_logosAPI->getClient(moduleName, instanceId);
    if (!client || !client->isConnected()) {
        return "{\"error\": \"Module not connected\"}";
    }

    QJsonDocument argsDoc = QJsonDocument::fromJson(argsJson.toUtf8());
    QJsonArray argsArray = argsDoc.array();

    QVariantList args;
    for (const QJsonValue& val : argsArray) {
        args.append(val.toVariant());
    }

    QVariant result;
    if (args.isEmpty()) {
        result = client->invokeRemoteMethod(moduleName, methodName);
    } else if (args.size() == 1) {
        result = client->invokeRemoteMethod(moduleName, methodName, args[0]);
    } else if (args.size() == 2) {
        result = client->invokeRemoteMethod(moduleName, methodName, args[0], args[1]);
    } else if (args.size() == 3) {
        result = client->invokeRemoteMethod(moduleName, methodName, args[0], args[1], args[2]);
    } else {
        return "{\"error\": \"Too many arguments\"}";
    }

    QJsonObject wrapper;
    wrapper["result"] = variantToJsonValue(result);
    QJsonDocument resultDoc(wrapper);
    return resultDoc.toJson(QJsonDocument::Compact);
}

bool CoreModuleManager::subscribeModuleInstanceEvent(
    const QString& moduleName,
    const QString& instanceId,
    const QString& eventName,
    ModuleInstanceEventCallback callback)
{
    if (!m_logosAPI || moduleName.trimmed().isEmpty() || instanceId.trimmed().isEmpty()
        || eventName.trimmed().isEmpty() || !callback) {
        return false;
    }

    const auto existing = std::find_if(
        m_moduleInstanceEventSubscriptions.cbegin(),
        m_moduleInstanceEventSubscriptions.cend(),
        [&](const ModuleInstanceEventSubscription& subscription) {
            return subscription.moduleName == moduleName
                && subscription.instanceId == instanceId
                && subscription.eventName == eventName;
        });
    if (existing != m_moduleInstanceEventSubscriptions.cend()) {
        return true;
    }

    LogosAPIClient* client = m_logosAPI->getClient(moduleName, instanceId);
    if (!client || !client->isConnected()) {
        return false;
    }
    LogosObject* object = client->requestObject(moduleName);
    if (!object) {
        return false;
    }

    try {
        client->onEvent(
            object,
            eventName,
            [moduleName, instanceId, eventName, callback = std::move(callback)](
                const QString& receivedEvent,
                const QVariantList& args) {
                if (receivedEvent == eventName) {
                    callback(moduleName, instanceId, eventName, args);
                }
            });
        m_moduleInstanceEventSubscriptions.push_back(
            ModuleInstanceEventSubscription { moduleName, instanceId, eventName, object });
        return true;
    } catch (...) {
        object->disconnectEvents();
        object->release();
        return false;
    }
}

void CoreModuleManager::unsubscribeModuleInstanceEvent(const QString& moduleName,
                                                       const QString& instanceId,
                                                       const QString& eventName)
{
    auto subscription = std::find_if(
        m_moduleInstanceEventSubscriptions.begin(),
        m_moduleInstanceEventSubscriptions.end(),
        [&](const ModuleInstanceEventSubscription& candidate) {
            return candidate.moduleName == moduleName
                && candidate.instanceId == instanceId
                && candidate.eventName == eventName;
        });
    if (subscription == m_moduleInstanceEventSubscriptions.end()) {
        return;
    }

    LogosObject* const object = subscription->object;
    m_moduleInstanceEventSubscriptions.erase(subscription);
    if (object) {
        object->disconnectEvents();
        object->release();
    }
}

void CoreModuleManager::clearModuleInstanceEventSubscriptions(const QString& moduleName,
                                                              const QString& instanceId)
{
    for (auto it = m_moduleInstanceEventSubscriptions.begin();
         it != m_moduleInstanceEventSubscriptions.end();) {
        if (it->moduleName != moduleName || it->instanceId != instanceId) {
            ++it;
            continue;
        }
        LogosObject* const object = it->object;
        it = m_moduleInstanceEventSubscriptions.erase(it);
        if (object) {
            object->disconnectEvents();
            object->release();
        }
    }
}

void CoreModuleManager::clearModuleInstanceEventSubscriptions()
{
    for (ModuleInstanceEventSubscription& subscription : m_moduleInstanceEventSubscriptions) {
        if (subscription.object) {
            subscription.object->disconnectEvents();
            subscription.object->release();
        }
    }
    m_moduleInstanceEventSubscriptions.clear();
}

void CoreModuleManager::updateModuleStats()
{
    char* stats_json = logos_core_get_module_stats();
    if (!stats_json) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(stats_json);
    // logos_core_get_module_stats allocates with new char[], so free with delete[].
    delete[] stats_json;

    if (doc.isNull()) {
        qWarning() << "Failed to parse module stats JSON";
        return;
    }

    QJsonArray modulesArray;
    if (doc.isArray()) {
        modulesArray = doc.array();
    } else if (doc.isObject()) {
        QJsonObject root = doc.object();
        modulesArray = root["modules"].toArray();
    }

    for (const QJsonValue& val : modulesArray) {
        QJsonObject moduleObj = val.toObject();
        QString name = moduleObj["name"].toString();

        if (!name.isEmpty()) {
            QVariantMap stats;
            // Tolerate multiple field names across runtime versions. Older
            // lib builds emit `cpu` / `memory` / `memory_MB`; newer ones
            // emit `cpu_percent` / `memory_mb`. Take the first non-zero hit.
            double cpu = moduleObj["cpu_percent"].toDouble();
            if (cpu == 0) cpu = moduleObj["cpu"].toDouble();

            double memory = moduleObj["memory_mb"].toDouble();
            if (memory == 0) memory = moduleObj["memory"].toDouble();
            if (memory == 0) memory = moduleObj["memory_MB"].toDouble();

            stats["cpu"] = QString::number(cpu, 'f', 1);
            stats["memory"] = QString::number(memory, 'f', 1);
            m_moduleStats[name] = stats;
        }
    }

    emit coreModulesChanged();
}
