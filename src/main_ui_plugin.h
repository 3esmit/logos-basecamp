#pragma once

#include <IComponent.h>
#include <QObject>
#include <QPointer>
#include <memory>

class BasecampCoreService;
class MainContainer;
class LogosAPI;

class MainUIPlugin : public QObject, public IComponent
{
    Q_OBJECT
    Q_INTERFACES(IComponent)
    Q_PLUGIN_METADATA(IID IComponent_iid FILE "metadata.json")

public:
    explicit MainUIPlugin(QObject* parent = nullptr);
    ~MainUIPlugin();

    // IComponent implementation
    Q_INVOKABLE QWidget* createWidget(LogosAPI* logosAPI = nullptr) override;
    void destroyWidget(QWidget* widget) override;

private:
    void startCoreService();
    void stopCoreService();

    // QPointer auto-nulls when the widget is destroyed by its Qt parent
    // (e.g. by Window's destructor), so ~MainUIPlugin() won't double-delete
    // it later during QLibraryStore::cleanup() at process exit.
    QPointer<MainContainer> m_mainContainer;
    LogosAPI* m_logosAPI;
    // The dedicated LogosAPI owns the provider, which keeps a non-owning
    // pointer to this service. Destroy it first during shutdown.
    std::unique_ptr<LogosAPI> m_coreServiceApi;
    std::unique_ptr<BasecampCoreService> m_coreService;
};
