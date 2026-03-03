#pragma once

#include <functional>
#include <string>

/**
 * @brief Interface for forwarding serialized mesh events to an app handler.
 *
 * Implemented by both firmware's AppModule (OLED path) and device-ui's
 * AppHost (TFT/LVGL path).  Firmware calls the global instance from the
 * Router thread; the implementer is responsible for thread-safe queuing.
 */
class MeshEventHandler
{
  public:
    virtual ~MeshEventHandler() = default;

    /// Return true if this handler wants packets on the given port number.
    /// Default-empty: AppHost no longer implements this.
    virtual bool wantPort(int port) const { return false; }

    /// Queue a serialized mesh-event JSON string for later processing.
    /// Called from the Router thread -- implementations must be thread-safe.
    /// Default-empty: AppHost no longer implements this.
    virtual void handleMeshEvent(const std::string &eventJson) {}

    /// Called from Router thread when a handler updates app_state.
    /// Implementers push to a thread-safe queue for UI-thread consumption.
    virtual void handleStateChanged(const std::string &appSlug, const std::string &key, const std::string &value) {}
};

/// Register the active mesh-event handler (nullptr to clear).
void setGlobalMeshEventHandler(MeshEventHandler *handler);

/// Get the currently registered handler (may be nullptr).
MeshEventHandler *getGlobalMeshEventHandler();

/// Callback type for notifying firmware when a new app is approved and ready.
using AppReadyCallback = std::function<void(const std::string &slug)>;

/// Set the callback that fires when an app becomes ready (e.g., first-time approval).
void setAppReadyCallback(AppReadyCallback cb);

/// Get the currently registered app-ready callback (may be empty).
AppReadyCallback getAppReadyCallback();
