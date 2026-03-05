#include "mapps/MeshEventHandler.h"

#include <atomic>

#ifdef HAS_FREE_RTOS
#include <mutex>
#endif

static std::atomic<MeshEventHandler *> globalHandler{nullptr};

void setGlobalMeshEventHandler(MeshEventHandler *handler)
{
    globalHandler.store(handler, std::memory_order_release);
}

MeshEventHandler *getGlobalMeshEventHandler()
{
    return globalHandler.load(std::memory_order_acquire);
}

#ifdef HAS_FREE_RTOS
static std::mutex callbackMutex;
#endif
static AppReadyCallback appReadyCallback;

void setAppReadyCallback(AppReadyCallback cb)
{
#ifdef HAS_FREE_RTOS
    std::lock_guard<std::mutex> lock(callbackMutex);
#endif
    appReadyCallback = std::move(cb);
}

AppReadyCallback getAppReadyCallback()
{
#ifdef HAS_FREE_RTOS
    std::lock_guard<std::mutex> lock(callbackMutex);
#endif
    return appReadyCallback;
}
