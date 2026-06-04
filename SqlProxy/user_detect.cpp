#include "pch.h"
#include "user_detect.h"
#include <tlhelp32.h>

std::string GetLocalIpAddress() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "127.0.0.1";

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) { WSACleanup(); return "127.0.0.1"; }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(65530);

    std::string result = "127.0.0.1";
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        sockaddr_in localAddr;
        int len = sizeof(localAddr);
        if (getsockname(sock, (sockaddr*)&localAddr, &len) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &localAddr.sin_addr, ip, sizeof(ip));
            result = ip;
        }
    }

    closesocket(sock);
    WSACleanup();
    return result;
}

std::string GetActiveConsoleUser() {
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId != 0xFFFFFFFF) {
        wchar_t* buf = nullptr;
        DWORD bytes = 0;
        if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &buf, &bytes)) {
            std::wstring wuser(buf);
            WTSFreeMemory(buf);
            int len = WideCharToMultiByte(CP_UTF8, 0, wuser.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string user(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wuser.c_str(), -1, &user[0], len, nullptr, nullptr);
            if (!user.empty() && user.back() != '$') return user;
        }
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        HANDLE hToken = nullptr;
                        if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                            DWORD len = 0;
                            GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
                            if (len > 0) {
                                std::vector<BYTE> buf(len);
                                if (GetTokenInformation(hToken, TokenUser, buf.data(), len, &len)) {
                                    TOKEN_USER* tu = (TOKEN_USER*)buf.data();
                                    wchar_t name[256] = {}, domain[256] = {};
                                    DWORD nameLen = 256, domainLen = 256;
                                    SID_NAME_USE use;
                                    if (LookupAccountSidW(nullptr, tu->User.Sid, name, &nameLen, domain, &domainLen, &use)) {
                                        int slen = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
                                        std::string user(slen - 1, '\0');
                                        WideCharToMultiByte(CP_UTF8, 0, name, -1, &user[0], slen, nullptr, nullptr);
                                        CloseHandle(hToken);
                                        CloseHandle(hProc);
                                        CloseHandle(snap);
                                        return user;
                                    }
                                }
                            }
                            CloseHandle(hToken);
                        }
                        CloseHandle(hProc);
                    }
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    wchar_t userName[256] = {};
    DWORD size = 256;
    GetUserNameW(userName, &size);
    int slen = WideCharToMultiByte(CP_UTF8, 0, userName, -1, nullptr, 0, nullptr, nullptr);
    std::string user(slen > 0 ? slen - 1 : 0, '\0');
    if (slen > 0) WideCharToMultiByte(CP_UTF8, 0, userName, -1, &user[0], slen, nullptr, nullptr);
    if (!user.empty() && user.back() == '$') user.pop_back();
    return user;
}
