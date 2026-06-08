#include "pch.h"
#include "named_pipe.h"

NamedPipeClient& NamedPipeClient::Get() {
    static NamedPipeClient instance;
    return instance;
}

void NamedPipeClient::Initialize(const std::string& pipeName) {
    if (_initialized.exchange(true)) return;
    _pipeName = pipeName;
    InitializeCriticalSection(&_cs);
}

void NamedPipeClient::SendSqlMessage(const std::string& oracleUser, const std::string& windowsUser, const std::string& sqlText) {
    std::string escapedSql = sqlText;
    for (size_t i = 0; i < escapedSql.size(); ++i) {
        if (escapedSql[i] == '\\') { escapedSql.insert(i, "\\"); i++; }
        else if (escapedSql[i] == '"') { escapedSql.insert(i, "\\"); i++; }
        else if (escapedSql[i] == '\n') { escapedSql.replace(i, 1, "\\n"); }
        else if (escapedSql[i] == '\r') { escapedSql.erase(i, 1); i--; }
        else if (escapedSql[i] < 32) { escapedSql.replace(i, 1, " "); }
    }

    std::string escapedUser = oracleUser;
    for (size_t i = 0; i < escapedUser.size(); ++i) {
        if (escapedUser[i] == '\\') { escapedUser.insert(i, "\\"); i++; }
        else if (escapedUser[i] == '"') { escapedUser.insert(i, "\\"); i++; }
        else if (escapedUser[i] < 32) { escapedUser.replace(i, 1, " "); }
    }

    std::string escapedWinUser = windowsUser;
    for (size_t i = 0; i < escapedWinUser.size(); ++i) {
        if (escapedWinUser[i] == '\\') { escapedWinUser.insert(i, "\\"); i++; }
        else if (escapedWinUser[i] == '"') { escapedWinUser.insert(i, "\\"); i++; }
        else if (escapedWinUser[i] < 32) { escapedWinUser.replace(i, 1, " "); }
    }

    std::string msg = "{\"u\":\"" + escapedUser + "\",\"w\":\"" + escapedWinUser + "\",\"s\":\"" + escapedSql + "\"}\n";
    WriteMessage(msg);
}

bool NamedPipeClient::Connect() {
    EnterCriticalSection(&_cs);

    if (_pipe != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&_cs);
        return true;
    }

    _pipe = CreateFileA(
        _pipeName.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    if (_pipe == INVALID_HANDLE_VALUE) {
        _pipe = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&_cs);
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(_pipe, &mode, nullptr, nullptr);

    LeaveCriticalSection(&_cs);
    return true;
}

bool NamedPipeClient::WriteMessage(const std::string& msg) {
    for (int retry = 0; retry < 5; retry++) {
        if (!Connect()) {
            Sleep(200);
            continue;
        }

        DWORD written = 0;
        BOOL ok = WriteFile(_pipe, msg.c_str(), static_cast<DWORD>(msg.size()), &written, nullptr);

        if (ok && written == msg.size()) {
            return true;
        }

        EnterCriticalSection(&_cs);
        if (_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(_pipe);
            _pipe = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&_cs);
        Sleep(200);
    }
    return false;
}

void NamedPipeClient::Shutdown() {
    EnterCriticalSection(&_cs);
    if (_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(_pipe);
        _pipe = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&_cs);
    DeleteCriticalSection(&_cs);
}
