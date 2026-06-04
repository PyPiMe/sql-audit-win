#include "pch.h"
#include "oci_hook.h"
#include "named_pipe.h"

typedef unsigned int ub4;
typedef unsigned char oratext;
typedef int sword;
typedef void dvoid;

static const int HOOK_JMP_LEN = 5;

struct HookInfo {
    void* targetFunc;    // address in oci.dll
    void* hookFunc;      // our hook function
    uint8_t saved[5];
    bool installed;
};

static HMODULE g_ociModule = nullptr;
static HookInfo g_hooks[3];
static CRITICAL_SECTION g_hookCs;

extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtPrepare(
    dvoid* stmtp, dvoid* errhp, oratext* stmt, ub4 stmt_len, ub4 language, ub4 mode);
extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtPrepare2(
    dvoid* svchp, dvoid** stmtp, dvoid* errhp, oratext* stmt, ub4 stmt_len,
    oratext* key, ub4 key_len, ub4 language, ub4 mode);
extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtExecute(
    dvoid* svchp, dvoid* stmtp, dvoid* errhp, ub4 iters, ub4 rowoff,
    void* snap_in, void* snap_out, ub4 mode);

static bool IsInternalSql(const std::string& sql);

static void WriteJmp32(uint8_t* dst, void* target) {
    dst[0] = 0xE9;
    *(int32_t*)(dst + 1) = (int32_t)((uint8_t*)target - dst - 5);
}

static bool PatchJmp(HookInfo& h) {
    DWORD old;
    if (!VirtualProtect(h.targetFunc, HOOK_JMP_LEN, PAGE_EXECUTE_READWRITE, &old))
        return false;
    WriteJmp32((uint8_t*)h.targetFunc, h.hookFunc);
    VirtualProtect(h.targetFunc, HOOK_JMP_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), h.targetFunc, HOOK_JMP_LEN);
    return true;
}

static void RestoreOriginal(HookInfo& h) {
    DWORD old;
    VirtualProtect(h.targetFunc, HOOK_JMP_LEN, PAGE_EXECUTE_READWRITE, &old);
    memcpy(h.targetFunc, h.saved, HOOK_JMP_LEN);
    VirtualProtect(h.targetFunc, HOOK_JMP_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), h.targetFunc, HOOK_JMP_LEN);
}

bool InstallHooks() {
    g_ociModule = GetModuleHandleA("oci.dll");
    if (!g_ociModule) {
        OutputDebugStringA("[OciHook] ERROR: oci.dll not loaded\n");
        return false;
    }
    OutputDebugStringA("[OciHook] Found oci.dll module\n");

    InitializeCriticalSection(&g_hookCs);

    void* prepare   = GetProcAddress(g_ociModule, "OCIStmtPrepare");
    void* prepare2  = GetProcAddress(g_ociModule, "OCIStmtPrepare2");
    void* execute   = GetProcAddress(g_ociModule, "OCIStmtExecute");

    g_hooks[0] = { prepare,   (void*)Hook_OCIStmtPrepare,  {}, false };
    g_hooks[1] = { prepare2,  (void*)Hook_OCIStmtPrepare2, {}, false };
    g_hooks[2] = { execute,   (void*)Hook_OCIStmtExecute,  {}, false };

    bool any = false;
    for (auto& h : g_hooks) {
        if (!h.targetFunc) continue;
        memcpy(h.saved, h.targetFunc, HOOK_JMP_LEN);
        if (PatchJmp(h)) {
            h.installed = true;
            any = true;
        } else {
            OutputDebugStringA("[OciHook] Failed to patch\n");
        }
    }
    if (any) OutputDebugStringA("[OciHook] Hooks installed\n");
    return any;
}

void RemoveHooks() {
    for (auto& h : g_hooks) {
        if (!h.installed || !h.targetFunc) continue;
        RestoreOriginal(h);
        h.installed = false;
    }
    DeleteCriticalSection(&g_hookCs);
}

static void SendSqlToPipe(const char* stmt, uint32_t stmt_len) {
    if (!stmt || stmt_len == 0) return;

    std::string sql(stmt, stmt_len);
    while (!sql.empty() && (sql.back() == ' ' || sql.back() == '\n' || sql.back() == '\r' || sql.back() == '\0'))
        sql.pop_back();

    for (auto& c : sql) {
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    }

    size_t pos = 0;
    while ((pos = sql.find("  ", pos)) != std::string::npos)
        sql.erase(pos, 1);

    if (sql.empty()) return;

    if (IsInternalSql(sql)) {
        OutputDebugStringA(("[OciHook] Filtered: " + sql.substr(0, 120) + "\n").c_str());
        return;
    }

    char userBuf[256] = {};
    DWORD sz = sizeof(userBuf);
    GetUserNameA(userBuf, &sz);

    OutputDebugStringA(("[OciHook] Captured: " + sql.substr(0, 200) + "\n").c_str());
    NamedPipeClient::Get().SendSqlMessage(userBuf, sql);
}

extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtPrepare(
    dvoid* stmtp, dvoid* errhp, oratext* stmt, ub4 stmt_len, ub4 language, ub4 mode) {
    SendSqlToPipe((const char*)stmt, stmt_len);

    typedef sword(__cdecl *Func)(dvoid*, dvoid*, oratext*, ub4, ub4, ub4);
    Func real = (Func)g_hooks[0].targetFunc;

    EnterCriticalSection(&g_hookCs);
    RestoreOriginal(g_hooks[0]);
    sword ret = real(stmtp, errhp, stmt, stmt_len, language, mode);
    PatchJmp(g_hooks[0]);
    LeaveCriticalSection(&g_hookCs);
    return ret;
}

extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtPrepare2(
    dvoid* svchp, dvoid** stmtp, dvoid* errhp, oratext* stmt, ub4 stmt_len,
    oratext* key, ub4 key_len, ub4 language, ub4 mode) {
    SendSqlToPipe((const char*)stmt, stmt_len);

    typedef sword(__cdecl *Func)(dvoid*, dvoid**, dvoid*, oratext*, ub4, oratext*, ub4, ub4, ub4);
    Func real = (Func)g_hooks[1].targetFunc;

    EnterCriticalSection(&g_hookCs);
    RestoreOriginal(g_hooks[1]);
    sword ret = real(svchp, stmtp, errhp, stmt, stmt_len, key, key_len, language, mode);
    PatchJmp(g_hooks[1]);
    LeaveCriticalSection(&g_hookCs);
    return ret;
}

extern "C" __declspec(dllexport) sword __cdecl Hook_OCIStmtExecute(
    dvoid* svchp, dvoid* stmtp, dvoid* errhp, ub4 iters, ub4 rowoff,
    void* snap_in, void* snap_out, ub4 mode) {
    typedef sword(__cdecl *Func)(dvoid*, dvoid*, dvoid*, ub4, ub4, void*, void*, ub4);
    Func real = (Func)g_hooks[2].targetFunc;

    EnterCriticalSection(&g_hookCs);
    RestoreOriginal(g_hooks[2]);
    sword ret = real(svchp, stmtp, errhp, iters, rowoff, snap_in, snap_out, mode);
    PatchJmp(g_hooks[2]);
    LeaveCriticalSection(&g_hookCs);
    return ret;
}

// ---- SQL Filter Patterns ----
static const char* InternalSqlPatterns[] = {
    "sys.dbms_transaction.local_transaction_id",
    "sys.dbms_session.",
    "sys.dbms_application_info.",
    "sys.dbms_output.",
    "sys.dbms_alert.",
    "sys.dbms_pipe.",
    "sys.dbms_defer.",
    "sys.dbms_lock.",
    "sys.dbms_metadata.",
    "sys.dbms_describe.",
    "sys.dbms_preprocessor.",
    "sys.dbms_standard.",
    "sys.dbms_registry.",
    "sys.all_mviews",
    "sys.all_objects",
    "sys.all_tables",
    "sys.all_views",
    "sys.all_tab_columns",
    "sys.all_tab_cols",
    "sys.all_tab_comments",
    "sys.all_col_comments",
    "sys.all_indexes",
    "sys.all_ind_columns",
    "sys.all_ind_expressions",
    "sys.all_ind_partitions",
    "sys.all_constraints",
    "sys.all_cons_columns",
    "sys.all_sequences",
    "sys.all_synonyms",
    "sys.all_procedures",
    "sys.all_arguments",
    "sys.all_triggers",
    "sys.all_source",
    "sys.all_types",
    "sys.all_type_attrs",
    "sys.all_type_methods",
    "sys.all_part_tables",
    "sys.all_tab_partitions",
    "sys.all_part_key_columns",
    "sys.all_tab_privs",
    "sys.all_tab_privs_made",
    "sys.all_col_privs",
    "sys.all_db_links",
    "sys.all_users",
    "sys.all_catalog",
    "sys.all_directories",
    "sys.all_java_classes",
    "sys.all_policies",
    "sys.all_scheduler_jobs",
    "sys.v$session",
    "sys.v$transaction",
    "sys.v$instance",
    "sys.v$parameter",
    "sys.v$nls_parameters",
    "sys.v$open_cursor",
    "sys.v$mystat",
    "sys.v$version",
    "sys.v$database",
    "sys.v$sql",
    "sys.v$lock",
    "sys.v$access",
    "sys.v$enabledprivs",
    "v$session",
    "v$transaction",
    "v$instance",
    "v$parameter",
    "v$nls_parameters",
    "v$open_cursor",
    "v$mystat",
    "v$version",
    "v$database",
    "v$sql",
    "v$lock",
    "sys_context(",
    "alter session set",
    "set plsql_code_type",
    "set plscope_settings",
    "select 'x' from dual",
    "select null from dual",
    "select user from dual",
    "length(chr",
    "lengthb(nchr",
    "lengthbnchr",
    "all_synonyms",
    "dba_synonyms",
    "sys.all_queue_tables",
    "sys.all_external_tables",
    "sys.all_external_locations",
    "all_external_tables",
    "all_external_locations",
    "PLSQLDEV_SAVEPOINT",
    "sys.plsqldev_authorization",
    "v$statname",
    "han_sql_audit_log",
};

static bool IsInternalSql(const std::string& sql) {
    std::string upper;
    upper.reserve(sql.size());
    for (char c : sql) upper += (char)toupper((unsigned char)c);
    for (const auto* pattern : InternalSqlPatterns) {
        std::string patUpper;
        for (const char* p = pattern; *p; p++)
            patUpper += (char)toupper((unsigned char)*p);
        if (upper.find(patUpper) != std::string::npos)
            return true;
    }
    return false;
}

static DWORD WINAPI HookThreadProc(LPVOID) {
    OutputDebugStringA("[OciHook] Hook thread waiting for oci.dll...\n");
    for (int i = 0; i < 60; i++) {
        if (InstallHooks()) return 0;
        Sleep(500);
    }
    OutputDebugStringA("[OciHook] TIMEOUT: oci.dll not found\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringA("[OciHook] DLL loaded\n");
        NamedPipeClient::Get().Initialize("\\\\.\\pipe\\SqlProxyOciHook");
        CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        RemoveHooks();
        NamedPipeClient::Get().Shutdown();
        break;
    }
    return TRUE;
}
