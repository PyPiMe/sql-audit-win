#pragma once
#include <string>
#include <atomic>

class NamedPipeClient {
public:
    static NamedPipeClient& Get();

    void Initialize(const std::string& pipeName);
    void SendSqlMessage(const std::string& username, const std::string& sqlText);
    void Shutdown();

private:
    NamedPipeClient() = default;
    ~NamedPipeClient() { Shutdown(); }

    bool Connect();
    bool WriteMessage(const std::string& msg);

    std::string _pipeName;
    std::atomic<bool> _initialized{ false };
    HANDLE _pipe{ INVALID_HANDLE_VALUE };
    CRITICAL_SECTION _cs;
};
