#include "mapps/MeshEventHandler.h"

#include <atomic>
#include <mutex>

static std::atomic<MeshEventHandler *> globalHandler{nullptr};

void setGlobalMeshEventHandler(MeshEventHandler *handler)
{
    globalHandler.store(handler, std::memory_order_release);
}

MeshEventHandler *getGlobalMeshEventHandler()
{
    return globalHandler.load(std::memory_order_acquire);
}

static std::mutex callbackMutex;
static AppReadyCallback appReadyCallback;

void setAppReadyCallback(AppReadyCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    appReadyCallback = std::move(cb);
}

AppReadyCallback getAppReadyCallback()
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    return appReadyCallback;
}
