#pragma once
#include <string>
#include <vector>
#include <mutex>

struct SqlAuditRecord {
    std::string Wid;
    std::string Timestamp;
    std::string SourceIp;
    std::string Username;
    std::string SqlText;
    std::string ClientHost;
    std::string ClientUser;
};

class FileLogger {
public:
    FileLogger(const std::string& logPath);
    void Log(const SqlAuditRecord& record);
    std::vector<SqlAuditRecord> GetBufferedRecords();
    void ClearBuffer();
    void ClearFile();

private:
    void EnsureDirectory();

    std::string _logPath;
    std::mutex _mutex;
    std::vector<SqlAuditRecord> _buffer;
};
