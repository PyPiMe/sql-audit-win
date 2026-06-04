#include "pch.h"
#include "debug_logger.h"

DebugLogger::DebugLogger(const std::string& path, bool enabled)
    : _path(path), _enabled(enabled) {
    if (_enabled) EnsureDirectory();
}

void DebugLogger::EnsureDirectory() {
    size_t lastSlash = _path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string dir = _path.substr(0, lastSlash);
        CreateDirectoryA(dir.c_str(), nullptr);
    }
}

void DebugLogger::Log(const std::string& msg) {
    if (!_enabled) return;

    std::lock_guard<std::mutex> lock(_mutex);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[128];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d|",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::ofstream f(_path, std::ios::app);
    if (f) f << buf << msg << "\n";
}

void DebugLogger::LogHex(const std::string& prefix, const BYTE* data, int len) {
    if (!_enabled || len <= 0) return;

    std::string hex;
    hex.reserve(len * 3);
    for (int i = 0; i < len && i < 256; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", data[i]);
        hex += b;
    }
    Log(prefix + ": " + hex);
}
