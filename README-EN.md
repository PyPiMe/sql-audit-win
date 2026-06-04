[中文](README.md)

# sql-audit-win

> **SQL Audit for Oracle on Windows via OCI API Hook** — Pure C++. Process injection + inline hook captures pristine SQL at the OCI library level. No Oracle client modification, no .NET runtime, zero external dependencies.

---

## Architecture

```
┌─────────────────┐
│  SqlProxy.exe   │  ← C++ audit service
│  · Named Pipe   │
│  · Process watch│
│  · DLL injector │──── CreateRemoteThread ──┐
│  · File log     │                          │
│  · ODBC upload  │                          ▼
└────────┬────────┘    ┌──────────────────────────────┐     ┌──────────────────────┐
         │ Named Pipe  │  OciHook.dll (injected)       │     │  Oracle Database     │
         └─────────────│  Inline Hook:                 │────▶│  Server              │
                       │  OCIStmtPrepare / Prepare2    │     └──────────────────────┘
                       └──────────────────────────────┘
                        Inside PL/SQL Developer / SQLPlus
```

**How it works:**
1. `SqlProxy.exe` starts a Named Pipe server
2. Scans processes every 3 seconds for Oracle clients (plsqldev.exe, etc.)
3. Injects `OciHook.dll` via `CreateRemoteThread` + `LoadLibrary`
4. A polling thread inside the DLL waits for `oci.dll` to load, then installs inline hooks on `OCIStmtPrepare`/`OCIStmtPrepare2`
5. Captured SQL passes through 100+ filter rules, then sent back via Named Pipe
6. The service writes JSON-lines logs and periodically uploads via ODBC

---

## ⚠️ 32-bit vs 64-bit Architecture Matching

**The injector and target process must share the same architecture!** A 64-bit process cannot inject a 32-bit DLL, and vice versa.

| Oracle Client | Architecture | Build Command |
|--------------|-------------|---------------|
| PL/SQL Developer | **32-bit (Win32)** | `build_oci_hook.bat release win32` |
| SQL Developer 64-bit | 64-bit | `build_oci_hook.bat` |
| SQLPlus | Check Task Manager | Match architecture |

**How to check:** Task Manager → Details → look for `(32 bit)` suffix on the process name.

---

## Build

**Prerequisites:** Visual Studio 2022 + "Desktop development with C++" workload

```cmd
build_oci_hook.bat                      # 64-bit Release (default)
build_oci_hook.bat release win32        # 32-bit Release
build_oci_hook.bat debug win32          # 32-bit Debug
```

Output: `Win32\Release\` or `x64\Release\`

---

## Deploy

Copy 3 files to a single directory (e.g. `C:\Tools\SqlProxy\`):

```cmd
xcopy Win32\Release\SqlProxy.exe C:\Tools\SqlProxy\
xcopy Win32\Release\OciHook.dll  C:\Tools\SqlProxy\
xcopy SqlProxy\config.json       C:\Tools\SqlProxy\
```

**No deployment to Oracle client directories. No Oracle files are modified.**

---

## Configuration (`config.json`)

```json
{
    "PipeName": "SqlProxyOciHook",
    "LogFilePath": "logs/sql_audit.log",
    "DebugLogPath": "logs/debug.log",
    "DebugEnabled": true,
    "FlushIntervalMinutes": 2,
    "StartupLogPath": "logs/startup.log",
    "AuditDb": {
        "Host": "192.168.1.1",
        "Port": 1521,
        "ServiceName": "your_db_servicename",
        "Username": "your_db_user",
        "Password": "your_password"
    }
}
```

| Parameter | Description |
|-----------|-------------|
| `PipeName` | Named Pipe name |
| `LogFilePath` | SQL audit log path (JSON-lines) |
| `DebugLogPath` | Debug log path |
| `DebugEnabled` | Enable debug logging |
| `FlushIntervalMinutes` | Idle minutes before batch upload to DB |
| `StartupLogPath` | Startup diagnostic log path (empty to disable) |
| `AuditDb` | Oracle audit database connection info |

### ODBC Driver

Database connectivity uses Windows ODBC. An Oracle ODBC driver must be installed. Connection string format:

```
Driver={Oracle in OraClient19Home1};Dbq=Host:Port/ServiceName;Uid=User;Pwd=Password;
```

If your ODBC driver name differs, update `BuildConnectionString` in `db_uploader.cpp`.

---

## Run

```cmd
C:\Tools\SqlProxy\SqlProxy.exe              # Console mode
C:\Tools\SqlProxy\SqlProxy.exe --service    # NSSM service mode
```

### Install as Windows Service (NSSM)

Download NSSM: https://nssm.cc/download

```cmd
C:\Tools\nssm\win64\nssm.exe install SqlProxy
```

In the NSSM window:
- **Path:** `C:\Tools\SqlProxy\SqlProxy.exe`
- **Startup directory:** `C:\Tools\SqlProxy`
- **Arguments:** `--service`

```cmd
net start SqlProxy
```

**Auto-restart on failure:**

```cmd
nssm set SqlProxy AppExit Default Restart
nssm set SqlProxy AppThrottle 5000
```

---

## SQL Filtering

`OciHook.dll` includes 100+ filter rules. SQL matching any of the following categories is discarded:

| Category | Count | Examples |
|----------|-------|----------|
| DBMS internal packages | 13 | `sys.dbms_session.*`, `sys.dbms_metadata.*` |
| IDE object browser | 40+ | `sys.all_tables`, `sys.all_source`, `sys.all_external_locations` |
| V$ dynamic views | 22 | `v$session`, `v$statname` |
| PL/SQL Developer internal | 3 | `sys.plsqldev_authorization`, `PLSQLDEV_SAVEPOINT` |
| Session/driver config | 5 | `alter session set`, `sys_context(`, `set plsql_code_type` |
| Audit table self-queries | 1 | `han_sql_audit_log` |
| Probe queries | 4 | `select 'x' from dual`, `select user from dual` |

Filters use case-insensitive substring matching and are defined in `OciHook/oci_hook.cpp` under `InternalSqlPatterns`.

---

## Database Schema

```sql
CREATE TABLE han_sql_audit_log (
    wid          VARCHAR2(40)  NOT NULL,
    timestamp    VARCHAR2(40),
    source_ip    VARCHAR2(100),
    username     VARCHAR2(30),
    sql_text     VARCHAR2(4000),
    client_host  VARCHAR2(100),
    client_user  VARCHAR2(50)
);

CREATE INDEX idx_audit_time ON han_sql_audit_log(timestamp);
CREATE INDEX idx_audit_user ON han_sql_audit_log(username);
```

| Column | Source |
|--------|--------|
| `wid` | Auto-generated GUID |
| `timestamp` | Capture time `yyyy-MM-dd HH:mm:ss` |
| `source_ip` | Audit service machine IP |
| `username` | Windows username executing the SQL |
| `sql_text` | Full SQL statement text |
| `client_host` | Machine name |
| `client_user` | Interactive console user (WTS API) |

---

## Troubleshooting

**Service exits immediately:** Check the `StartupLogPath` diagnostic log.

**No SQL records:** Use [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview):
1. Run as Administrator → Capture → Capture Global Win32
2. Look for `[OciHook]` prefixed messages
   - No messages → injection failed (architecture mismatch or DLL path error)
   - `TIMEOUT` → oci.dll not loaded
   - `Captured` but no file output → Named Pipe connection failed

**ODBC connection failure:** Verify Oracle ODBC driver is installed and the driver name matches the code.

---

## License

Apache License 2.0
