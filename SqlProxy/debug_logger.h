#pragma once
#include <string>
#include <mutex>

class DebugLogger {
public:
    DebugLogger(const std::string& path, bool enabled);
    void Log(const std::string& msg);
    void LogHex(const std::string& prefix, const BYTE* data, int len);

private:
    std::string _path;
    bool _enabled;
    std::mutex _mutex;

    void EnsureDirectory();
};
