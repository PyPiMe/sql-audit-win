#pragma once
#include <string>
#include <vector>
#include "config.h"

struct SqlAuditRecord;

class DbUploader {
public:
    DbUploader(const AuditDbConfig& config);
    int Upload(const std::vector<SqlAuditRecord>& records);

private:
    std::string BuildConnectionString(const AuditDbConfig& config);
    std::string _connStr;
};
