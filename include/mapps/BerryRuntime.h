#pragma once

#include "mapps/AppRuntime.h"

#include <memory>

extern "C" {
#include "berry.h"
}

class AppStateBackend;

class BerryRuntime : public AppRuntime
{
  public:
    // Constructed with app slug and permissions list
    BerryRuntime(const std::string &appSlug, const std::vector<std::string> &permissions);
    ~BerryRuntime() override;

    // Provide the Berry source code to execute on start()
    void setSource(const std::string &source);

    // Set the app state backend; start() auto-registers app_state bindings when set
    void setAppStateBackend(std::shared_ptr<AppStateBackend> backend);

    // AppRuntime interface
    AppRuntime *addBindings(const std::string &moduleName,
                            const std::map<std::string, NativeAppFunction> &bindings) override;
    void setBootstrap(const std::string &moduleName, const std::string &script) override;
    bool start() override;
    void stop() override;
    AppValue call(const std::string &function) override;
    AppValue call(const std::string &function, const std::vector<AppValue> &args) override;
    bool isRunning() const override { return vm != nullptr; }
    bool hasPermission(const std::string &perm) const override;

    bvm *getVM() const { return vm; }

    // Return the last error message from start()/call() (empty if none)
    std::string getLastError() const override { return lastError; }

    // Flattened at start(): "_moduleName_funcName" -> NativeAppFunction
    // Public because the static C callback needs access via pointer.
    std::map<std::string, NativeAppFunction> flatBindings;

  private:
    void createVM();
    void destroyVM();
    void registerBindings();
    std::string generateBootstrap() const;

    static void obsHook(bvm *vm, int event, ...);

    // Set at construction
    std::string appSlug;
    std::vector<std::string> permissions;

    // Source code provided via setSource()
    std::string sourceCode;

    // Accumulated via addBindings() before start()
    // moduleName -> {funcName -> NativeAppFunction}
    std::map<std::string, std::map<std::string, NativeAppFunction>> moduleBindings;

    // Custom bootstrap scripts per module (overrides auto-generated class)
    std::map<std::string, std::string> customBootstraps;

    // App state backend (optional)
    std::shared_ptr<AppStateBackend> appStateBackend;

    // Last error message from start()/call()
    std::string lastError;

    bvm *vm;
    int instructionCount;

    static constexpr int MAX_INSTRUCTIONS = 100000;
};
