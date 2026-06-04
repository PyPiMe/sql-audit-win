#include "pch.h"
#include "file_logger.h"

static std::string GenerateGuid() {
    GUID guid;
    CoCreateGuid(&guid);
    char buf[64];
    snprintf(buf, sizeof(buf), "%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

static std::string GetTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c); out += buf; }
                else out += c;
                break;
        }
    }
    return out;
}

FileLogger::FileLogger(const std::string& logPath) : _logPath(logPath) {
    EnsureDirectory();
}

void FileLogger::EnsureDirectory() {
    size_t lastSlash = _logPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string dir = _logPath.substr(0, lastSlash);
        CreateDirectoryA(dir.c_str(), nullptr);
    }
}

void FileLogger::Log(const SqlAuditRecord& record) {
    std::lock_guard<std::mutex> lock(_mutex);
    _buffer.push_back(record);

    std::string json = "{";
    json += "\"wid\":\"" + record.Wid + "\",";
    json += "\"timestamp\":\"" + record.Timestamp + "\",";
    json += "\"source_ip\":\"" + EscapeJson(record.SourceIp) + "\",";
    json += "\"username\":\"" + EscapeJson(record.Username) + "\",";
    json += "\"sql_text\":\"" + EscapeJson(record.SqlText) + "\",";
    json += "\"client_host\":\"" + EscapeJson(record.ClientHost) + "\",";
    json += "\"client_user\":\"" + EscapeJson(record.ClientUser) + "\"";
    json += "}";

    std::string line = record.Timestamp + "|" + json + "\n";

    std::ofstream f(_logPath, std::ios::app);
    if (f) f << line;
}

std::vector<SqlAuditRecord> FileLogger::GetBufferedRecords() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _buffer;
}

void FileLogger::ClearBuffer() {
    std::lock_guard<std::mutex> lock(_mutex);
    _buffer.clear();
}

void FileLogger::ClearFile() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::ofstream f(_logPath, std::ios::trunc);
}
