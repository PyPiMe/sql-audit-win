#pragma once
#include <string>

struct AuditDbConfig {
    std::string Host;
    int Port = 1521;
    std::string ServiceName;
    std::string Username;
    std::string Password;
};

struct AppConfig {
    std::string PipeName = "SqlProxyOciHook";
    std::string LogFilePath = "logs/sql_audit.log";
    std::string DebugLogPath = "logs/debug.log";
    bool DebugEnabled = true;
    int FlushIntervalMinutes = 10;
    std::string StartupLogPath;
    AuditDbConfig AuditDb;
};

AppConfig LoadConfig(const std::string& path);
