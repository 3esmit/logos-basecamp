#pragma once

#include <functional>
#include <vector>

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include "logos_api.h"

class QTimer;
class LogosObject;

// CoreModuleManager — single owner of the logos_core_* C API.
//
// Every call into liblogos (known/loaded module lists, load/unload, cascade
// unload, stats) funnels through this class. Everything else in the app
// (UIPluginManager, PackageManager, MainUIBackend) uses these thin wrappers and never touches
// the C API directly.
//
// This split exists so the UI-plugin lifecycle, dependency caches, and
// cascade-confirmation state live somewhere that doesn't need to know about
// memory-marshalling char*/char** conventions. It also gives us a natural
// seam to swap the underlying runtime (Remote vs Local) in one place.
//
// Also owns the periodic stats poller — a single 2s QTimer that asks
// liblogos for per-module CPU/memory and emits coreModulesChanged() so the
// Modules tab re-reads via Q_PROPERTY.
class CoreModuleManager : public QObject {
    Q_OBJECT

public:
    using ModuleInstanceEventCallback = std::function<void(
        const QString& moduleName,
        const QString& instanceId,
        const QString& eventName,
        const QVariantList& args)>;

    explicit CoreModuleManager(LogosAPI* logosAPI, QObject* parent = nullptr);
    ~CoreModuleManager() override;

    // Thin C API wrappers — each is a one-liner over the corresponding
    // logos_core_* call with char*/QStringList marshalling. Callers get
    // cooked Qt types; they never see a raw C string.
    QStringList knownModules() const;
    QStringList loadedModules() const;
    // Returns true on success. Wraps logos_core_load_module(name, true)
    // (the C API function that also resolves forward deps before loading).
    bool loadModule(const QString& name);
    // Explicit runtime-address variant. A non-empty instance ID always means
    // one independent module runtime; it never aliases the default instance.
    // Dependencies remain default-instance package dependencies in liblogos.
    bool loadModuleInstance(const QString& moduleName, const QString& instanceId);
    // Returns true on success. Wraps logos_core_unload_module — this does NOT
    // cascade. Caller is responsible for cascade semantics (see
    // unloadModuleWithDependents).
    bool unloadModule(const QString& name);
    // Unloads exactly one explicit runtime. Scoped unloads deliberately do
    // not cascade package-level dependents because siblings may share them.
    bool unloadModuleInstance(const QString& moduleName, const QString& instanceId);
    bool isModuleInstanceLoaded(const QString& moduleName,
                                const QString& instanceId) const;
    // JSON array from liblogos with the complete active runtime address,
    // endpoint, PID, and default-instance marker for every loaded module.
    QString moduleInstancesInfo() const;
    // Cascade variant — tears down `name` and all currently-loaded modules
    // that depend on it, leaves-first. Returns true on full success; false
    // if any individual unload step failed (the cascade may have made
    // progress — callers should still refresh their UI state).
    bool unloadModuleWithDependents(const QString& name);

    // Cached stats as of the last timer tick (may be up to ~2s stale). Empty
    // entries for modules the poller hasn't seen yet. QML renders "0.0" via
    // the caller's compose layer when absent — we return the raw map here.
    QVariantMap moduleStats(const QString& name) const;

    // Re-scan every plugin directory via the lib, then emit
    // coreModulesChanged(). Used by the Modules tab's Reload button and by
    // PackageManager after install/uninstall events reshape the known set.
    Q_INVOKABLE void refresh();

    // Introspection — serialised to JSON for QML. getMethods/getEvents return
    // "[]" and callMethod returns error JSON on failure rather than throwing.
    // Module not being connected is a normal transient state, not an error.
    Q_INVOKABLE QString getMethods(const QString& moduleName);
    Q_INVOKABLE QString getEvents(const QString& moduleName);
    Q_INVOKABLE QString callMethod(const QString& moduleName,
                                   const QString& methodName,
                                   const QString& argsJson);
    QString getModuleInstanceMethods(const QString& moduleName,
                                     const QString& instanceId);
    QString getModuleInstanceEvents(const QString& moduleName,
                                    const QString& instanceId);
    QString callModuleInstanceMethod(const QString& moduleName,
                                     const QString& instanceId,
                                     const QString& methodName,
                                     const QString& argsJson);
    bool subscribeModuleInstanceEvent(const QString& moduleName,
                                      const QString& instanceId,
                                      const QString& eventName,
                                      ModuleInstanceEventCallback callback);
    void unsubscribeModuleInstanceEvent(const QString& moduleName,
                                        const QString& instanceId,
                                        const QString& eventName);
    void clearModuleInstanceEventSubscriptions();

signals:
    // Emitted by refresh() and after every stats-timer tick. MainUIBackend
    // forwards this into its own signal of the same name via a signal-to-
    // signal connect — QML binds to that forwarder.
    void coreModulesChanged();

private slots:
    void updateModuleStats();

private:
    void clearModuleInstanceEventSubscriptions(const QString& moduleName,
                                               const QString& instanceId);

    struct ModuleInstanceEventSubscription {
        QString moduleName;
        QString instanceId;
        QString eventName;
        LogosObject* object = nullptr;
    };

    LogosAPI* m_logosAPI;   // not owned
    QTimer*   m_statsTimer; // owned (parent=this)
    QMap<QString, QVariantMap> m_moduleStats;
    std::vector<ModuleInstanceEventSubscription> m_moduleInstanceEventSubscriptions;
};
