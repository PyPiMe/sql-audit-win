#include "pch.h"
#include "config.h"
#include "pipe_server.h"
#include "file_logger.h"
#include "db_uploader.h"
#include "user_detect.h"
#include "debug_logger.h"
#include "injector.h"

static AppConfig g_config;
static std::unique_ptr<FileLogger> g_fileLogger;
static std::unique_ptr<DbUploader> g_dbUploader;
static std::unique_ptr<PipeServer> g_pipeServer;
static std::unique_ptr<DebugLogger> g_debugLogger;
static std::unique_ptr<Injector> g_injector;
static std::string g_sourceIp;
static std::string g_clientHost;
static std::string g_clientUser;
static std::atomic<bool> g_running{true};
static std::atomic<time_t> g_lastActivity{0};
static std::thread g_flushThread;
static std::thread g_injectThread;
static bool g_isService = false;

static std::string GetExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    size_t last = s.find_last_of("\\/");
    return (last != std::string::npos) ? s.substr(0, last + 1) : s;
}

static void DiagLog(const char* msg) {
    if (g_config.StartupLogPath.empty()) return;
    FILE* f = fopen(g_config.StartupLogPath.c_str(), "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

static std::string GuidGen() {
    GUID g;
    CoCreateGuid(&g);
    char buf[64];
    snprintf(buf, sizeof(buf), "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

static std::string TimestampNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static void OnSqlMessage(const std::string& oracleUser, const std::string& windowsUser, const std::string& sqlText) {
    g_lastActivity = time(nullptr);
    SqlAuditRecord record;
    record.Wid = GuidGen();
    record.Timestamp = TimestampNow();
    record.SourceIp = g_sourceIp;
    record.Username = oracleUser;
    record.SqlText = sqlText;
    record.ClientHost = g_clientHost;
    record.ClientUser = windowsUser.empty() ? g_clientUser : windowsUser;
    g_fileLogger->Log(record);
    g_debugLogger->Log("[SQL] [" + record.Timestamp + "]");
    g_debugLogger->Log("[SQL] SourceIp=" + g_sourceIp + ", OracleUser=" + oracleUser + ", WinUser=" + record.ClientUser);
    g_debugLogger->Log("[SQL] ClientHost=" + g_clientHost + ", ClientUser=" + record.ClientUser);
    g_debugLogger->Log("[SQL] " + sqlText);
    printf("[SQL] %s [%s] %s\n", record.Timestamp.c_str(), oracleUser.c_str(), sqlText.c_str());
}

static void InjectorLoop() {
    while (g_running) {
        g_injector->WatchAndInject();
        Sleep(3000);
    }
}

static void FlushLoop() {
    while (g_running) {
        Sleep(30000);
        time_t now = time(nullptr);
        int idleMin = (int)((now - g_lastActivity) / 60);
        if (idleMin >= g_config.FlushIntervalMinutes) {
            auto records = g_fileLogger->GetBufferedRecords();
            if (!records.empty()) {
                g_debugLogger->Log("[IDLE] Flushing " + std::to_string(records.size()) + " records...");
                int uploaded = g_dbUploader->Upload(records);
                if (uploaded > 0) {
                    g_fileLogger->ClearBuffer();
                    g_fileLogger->ClearFile();
                } else {
                    g_debugLogger->Log("[IDLE] Upload failed, retaining " + std::to_string(records.size()) + " records");
                }
            }
        }
    }
}

static int RunApplication() {
    DiagLog("RunApplication start");
    g_config = LoadConfig(GetExeDir() + "config.json");
    DiagLog("config loaded");

    g_debugLogger = std::make_unique<DebugLogger>(g_config.DebugLogPath, g_config.DebugEnabled);
    g_fileLogger = std::make_unique<FileLogger>(g_config.LogFilePath);
    g_fileLogger->LoadFromFile();
    DiagLog("file logger created");
    g_dbUploader = std::make_unique<DbUploader>(g_config.AuditDb,
        [](const std::string& msg) { g_debugLogger->Log(msg); });
    DiagLog("db uploader created");
    g_sourceIp = GetLocalIpAddress();

    char hostBuf[256] = {};
    DWORD hostSize = 256;
    GetComputerNameA(hostBuf, &hostSize);
    g_clientHost = hostBuf;
    g_clientUser = GetActiveConsoleUser();

    g_injector = std::make_unique<Injector>(GetExeDir() + "OciHook.dll",
        [](const std::string& msg) { g_debugLogger->Log(msg); });
    DiagLog("injector created");

    g_debugLogger->Log("========================================");
    g_debugLogger->Log("[START] SQL Audit OCI Hook Service (C++)");
    g_debugLogger->Log("[START] Named Pipe: " + g_config.PipeName);
    if (!g_config.AuditDb.Host.empty())
        g_debugLogger->Log("[START] Audit DB: " + g_config.AuditDb.Host + ":" +
            std::to_string(g_config.AuditDb.Port) + "/" + g_config.AuditDb.ServiceName);
    g_debugLogger->Log("[START] Flush interval: " + std::to_string(g_config.FlushIntervalMinutes) + " minutes");
    g_debugLogger->Log("========================================");

    std::string pipePath = "\\\\.\\pipe\\" + g_config.PipeName;
    g_pipeServer = std::make_unique<PipeServer>(pipePath, OnSqlMessage);
    g_pipeServer->Start();
    DiagLog("pipe server started");

    printf("[INFO] OCI Hook listening on pipe: %s\n", g_config.PipeName.c_str());

    g_lastActivity = time(nullptr);
    g_flushThread = std::thread(FlushLoop);
    DiagLog("flush thread started");
    g_injectThread = std::thread(InjectorLoop);
    DiagLog("inject thread started");

    DiagLog("entering main loop");
    while (g_running) Sleep(1000);
    DiagLog("main loop exited");

    g_pipeServer->Stop();
    g_running = false;
    if (g_flushThread.joinable()) g_flushThread.join();
    if (g_injectThread.joinable()) g_injectThread.join();

    g_debugLogger->Log("[STOP] Service stopped");
    g_injector->UnloadAll();
    DiagLog("RunApplication exit");
    return 0;
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char* argv[]) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--service") == 0) {
            g_isService = true;
            break;
        }
    }

    if (g_isService) {
        DiagLog("service mode, running directly");
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
        RunApplication();
    } else {
        printf("=== SQL Audit OCI Hook (C++) ===\n");
        printf("[INFO] Press Ctrl+C to stop...\n\n");
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
        RunApplication();
    }

    CoUninitialize();
    return 0;
}
