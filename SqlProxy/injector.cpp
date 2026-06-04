#include "pch.h"
#include "injector.h"
#include <tlhelp32.h>

static const wchar_t* KnownOracleClients[] = {
    L"plsqldev.exe", L"sqlplus.exe", L"sqlplusw.exe",
    L"sqldeveloper.exe", L"sqldeveloper64W.exe", L"toad.exe",
    L"datagrip64.exe", L"datagrip.exe", L"dbeaver.exe"
};

Injector::Injector(const std::string& hookDllPath, LogFunc logger) : _dllPath(hookDllPath), _log(logger) {}

std::vector<DWORD> Injector::FindOracleClientPids() {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            for (const auto* client : KnownOracleClients) {
                if (_wcsicmp(pe.szExeFile, client) == 0) {
                    pids.push_back(pe.th32ProcessID);
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

bool Injector::InjectIntoProcess(DWORD pid) {
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProc) {
        _log("[INJECT] OpenProcess failed for PID " + std::to_string(pid) + ", err=" + std::to_string(GetLastError()));
        return false;
    }

    BOOL isWow64 = FALSE;
    IsWow64Process(hProc, &isWow64);
#ifdef _WIN64
    if (isWow64) {
        _log("[INJECT] SKIP PID " + std::to_string(pid) + ": target is 32-bit, injector is 64-bit");
        CloseHandle(hProc);
        return false;
    }
#else
    // 32-bit injector: if target is 64-bit, we're already in WOW64 and OpenProcess would fail
#endif

    size_t pathSize = _dllPath.size() + 1;
    void* remoteMem = VirtualAllocEx(hProc, nullptr, pathSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProc);
        return false;
    }

    if (!WriteProcessMemory(hProc, remoteMem, _dllPath.c_str(), pathSize, nullptr)) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)LoadLibraryA, remoteMem, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    if (exitCode == 0) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        _log("[INJECT] FAIL PID " + std::to_string(pid) + ": LoadLibrary returned NULL");
        return false;
    }

    void* hMod = (void*)(uintptr_t)exitCode;
    _injected[pid] = hMod;

    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    _log("[INJECT] OK: PID " + std::to_string(pid));
    printf("[INJECT] Injected OciHook.dll into PID %lu\n", pid);
    return true;
}

void Injector::UnloadAll() {
    std::lock_guard<std::mutex> lock(_mutex);

    for (auto& [pid, hMod] : _injected) {
        HANDLE hProc = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION,
            FALSE, pid);
        if (!hProc) continue;

        HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
            (LPTHREAD_START_ROUTINE)FreeLibrary, hMod, 0, nullptr);
        if (hThread) {
            WaitForSingleObject(hThread, 3000);
            CloseHandle(hThread);
            printf("[UNLOAD] Unloaded OciHook.dll from PID %lu\n", pid);
        }
        CloseHandle(hProc);
    }
    _injected.clear();
}

void Injector::WatchAndInject() {
    auto pids = FindOracleClientPids();

    std::lock_guard<std::mutex> lock(_mutex);
    for (DWORD pid : pids) {
        if (_injected.count(pid)) continue;

        if (InjectIntoProcess(pid)) {
            printf("[INJECT] Injected OciHook.dll into PID %lu\n", pid);
        }
    }

    for (auto it = _injected.begin(); it != _injected.end(); ) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, it->first);
        if (h) {
            DWORD code;
            if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE) {
                CloseHandle(h);
                ++it;
                continue;
            }
            CloseHandle(h);
        }
        it = _injected.erase(it);
    }
}
