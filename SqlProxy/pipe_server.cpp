#include "pch.h"
#include "pipe_server.h"

PipeServer::PipeServer(const std::string& pipeName, MessageHandler handler)
    : _pipeName(pipeName), _handler(handler) {
    _stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

PipeServer::~PipeServer() {
    Stop();
    CloseHandle(_stopEvent);
}

void PipeServer::Start() {
    _running = true;
    _thread = std::thread(&PipeServer::ListenLoop, this);
}

void PipeServer::Stop() {
    _running = false;
    SetEvent(_stopEvent);

    {
        std::lock_guard<std::mutex> lock(_closeMutex);
        for (HANDLE h : _pendingPipes) {
            CancelIoEx(h, nullptr);
            DisconnectNamedPipe(h);
            CloseHandle(h);
        }
        _pendingPipes.clear();
    }

    HANDLE ping = CreateFileA(_pipeName.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (ping != INVALID_HANDLE_VALUE) CloseHandle(ping);

    if (_thread.joinable()) _thread.join();
}

void PipeServer::ListenLoop() {
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, FALSE };

    while (_running) {
        HANDLE pipe = CreateNamedPipeA(
            _pipeName.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, &sa);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(_closeMutex);
            _pendingPipes.push_back(pipe);
        }

        OVERLAPPED ol = {};
        ol.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ol);

        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE handles[2] = { ol.hEvent, _stopEvent };
                DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0) {
                    printf("[PIPE] Client connected\n");
                    std::thread([this, pipe]() {
                        HandleClient(pipe);
                        DisconnectNamedPipe(pipe);
                        CloseHandle(pipe);
                        {
                            std::lock_guard<std::mutex> lock(_closeMutex);
                            auto it = std::find(_pendingPipes.begin(), _pendingPipes.end(), pipe);
                            if (it != _pendingPipes.end()) _pendingPipes.erase(it);
                        }
                    }).detach();
                    CloseHandle(ol.hEvent);
                    continue;
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                printf("[PIPE] Client connected\n");
                std::thread([this, pipe]() {
                    HandleClient(pipe);
                    DisconnectNamedPipe(pipe);
                    CloseHandle(pipe);
                    {
                        std::lock_guard<std::mutex> lock(_closeMutex);
                        auto it = std::find(_pendingPipes.begin(), _pendingPipes.end(), pipe);
                        if (it != _pendingPipes.end()) _pendingPipes.erase(it);
                    }
                }).detach();
                CloseHandle(ol.hEvent);
                continue;
            }
        } else {
            printf("[PIPE] Client connected\n");
            std::thread([this, pipe]() {
                HandleClient(pipe);
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                {
                    std::lock_guard<std::mutex> lock(_closeMutex);
                    auto it = std::find(_pendingPipes.begin(), _pendingPipes.end(), pipe);
                    if (it != _pendingPipes.end()) _pendingPipes.erase(it);
                }
            }).detach();
            CloseHandle(ol.hEvent);
            continue;
        }

        CloseHandle(ol.hEvent);

        {
            std::lock_guard<std::mutex> lock(_closeMutex);
            auto it = std::find(_pendingPipes.begin(), _pendingPipes.end(), pipe);
            if (it != _pendingPipes.end()) _pendingPipes.erase(it);
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void PipeServer::HandleClient(HANDLE pipe) {
    char buffer[65536];
    std::string messageBuilder;

    while (_running) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
        if (!ok || bytesRead == 0) break;

        buffer[bytesRead] = '\0';
        messageBuilder.append(buffer, bytesRead);

        while (true) {
            size_t nl = messageBuilder.find('\n');
            if (nl == std::string::npos) break;

            std::string line = messageBuilder.substr(0, nl);
            messageBuilder.erase(0, nl + 1);

            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            size_t uStart = line.find("\"u\":\"");
            size_t sStart = line.find("\"s\":\"");
            if (uStart == std::string::npos || sStart == std::string::npos) continue;

            uStart += 5;
            size_t uEnd = line.find('"', uStart);
            if (uEnd == std::string::npos) continue;

            sStart += 5;
            size_t sEnd = line.rfind('"');
            if (sEnd == std::string::npos || sEnd < sStart) continue;

            std::string username = line.substr(uStart, uEnd - uStart);
            std::string sqlText = line.substr(sStart, sEnd - sStart);

            auto unescape = [](std::string& s) {
                for (size_t i = 0; i + 1 < s.size(); i++) {
                    if (s[i] == '\\') {
                        char next = s[i + 1];
                        if (next == '"' || next == '\\' || next == '/' || next == 'n' || next == 'r' || next == 't') {
                            s.erase(i, 1);
                            if (next == 'n') s[i] = '\n';
                            else if (next == 'r') s[i] = '\r';
                            else if (next == 't') s[i] = '\t';
                        }
                    }
                }
            };
            unescape(sqlText);
            unescape(username);

            if (!sqlText.empty()) {
                _handler(username, sqlText);
            }
        }
    }
}
