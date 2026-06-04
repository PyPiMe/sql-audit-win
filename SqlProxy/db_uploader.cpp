#include "pch.h"
#include "db_uploader.h"
#include "file_logger.h"

DbUploader::DbUploader(const AuditDbConfig& config) {
    _connStr = BuildConnectionString(config);
}

std::string DbUploader::BuildConnectionString(const AuditDbConfig& config) {
    return "Driver={Oracle in OraClient19Home1};Dbq=" +
        config.Host + ":" + std::to_string(config.Port) + "/" + config.ServiceName +
        ";Uid=" + config.Username + ";Pwd=" + config.Password + ";";
}

int DbUploader::Upload(const std::vector<SqlAuditRecord>& records) {
    if (records.empty()) return 0;
    if (_connStr.empty()) {
        printf("[WARN] No audit database configured\n");
        return 0;
    }

    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    int uploaded = 0;

    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) return 0;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    if (SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) != SQL_SUCCESS) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return 0;
    }

    SQLCHAR connOut[1024] = {};
    SQLSMALLINT connOutLen = 0;
    SQLRETURN ret = SQLDriverConnectA(dbc, (SQLHWND)NULL,
        (SQLCHAR*)_connStr.c_str(), (SQLSMALLINT)_connStr.size(),
        connOut, sizeof(connOut), &connOutLen,
        SQL_DRIVER_NOPROMPT);

    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        printf("[ERROR] DB connection failed\n");
        return 0;
    }

    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return 0;
    }

    const char* sql = "INSERT INTO han_sql_audit_log (wid, timestamp, source_ip, username, sql_text, client_host, client_user) VALUES (?, ?, ?, ?, ?, ?, ?)";
    SQLPrepareA(stmt, (SQLCHAR*)sql, SQL_NTS);

    for (const auto& r : records) {
        SQLLEN widLen = (SQLLEN)r.Wid.size();
        SQLLEN tsLen = (SQLLEN)r.Timestamp.size();
        SQLLEN srcIpLen = r.SourceIp.empty() ? SQL_NULL_DATA : (SQLLEN)r.SourceIp.size();
        SQLLEN userLen = r.Username.empty() ? SQL_NULL_DATA : (SQLLEN)r.Username.size();
        SQLLEN sqlLen = (SQLLEN)r.SqlText.size();
        SQLLEN chostLen = r.ClientHost.empty() ? SQL_NULL_DATA : (SQLLEN)r.ClientHost.size();
        SQLLEN cuserLen = r.ClientUser.empty() ? SQL_NULL_DATA : (SQLLEN)r.ClientUser.size();

        SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 40, 0,
            (SQLPOINTER)r.Wid.c_str(), 0, &widLen);
        SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 40, 0,
            (SQLPOINTER)r.Timestamp.c_str(), 0, &tsLen);
        SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 100, 0,
            (SQLPOINTER)r.SourceIp.c_str(), 0, &srcIpLen);
        SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 30, 0,
            (SQLPOINTER)r.Username.c_str(), 0, &userLen);
        SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4000, 0,
            (SQLPOINTER)r.SqlText.c_str(), 0, &sqlLen);
        SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 100, 0,
            (SQLPOINTER)r.ClientHost.c_str(), 0, &chostLen);
        SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 50, 0,
            (SQLPOINTER)r.ClientUser.c_str(), 0, &cuserLen);

        ret = SQLExecute(stmt);
        if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
            uploaded++;
        }

        SQLFreeStmt(stmt, SQL_RESET_PARAMS);
        SQLFreeStmt(stmt, SQL_CLOSE);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);

    printf("[INFO] Uploaded %d records to database\n", uploaded);
    return uploaded;
}
