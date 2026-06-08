#include "pch.h"
#include "db_uploader.h"
#include "file_logger.h"

typedef int       sword;
typedef unsigned  ub4;
typedef unsigned  ub2;
typedef void      dvoid;
typedef unsigned char oratext;

#define OCI_DEFAULT              0
#define OCI_THREADED             1
#define OCI_HTYPE_ENV            1
#define OCI_HTYPE_ERROR          2
#define OCI_HTYPE_SVCCTX         3
#define OCI_HTYPE_STMT           4
#define OCI_HTYPE_SERVER         8
#define OCI_HTYPE_SESSION        9
#define OCI_SUCCESS              0
#define OCI_SUCCESS_WITH_INFO    1
#define OCI_COMMIT_ON_SUCCESS    0
#define OCI_ATTR_SERVER          6
#define OCI_ATTR_SESSION         7
#define OCI_ATTR_USERNAME        22
#define OCI_ATTR_PASSWORD        23
#define OCI_CRED_RDBMS           1
#define OCI_NTV_SYNTAX           1

typedef sword (*OCIInitialize_t)(ub4, dvoid*, dvoid*(*)(dvoid*,size_t), dvoid*(*)(dvoid*,dvoid*,size_t), void(*)(dvoid*,dvoid*));
typedef sword (*OCIEnvCreate_t)(dvoid**, ub4, dvoid*, dvoid*(*)(dvoid*,size_t), dvoid*(*)(dvoid*,dvoid*,size_t), void(*)(dvoid*,dvoid*), size_t, dvoid**);
typedef sword (*OCIHandleAlloc_t)(dvoid*, dvoid**, ub4, size_t, dvoid**);
typedef sword (*OCIHandleFree_t)(dvoid*, ub4);
typedef sword (*OCIServerAttach_t)(dvoid*, dvoid*, oratext*, signed int, ub4);
typedef sword (*OCISessionBegin_t)(dvoid*, dvoid*, dvoid*, ub4, ub4);
typedef sword (*OCIAttrSet_t)(dvoid*, ub4, dvoid*, ub4, ub4, dvoid*);
typedef sword (*OCIStmtPrepare_t)(dvoid*, dvoid*, oratext*, ub4, ub4, ub4);
typedef sword (*OCIStmtExecute_t)(dvoid*, dvoid*, dvoid*, ub4, ub4, dvoid*, dvoid*, ub4);
typedef sword (*OCIBindByName_t)(dvoid*, dvoid**, dvoid*, oratext*, signed int, dvoid*, signed int, ub2, dvoid*, ub2*, ub2*, ub4, ub4*, ub4);
typedef sword (*OCITransCommit_t)(dvoid*, dvoid*, ub4);
typedef sword (*OCIServerDetach_t)(dvoid*, dvoid*, ub4);
typedef sword (*OCISessionEnd_t)(dvoid*, dvoid*, dvoid*, ub4);
typedef sword (*OCIErrorGet_t)(dvoid*, ub4, oratext*, signed int*, oratext*, ub4, ub4);

#define OCI_FUNC(name) static name##_t p##name = nullptr

OCI_FUNC(OCIInitialize);
OCI_FUNC(OCIEnvCreate);
OCI_FUNC(OCIHandleAlloc);
OCI_FUNC(OCIHandleFree);
OCI_FUNC(OCIServerAttach);
OCI_FUNC(OCISessionBegin);
OCI_FUNC(OCIAttrSet);
OCI_FUNC(OCIStmtPrepare);
OCI_FUNC(OCIStmtExecute);
OCI_FUNC(OCIBindByName);
OCI_FUNC(OCITransCommit);
OCI_FUNC(OCIServerDetach);
OCI_FUNC(OCISessionEnd);
OCI_FUNC(OCIErrorGet);

static bool g_ociLoaded = false;

static bool LoadOci() {
    if (g_ociLoaded) return true;
    HMODULE h = LoadLibraryA("oci.dll");
    if (!h) {
        h = LoadLibraryA("oci.dll"); // try again
        if (!h) return false;
    }

    #define RESOLVE(f) p##f = (f##_t)GetProcAddress(h, #f); if (!p##f) { FreeLibrary(h); return false; }
    RESOLVE(OCIInitialize);
    RESOLVE(OCIEnvCreate);
    RESOLVE(OCIHandleAlloc);
    RESOLVE(OCIHandleFree);
    RESOLVE(OCIServerAttach);
    RESOLVE(OCISessionBegin);
    RESOLVE(OCIAttrSet);
    RESOLVE(OCIStmtPrepare);
    RESOLVE(OCIStmtExecute);
    RESOLVE(OCIBindByName);
    RESOLVE(OCITransCommit);
    RESOLVE(OCIServerDetach);
    RESOLVE(OCISessionEnd);
    RESOLVE(OCIErrorGet);
    #undef RESOLVE

    g_ociLoaded = true;
    return true;
}

static void LogOciError(void* errhp, const std::string& ctx, DbUploader::Logger& log) {
    oratext msg[512];
    sword errcode;
    pOCIErrorGet(errhp, 1, (oratext*)nullptr, &errcode, msg, sizeof(msg), OCI_HTYPE_ERROR);
    log("[DB] " + ctx + ": " + std::string((char*)msg) + " (ORA-" + std::to_string(errcode) + ")");
}

DbUploader::DbUploader(const AuditDbConfig& config, Logger logger) : _log(logger) {
    if (config.Host.find("(DESCRIPTION=") != std::string::npos) {
        _server = config.Host;
    } else {
        _server = "(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=" + config.Host + ")(PORT=" +
            std::to_string(config.Port) + "))(CONNECT_DATA=(SERVICE_NAME=" + config.ServiceName + ")))";
    }
    _user = config.Username;
    _pwd = config.Password;

    _hOci = nullptr;
    _ociReady = false;
    _envhp = nullptr;
    _errhp = nullptr;
    _svchp = nullptr;
    _srvhp = nullptr;
    _stmthp = nullptr;

    if (!LoadOci()) {
        _log("[DB] Failed to load oci.dll");
        return;
    }
    _ociReady = true;
}

DbUploader::~DbUploader() {
    Disconnect();
}

bool DbUploader::Connect() {
    if (!_ociReady) return false;

    sword rc = pOCIInitialize(OCI_THREADED, nullptr, nullptr, nullptr, nullptr);
    if (rc != OCI_SUCCESS && rc != OCI_SUCCESS_WITH_INFO) {
        _log("[DB] OCIInitialize failed: " + std::to_string(rc));
        return false;
    }

    rc = pOCIEnvCreate(&_envhp, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIEnvCreate failed"); return false; }

    rc = pOCIHandleAlloc(_envhp, &_errhp, OCI_HTYPE_ERROR, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIHandleAlloc(ERROR) failed"); return false; }

    rc = pOCIHandleAlloc(_envhp, &_svchp, OCI_HTYPE_SVCCTX, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIHandleAlloc(SVCCTX) failed"); return false; }

    rc = pOCIHandleAlloc(_envhp, &_srvhp, OCI_HTYPE_SERVER, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIHandleAlloc(SERVER) failed"); return false; }

    rc = pOCIServerAttach(_srvhp, _errhp, (oratext*)_server.c_str(), (signed int)_server.size(), OCI_DEFAULT);
    if (rc != OCI_SUCCESS) { LogOciError(_errhp, "ServerAttach", _log); return false; }

    pOCIAttrSet(_svchp, OCI_HTYPE_SVCCTX, _srvhp, 0, OCI_ATTR_SERVER, _errhp);

    void* usrhp = nullptr;
    rc = pOCIHandleAlloc(_envhp, &usrhp, OCI_HTYPE_SESSION, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIHandleAlloc(SESSION) failed"); return false; }

    pOCIAttrSet(usrhp, OCI_HTYPE_SESSION, (dvoid*)_user.c_str(), (ub4)_user.size(), OCI_ATTR_USERNAME, _errhp);
    pOCIAttrSet(usrhp, OCI_HTYPE_SESSION, (dvoid*)_pwd.c_str(), (ub4)_pwd.size(), OCI_ATTR_PASSWORD, _errhp);

    rc = pOCISessionBegin(_svchp, _errhp, usrhp, OCI_CRED_RDBMS, OCI_DEFAULT);
    if (rc != OCI_SUCCESS) { LogOciError(_errhp, "SessionBegin", _log); pOCIHandleFree(usrhp, OCI_HTYPE_SESSION); return false; }

    pOCIAttrSet(_svchp, OCI_HTYPE_SVCCTX, usrhp, 0, OCI_ATTR_SESSION, _errhp);

    rc = pOCIHandleAlloc(_envhp, &_stmthp, OCI_HTYPE_STMT, 0, nullptr);
    if (rc != OCI_SUCCESS) { _log("[DB] OCIHandleAlloc(STMT) failed"); pOCIHandleFree(usrhp, OCI_HTYPE_SESSION); return false; }

    return true;
}

void DbUploader::Disconnect() {
    if (_stmthp) { pOCIHandleFree(_stmthp, OCI_HTYPE_STMT); _stmthp = nullptr; }
    if (_svchp) {
        pOCISessionEnd(_svchp, _errhp, nullptr, OCI_DEFAULT);
        pOCIServerDetach(_srvhp, _errhp, OCI_DEFAULT);
    }
    if (_srvhp) { pOCIHandleFree(_srvhp, OCI_HTYPE_SERVER); _srvhp = nullptr; }
    if (_svchp) { pOCIHandleFree(_svchp, OCI_HTYPE_SVCCTX); _svchp = nullptr; }
    if (_errhp) { pOCIHandleFree(_errhp, OCI_HTYPE_ERROR); _errhp = nullptr; }
    if (_envhp) { pOCIHandleFree(_envhp, OCI_HTYPE_ENV); _envhp = nullptr; }
}

bool DbUploader::Execute(const SqlAuditRecord& r) {
    auto esc = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() * 2);
        for (char c : s) {
            if (c == '\'') out += "''";
            else out += c;
        }
        return out;
    };

    std::string wid = esc(r.Wid);
    std::string ts  = esc(r.Timestamp);
    std::string sip = esc(r.SourceIp);
    std::string usr = esc(r.Username);
    std::string sql = esc(r.SqlText.size() > 4000 ? r.SqlText.substr(0, 4000) : r.SqlText);
    std::string ch  = esc(r.ClientHost);
    std::string cu  = esc(r.ClientUser);

    std::string stmt = "INSERT INTO han_sql_audit_log(wid,timestamp,source_ip,username,sql_text,client_host,client_user) VALUES('"
        + wid + "','" + ts + "','" + sip + "','" + usr + "','" + sql + "','" + ch + "','" + cu + "')";

    sword rc = pOCIStmtPrepare(_stmthp, _errhp, (oratext*)stmt.c_str(), (ub4)stmt.size(), OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (rc != OCI_SUCCESS) { LogOciError(_errhp, "StmtPrepare", _log); return false; }

    rc = pOCIStmtExecute(_svchp, _stmthp, _errhp, 1, 0, nullptr, nullptr, OCI_COMMIT_ON_SUCCESS);
    return rc == OCI_SUCCESS || rc == OCI_SUCCESS_WITH_INFO;
}

int DbUploader::Upload(const std::vector<SqlAuditRecord>& records) {
    if (records.empty()) return 0;
    if (!_ociReady) return 0;

    if (!Connect()) {
        Disconnect();
        return 0;
    }

    int uploaded = 0;
    for (const auto& r : records) {
        if (Execute(r)) uploaded++;
        else LogOciError(_errhp, "Insert", _log);
    }

    pOCITransCommit(_svchp, _errhp, OCI_DEFAULT);
    Disconnect();

    _log("[DB] Uploaded " + std::to_string(uploaded) + "/" + std::to_string(records.size()) + " records");
    return uploaded;
}
