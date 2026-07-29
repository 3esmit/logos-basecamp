#include "main_ui_plugin.h"
#include "BasecampCoreService.h"
#include "CoreModuleManager.h"
#include "MainContainer.h"
#include "MainUIBackend.h"
#include <QDebug>
#include <QUuid>
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_api_provider.h"
#include "token_manager.h"

MainUIPlugin::MainUIPlugin(QObject* parent)
    : QObject(parent)
    , m_mainContainer(nullptr)
    , m_logosAPI(nullptr)
{
    qDebug() << "MainUIPlugin created";
}

MainUIPlugin::~MainUIPlugin()
{
    qDebug() << "MainUIPlugin destroyed";
    stopCoreService();
    // m_mainContainer may already have been destroyed by its Qt parent
    // (e.g. the central widget owned by Window). QPointer auto-nulls in
    // that case, so destroyWidget() becomes a no-op and we avoid a
    // double-delete during plugin unload at process exit.
    destroyWidget(m_mainContainer.data());
}

QWidget* MainUIPlugin::createWidget(LogosAPI* logosAPI)
{
    qDebug() << "-----> MainUIPlugin::createWidget: logosAPI:" << logosAPI;
    if (logosAPI) {
        m_logosAPI = logosAPI;
    }

    // Do not log capability tokens. They authorize cross-module calls.
    if (m_logosAPI) {
        QList<QString> keys = m_logosAPI->getTokenManager()->getTokenKeys();
        qDebug() << "MainUIPlugin token manager initialized with" << keys.size()
                 << "registered module identities";
    }
    
    if (!m_mainContainer) {
        m_mainContainer = new MainContainer(m_logosAPI);
        startCoreService();
    }
    return m_mainContainer;
}

void MainUIPlugin::destroyWidget(QWidget* widget)
{
    if (widget) {
        if (widget == m_mainContainer.data()) {
            stopCoreService();
        }
        delete widget;
        if (widget == m_mainContainer) {
            m_mainContainer = nullptr;
        }
    }
}

void MainUIPlugin::startCoreService()
{
    if (m_coreService || !m_logosAPI || !m_mainContainer || !m_mainContainer->getBackend()) {
        return;
    }

    QPointer<CoreModuleManager> runtime =
        m_mainContainer->getBackend()->coreModuleManager();
    if (!runtime) {
        qWarning() << "Basecamp core service unavailable: runtime manager missing";
        return;
    }

    BasecampCoreRuntimeOperations operations;
    operations.loadModuleInstance = [runtime](const QString& moduleName,
                                              const QString& instanceId) {
        return runtime && runtime->loadModuleInstance(moduleName, instanceId);
    };
    operations.unloadModuleInstance = [runtime](const QString& moduleName,
                                                const QString& instanceId) {
        return runtime && runtime->unloadModuleInstance(moduleName, instanceId);
    };
    operations.isModuleInstanceLoaded = [runtime](const QString& moduleName,
                                                  const QString& instanceId) {
        return runtime && runtime->isModuleInstanceLoaded(moduleName, instanceId);
    };
    operations.moduleInstancesInfo = [runtime] {
        return runtime ? runtime->moduleInstancesInfo() : QStringLiteral("[]");
    };
    operations.getModuleInstanceMethods = [runtime](const QString& moduleName,
                                                     const QString& instanceId) {
        return runtime ? runtime->getModuleInstanceMethods(moduleName, instanceId)
                       : QStringLiteral("[]");
    };
    operations.getModuleInstanceEvents = [runtime](const QString& moduleName,
                                                    const QString& instanceId) {
        return runtime ? runtime->getModuleInstanceEvents(moduleName, instanceId)
                       : QStringLiteral("[]");
    };
    operations.callModuleInstanceMethod = [runtime](const QString& moduleName,
                                                     const QString& instanceId,
                                                     const QString& methodName,
                                                     const QString& argsJson) {
        return runtime ? runtime->callModuleInstanceMethod(
                             moduleName, instanceId, methodName, argsJson)
                       : QStringLiteral("{\"error\":\"Basecamp runtime unavailable\"}");
    };

    TokenManager* tokenManager = m_logosAPI->getTokenManager();
    if (!tokenManager) {
        qWarning() << "Basecamp core service unavailable: token manager missing";
        return;
    }

    const QString existingCoreToken = tokenManager->getToken(QStringLiteral("core"));
    const bool createdCoreToken = existingCoreToken.isEmpty();
    const QString coreServiceToken = createdCoreToken
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : existingCoreToken;
    if (createdCoreToken) {
        tokenManager->saveToken(QStringLiteral("core"), coreServiceToken);
    }

    m_coreServiceApi = std::make_unique<LogosAPI>(QStringLiteral("core_service"));
    m_coreService = std::make_unique<BasecampCoreService>(std::move(operations));
    LogosAPIProvider* provider = m_coreServiceApi->getProvider();
    if (!provider || !provider->registerObject(QStringLiteral("core_service"),
                                               static_cast<LogosProviderObject*>(m_coreService.get()))) {
        qWarning() << "Failed to publish Basecamp core service";
        stopCoreService();
        if (createdCoreToken) {
            tokenManager->removeToken(QStringLiteral("core"));
        }
        return;
    }

    LogosAPIClient* capabilityModule = m_logosAPI->getClient(
        QStringLiteral("capability_module"));
    const QString capabilityToken = tokenManager->getToken(
        QStringLiteral("capability_module"));
    if (!capabilityModule || capabilityToken.isEmpty()
        || !capabilityModule->informModuleToken(
            capabilityToken, QStringLiteral("core_service"), coreServiceToken)) {
        qWarning() << "Failed to register Basecamp core service with capability module";
        stopCoreService();
        if (createdCoreToken) {
            tokenManager->removeToken(QStringLiteral("core"));
        }
        return;
    }
    qInfo() << "Published Basecamp core service";
}

void MainUIPlugin::stopCoreService()
{
    m_coreServiceApi.reset();
    m_coreService.reset();
}
