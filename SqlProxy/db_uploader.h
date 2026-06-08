#pragma once
#include <functional>
#include <string>
#include <vector>
#include "config.h"

struct SqlAuditRecord;

class DbUploader {
public:
    using Logger = std::function<void(const std::string&)>;

    DbUploader(const AuditDbConfig& config, Logger logger);
    ~DbUploader();
    int Upload(const std::vector<SqlAuditRecord>& records);

private:
    bool Connect();
    void Disconnect();
    bool Execute(const SqlAuditRecord& r);

    std::string _server;
    std::string _user;
    std::string _pwd;
    Logger _log;

    HMODULE _hOci;
    bool _ociReady;

    void* _envhp;
    void* _errhp;
    void* _svchp;
    void* _srvhp;
    void* _stmthp;
};
