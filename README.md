# sql-audit-win

> **SQL Audit Proxy for Oracle on Windows** — Transparently intercepts SQL statements from Oracle client applications (e.g. PL/SQL Developer), records them locally and uploads to an audit table. No Oracle built-in auditing required.
>
> **Windows 环境下的 Oracle SQL 审计代理** — 透明拦截 Oracle 客户端（如 PL/SQL Developer）发出的 SQL 语句，记录到本地文件并上传审计表，无需 Oracle 启用审计。

---

## Architecture / 功能架构

```
┌─────────────────┐     ┌──────────────┐     ┌──────────────────────┐
│ PL/SQL Developer│────▶│  SQL Proxy   │────▶│  Oracle Database     │
│  (Oracle Client)│     │  (MITM Agent)│     │  Server              │
└─────────────────┘     └──────────────┘     └──────────────────────┘
                               │
                         ┌─────▼──────┐
                         │ Log Service │
                         │ (File + DB) │
                         └────────────┘
```

---

## Features / 功能特性

| Feature / 功能 | Description / 描述 |
|---|---|
| **SQL Capture / SQL 捕获** | Extracts full SQL text from TNS stream; filters Oracle driver/IDE internal calls; cleans TNS binary artifacts |
| **Login User Extraction / 登录用户提取** | Parses TNS OAUTH packets to capture the actual PL/SQL Developer login username (stored in `username` field) |
| **Windows User Detection / Windows 用户识别** | Uses WTS API + explorer.exe token fallback to get the interactive Windows user even when running as SYSTEM service (stored in `client_user` field) |
| **Firewall Bypass Prevention / 防火墙绕过拦截** | Auto-creates Windows Firewall outbound rules to block known Oracle clients (plsqldev.exe, sqlplus.exe, etc.) from direct access to Oracle server; periodic re-scan to catch newly started processes |
| **TNS Redirect Handling / TNS 重定向处理** | Handles Oracle RAC SCAN IP redirects automatically |
| **Dual Logging / 双重日志** | Local JSON-lines file + automatic upload to Oracle audit table; local file is cleared after upload to avoid duplicates |
| **Windows Service Support / 服务模式** | Supports `--service` flag for headless operation via NSSM |

---

## Table Schema / 数据库表结构

```sql
CREATE TABLE han_sql_audit_log (
    wid          VARCHAR2(40)  not null,      -- GUID
    timestamp    VARCHAR2(40),                 -- capture time (YYYY-MM-DD HH:MM:SS)
    source_ip    VARCHAR2(100),                -- proxy host real IP
    username     VARCHAR2(30),                 -- PL/SQL Developer login username (from TNS OAUTH)
    sql_text     VARCHAR2(4000),               -- captured SQL text
    client_host  VARCHAR2(100),                -- proxy host machine name
    client_user  VARCHAR2(50)                  -- interactive Windows login user (via WTS API)
);

CREATE INDEX idx_audit_time ON han_sql_audit_log(timestamp);
CREATE INDEX idx_audit_user ON han_sql_audit_log(username);
CREATE INDEX idx_audit_ip  ON han_sql_audit_log(source_ip);

COMMENT ON COLUMN han_sql_audit_log.wid          IS 'wid / 唯一标识';
COMMENT ON COLUMN han_sql_audit_log.timestamp    IS 'capture time / 提交时间';
COMMENT ON COLUMN han_sql_audit_log.source_ip    IS 'proxy IP / 提交IP';
COMMENT ON COLUMN han_sql_audit_log.username     IS 'PL/SQL Developer login user / 提交用户';
COMMENT ON COLUMN han_sql_audit_log.sql_text     IS 'SQL text / SQL文本';
COMMENT ON COLUMN han_sql_audit_log.client_host  IS 'host name / 提交主机名';
COMMENT ON COLUMN han_sql_audit_log.client_user  IS 'Windows login user / 提交主机用户';
```

### Field Description / 字段说明

| Field / 字段 | Source / 来源 |
|---|---|
| `wid` | Auto-generated GUID by the proxy |
| `timestamp` | Capture time, format `yyyy-MM-dd HH:mm:ss` |
| `source_ip` | Real IP of the machine running the proxy (detected via UDP socket) |
| `username` | **PL/SQL Developer login username** extracted from TNS OAUTH `AUTH_TERMINAL` context; falls back to config's `Username` if TNS parsing fails |
| `sql_text` | Full SQL statement text |
| `client_host` | `Environment.MachineName` — the proxy host machine name |
| `client_user` | **Interactive Windows login user** — queried via `WTSGetActiveConsoleSessionId` + `WTSQuerySessionInformation`, falls back to explorer.exe process owner detection, then `Environment.UserName` |

---

## Configuration / 配置文件 (`config.json`)

```json
{
    "ListenAddress": "127.0.0.1",
    "ListenPort": 1521,
    "DebugLogPath": "logs/debug.log",
    "DebugEnabled": true,
    "LogFilePath": "logs/sql_audit.log",
    "FlushIntervalMinutes": 10,
    "PersistFirewallRules": false,
    "AuditDb": {
        "Host": "192.168.1.1",
        "Port": 1521,
        "ServiceName": "your_db_servicename",
        "Username": "your_db_user",
        "Password": "your_password"
    }
}
```

| Parameter / 参数 | Description / 说明 | Notes |
|---|---|---|
| `ListenAddress` | Proxy listen IP | Usually `127.0.0.1` |
| `ListenPort` | Proxy listen port | Usually `1521` (Oracle default) |
| `DebugLogPath` | Debug log file path | e.g. `logs/debug.log` |
| `DebugEnabled` | Enable debug logging | `true` or `false` |
| `LogFilePath` | SQL audit log file path | e.g. `logs/sql_audit.log` |
| `FlushIntervalMinutes` | Idle time before uploading to DB | Default `10` minutes |
| `PersistFirewallRules` | Keep firewall rules after proxy stops | `false` (remove on stop), `true` (keep) |
| `AuditDb.Host` | Real Oracle database IP | e.g. `192.168.1.1` |
| `AuditDb.Port` | Real Oracle database port | e.g. `1521` |
| `AuditDb.ServiceName` | Oracle service name | Must match the service name in tnsnames.ora |
| `AuditDb.Username` | Database account for upload | Used to connect to the audit database |
| `AuditDb.Password` | Database password | |

Note: `config.json` supports `//` style comments (parsed with `JsonCommentHandling.Skip`).

---

## Setup Guide / 使用指南

### 1. Build / 编译

```cmd
dotnet restore
dotnet publish -c Release
```

### 2. Configure / 配置

Edit `config.json` with your Oracle server details and credentials.

### 3. Install as Windows Service / 安装为服务 (optional)

Download NSSM from [nssm.cc/download](https://nssm.cc/download) (choose `nssm-2.24-101-g897c7ad.zip`), then:

```cmd
C:\Tools\nssm\win64\nssm.exe install SqlProxy
```

Set in the NSSM window:
- **Path:** `C:\Tools\SqlProxy\SqlProxy.exe`
- **Startup directory:** `C:\Tools\SqlProxy`
- **Arguments:** `--service`

```cmd
net start SqlProxy
```

**Service Recovery / 服务自动恢复**

To prevent users from stopping the proxy service (which would disable auditing), configure NSSM to automatically restart it:

```cmd
nssm set SqlProxy AppExit Default Restart
nssm set SqlProxy AppThrottle 5000
```

This ensures that even if someone stops the service via `sc stop`, `net stop`, or kills the process, NSSM will restart it within 5 seconds. No code changes required.

若要防止用户关闭代理服务导致审计失效，配置 NSSM 使其在服务退出后自动重启。无论通过 `sc stop`、`net stop` 还是 `taskkill` 停止服务，NSSM 都会在 5 秒内自动拉起，无需修改任何代码。

### 4. Client tnsnames.ora / 客户端配置

Configure your Oracle client (e.g. PL/SQL Developer) to connect to the proxy instead of the real database. The `SERVICE_NAME` must match the `ServiceName` in `config.json`.

```
(DESCRIPTION =
    (ADDRESS = (PROTOCOL = TCP)(HOST = 127.0.0.1)(PORT = 1521))
    (CONNECT_DATA =
        (SERVER = DEDICATED)
        (SERVICE_NAME = han)
    )
)
```

> **Note / 注意:** The username and password entered in PL/SQL Developer's login dialog **will be captured** in the `username` field (extracted from TNS OAUTH). The actual database authentication uses the credentials from `config.json`.

> **Important / 重要:** The proxy automatically blocks known Oracle client processes (plsqldev.exe, sqlplus.exe, etc.) from connecting directly to the database server via Windows Firewall. This prevents bypassing the audit. Set `PersistFirewallRules: true` to keep the block rules after the proxy stops.

---

## How It Works / 工作原理

1. **PL/SQL Developer** connects to `127.0.0.1:1521` (the proxy) via tnsnames.ora
2. **SQL Proxy** accepts the connection, connects to the real Oracle database using config credentials
3. **TNS Handshake** is relayed transparently; RAC SCAN redirects are handled automatically
4. **Login User Extraction**: The proxy parses the TNS OAUTH authentication packet, extracts the DB username (the value preceding `AUTH_TERMINAL`), and records it in the `username` field
5. **SQL Capture & Filtering / SQL 捕获与过滤**: Every client-to-backend data packet is scanned for SQL keywords; extracted SQL passes through a three-layer filter chain (see [SQL Filtering & Cleanup](#sql-filtering--cleanup-sql-过滤与清洗))
6. **Windows User Detection**: `client_user` is obtained via `WTSGetActiveConsoleSessionId` → `WTSQuerySessionInformation`, falling back to explorer.exe owner token lookup, then `Environment.UserName`
7. **Logging**: SQL records are written to a JSON-lines file; after a configurable idle period (default 10 min), buffered records are uploaded to `han_sql_audit_log` in Oracle, then the local file is cleared
8. **Bypass Prevention**: On startup, the proxy scans running processes for known Oracle clients and adds Windows Firewall block rules targeting their full executable paths. A periodic re-scan (every 30s) catches newly started processes. When the proxy stops, these rules are removed (unless `PersistFirewallRules: true`).

---

## Firewall Rule Management / 防火墙规则管理

The proxy uses per-process Windows Firewall rules (not a global port block) to prevent Oracle client applications from connecting directly to the database:

- **On startup**: Scans running processes for known clients (plsqldev.exe, sqlplus.exe, sqldeveloper.exe, toad.exe, etc.), adds outbound block rules targeting each executable path
- **Every 30s**: Re-scans for new client processes and adds rules for any previously unseen executables (already-blocked processes are skipped to avoid redundant firewall operations and log spam)
- **On shutdown**: Removes all rules by default (controlled by `PersistFirewallRules`)

This avoids the Windows Firewall limitation where "block" rules always win over "allow" rules.

---

## SQL Filtering & Cleanup / SQL 过滤与清洗

The proxy employs a three-layer defense to ensure only user-written application SQL is recorded in the audit log:

### Layer 1: Internal SQL Pattern Filtering / 内部SQL模式过滤

A hardcoded blocklist of **70+ patterns** covers Oracle driver/IDE internal calls that are never user-written:

| Category / 类别 | Examples / 示例 | Count |
|---|---|---|
| DBMS packages | `sys.dbms_transaction.local_transaction_id`, `sys.dbms_session.*`, `sys.dbms_application_info.*`, `sys.dbms_output.*`, `sys.dbms_metadata.*`, `sys.dbms_describe.*` | 11 |
| IDE object browser queries | `sys.all_tables`, `sys.all_views`, `sys.all_tab_columns`, `sys.all_indexes`, `sys.all_constraints`, `sys.all_procedures`, `sys.all_source`, `sys.all_types`, `sys.all_mviews`, etc. | 28 |
| V$ dynamic views | `sys.v$session`, `sys.v$transaction`, `sys.v$instance`, `sys.v$parameter`, `sys.v$open_cursor`, `sys.v$sql`, etc. | 11 |
| Session configuration | `alter session set`, `sys_context(` | 2 |

These are issued by Oracle drivers (ODAC/ODP.NET), connection pools, and IDE tools (PL/SQL Developer, TOAD) for internal housekeeping — they are not user business logic and are silently discarded.

### Layer 2: TTC Contamination Detection / TTC协议污染检测

Oracle's TTC (Two-Task Common) protocol frames are carried within TNS DATA packets. When TTC control bytes happen to fall in the ASCII `A-Z` range, they survive binary stripping and fuse with adjacent column names into long, unbroken uppercase-letter runs:

```
User SQL:       ... wid , ywwid , xnxqdm , kch , ...
TTC bytes:               Y      W      Y
Result:         ... widYWWIDXNXQDMKCH...
```

The proxy detects this by scanning for the longest continuous sequence of uppercase letters + digits without any separator characters (space, comma, underscore, etc.). If such a run exceeds **20 characters**, the SQL is classified as **TTC-contaminated** and discarded — no amount of string cleanup can reliably recover the original identifiers.

### Layer 3: TNS Artifact Cleanup / TNS二进制残留清洗

Two passes clean residual TNS binary artifacts from the extracted SQL text:

| Pass | Method | Purpose |
|---|---|---|
| **Embedded artifact removal** | `RemoveEmbeddedArtifactAt` | Strips `@` characters that are sandwiched between SQL identifiers or structural characters (`=`, `,`, `(`, `)`, `;`, space). e.g. `JH.@PYFADM` → `JH.PYFADM`, `10@;` → `10;` |
| **Trailing artifact truncation** | `TrimTrailingTnsArtifacts` | Detects repeating `@T`/`@@` patterns (both direct and space-separated) and truncates everything from the first occurrence. e.g. `...select ... from dual @ T @ T ...` → `...select ... from dual` |

> **Note:** Some TTC-level corruption is irrecoverable at the string level (e.g., TTC control bytes overwriting `(` or `=` characters). The proxy's filtering strategy is to **discard contaminated SQL** rather than record malformed text.

### Known Limitations / 已知局限

#### TTC Protocol-Level Character Corruption

Oracle's TTC (Two-Task Common) protocol frames are interleaved with SQL text within TNS DATA packets. When TTC control bytes overwrite SQL structural characters, the resulting text is permanently corrupted:

| Original SQL | Corrupted (recorded) | Lost char |
|---|---|---|
| `nvl(a.kcxzdm, '9999')` | `nvla.kcxzdm,'9999')` | `(` |
| `on a.pyfadm = b.pyfadm` | `on a.pyfadmb.pyfadm` | `=`, `(` |
| `order by njdm desc, c.pyfamc` | `order by njdm descpyfam c` | `,`, `.` |

This corruption is **not recoverable** at the string-cleanup level because:
- The TTC protocol is Oracle-proprietary with no official specification
- Third-party TTC parsers (Wireshark, sqlmap) cover only a subset of protocol versions
- A production-grade TTC parser would require ongoing maintenance for each Oracle release

The proxy mitigates this by:
1. **`IsContaminatedSql`** — Discarding SQL with runs of >= 20 contiguous uppercase letters (TTC frame contamination signal)
2. **`InternalSqlPatterns`** — Filtering known driver/IDE internal calls before they reach the log

If fully pristine SQL recording is required, consider **Oracle Fine-Grained Auditing (DBMS_FGA)** as an alternative to MITM proxying.

---

## Development Environment / 开发环境

- **Language:** C# (.NET 10.0-windows)
- **SDK:** dotnet-sdk-10.0.201
- **IDE:** Open Code
- **Debug VM:** Windows 10 Enterprise LTSC 2021 (KVM)
- **Database:** Oracle

---

## Dependencies / 依赖 (NuGet)

| Package | Version | Purpose |
|---|---|---|
| `Microsoft.Extensions.Hosting` | 8.0.0 | DI / BackgroundService |
| `Microsoft.Extensions.Logging` | 8.0.0 | Logging abstractions |
| `Microsoft.Extensions.Logging.Console` | 8.0.0 | Console logger |
| `Oracle.ManagedDataAccess.Core` | 23.5.1 | Oracle data access |

---

## License / 许可证

Apache License 2.0
