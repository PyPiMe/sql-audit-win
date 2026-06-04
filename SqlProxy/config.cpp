#include "pch.h"
#include "config.h"

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string StripComments(const std::string& json) {
    std::string result;
    result.reserve(json.size());
    for (size_t i = 0; i < json.size(); i++) {
        if (json[i] == '/' && i + 1 < json.size() && json[i + 1] == '/') {
            while (i < json.size() && json[i] != '\n') i++;
            if (i < json.size()) result += '\n';
        } else {
            result += json[i];
        }
    }
    return result;
}

static std::string ExtractString(const std::string& json, size_t& pos) {
    if (json[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    if (pos < json.size()) pos++;
    return result;
}

static void SkipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;
}

static int ExtractInt(const std::string& json, size_t& pos) {
    std::string num;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9' || json[pos] == '-'))
        num += json[pos++];
    return num.empty() ? 0 : std::stoi(num);
}

static bool ExtractBool(const std::string& json, size_t& pos) {
    if (json.compare(pos, 4, "true") == 0) { pos += 4; return true; }
    if (json.compare(pos, 5, "false") == 0) { pos += 5; return false; }
    return false;
}

static void ParseObject(const std::string& json, size_t& pos, AppConfig& config) {
    if (json[pos] != '{') return;
    pos++;

    while (pos < json.size()) {
        SkipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == '}') { pos++; return; }

        std::string key = ExtractString(json, pos);
        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') pos++;
        SkipWhitespace(json, pos);

        if (key == "PipeName") config.PipeName = ExtractString(json, pos);
        else if (key == "LogFilePath") config.LogFilePath = ExtractString(json, pos);
        else if (key == "DebugLogPath") config.DebugLogPath = ExtractString(json, pos);
        else if (key == "DebugEnabled") config.DebugEnabled = ExtractBool(json, pos);
        else if (key == "FlushIntervalMinutes") config.FlushIntervalMinutes = ExtractInt(json, pos);
        else if (key == "StartupLogPath") config.StartupLogPath = ExtractString(json, pos);
        else if (key == "AuditDb") {
            SkipWhitespace(json, pos);
            if (json[pos] == '{') {
                pos++;
                while (pos < json.size()) {
                    SkipWhitespace(json, pos);
                    if (pos >= json.size() || json[pos] == '}') { pos++; break; }
                    std::string subKey = ExtractString(json, pos);
                    SkipWhitespace(json, pos);
                    if (pos < json.size() && json[pos] == ':') pos++;
                    SkipWhitespace(json, pos);
                    if (subKey == "Host") config.AuditDb.Host = ExtractString(json, pos);
                    else if (subKey == "Port") config.AuditDb.Port = ExtractInt(json, pos);
                    else if (subKey == "ServiceName") config.AuditDb.ServiceName = ExtractString(json, pos);
                    else if (subKey == "Username") config.AuditDb.Username = ExtractString(json, pos);
                    else if (subKey == "Password") config.AuditDb.Password = ExtractString(json, pos);
                    else { ExtractString(json, pos); }
                    SkipWhitespace(json, pos);
                    if (pos < json.size() && json[pos] == ',') pos++;
                }
            }
        }
        else { if (json[pos] == '"') ExtractString(json, pos); }

        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') pos++;
    }
}

AppConfig LoadConfig(const std::string& path) {
    AppConfig config;
    std::string raw = ReadFile(path);
    if (raw.empty()) return config;

    std::string clean = StripComments(raw);
    size_t pos = 0;
    ParseObject(clean, pos, config);

    return config;
}
