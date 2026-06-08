#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class PipeServer {
public:
    using MessageHandler = std::function<void(const std::string& oracleUser, const std::string& windowsUser, const std::string& sqlText)>;

    PipeServer(const std::string& pipeName, MessageHandler handler);
    ~PipeServer();

    void Start();
    void Stop();

private:
    void ListenLoop();
    void HandleClient(HANDLE pipe);

    std::string _pipeName;
    MessageHandler _handler;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::mutex _closeMutex;
    std::vector<HANDLE> _pendingPipes;
    HANDLE _stopEvent;
};
