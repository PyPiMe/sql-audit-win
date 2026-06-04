#pragma once
#include <functional>
#include <unordered_set>
#include <mutex>

class Injector {
public:
    using LogFunc = std::function<void(const std::string&)>;

    Injector(const std::string& hookDllPath, LogFunc logger);
    void WatchAndInject();
    void UnloadAll();

private:
    bool InjectIntoProcess(DWORD pid);
    static std::vector<DWORD> FindOracleClientPids();

    std::string _dllPath;
    LogFunc _log;
    std::mutex _mutex;
    std::unordered_map<DWORD, void*> _injected;  // PID -> HMODULE
};
