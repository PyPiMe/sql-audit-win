[English](README-EN.md)

# sql-audit-win

> **Oracle 数据库专用 SQL 审计工具** — 纯 C++ 实现，通过进程注入 + Inline Hook 在 OCI 库层面捕获 SQL。无需修改 Oracle 客户端，无 .NET 依赖。
>
> **仅支持 Oracle 数据库。** 本工具通过 Hook Oracle 专有的 `OCIStmtPrepare`/`OCIStmtPrepare2` API 实现 SQL 拦截，不支持 SQL Server、MySQL、PostgreSQL 等其他数据库。

---

## 架构

```
┌─────────────────┐
│  SqlProxy.exe   │  ← C++ 审计服务
│  · Named Pipe   │
│  · 进程扫描      │
│  · DLL 注入      │──── CreateRemoteThread ──┐
│  · 文件日志      │                          │
│  · ODBC 上传    │                          ▼
└────────┬────────┘    ┌──────────────────────────────┐     ┌──────────────────────┐
         │ Named Pipe  │  OciHook.dll (注入到目标进程)  │     │  Oracle Database     │
         └─────────────│  Inline Hook:                 │────▶│  Server              │
                       │  OCIStmtPrepare / Prepare2    │     └──────────────────────┘
                       └──────────────────────────────┘
                        PL/SQL Developer / SQLPlus 进程内
```

**工作流程：**
1. `SqlProxy.exe` 启动 Named Pipe 服务器
2. 每 3 秒扫描进程，发现 Oracle 客户端（plsqldev.exe 等）
3. `CreateRemoteThread` + `LoadLibrary` 注入 `OciHook.dll`
4. DLL 内轮询线程等待 `oci.dll` 加载后，Inline Hook `OCIStmtPrepare`/`OCIStmtPrepare2`
5. 拦截到的 SQL 经 100+ 条规则过滤后，通过 Named Pipe 发送回服务
6. 服务写入 JSON-lines 日志，空闲时通过 ODBC 批量上传至数据库

---

## ⚠️ 32 位 / 64 位架构匹配

**注入器和目标进程必须架构一致！** 64 位进程不能注入 32 位 DLL，反之亦然。

| Oracle 客户端 | 进程架构 | 编译命令 |
|--------------|---------|---------|
| PL/SQL Developer | **32 位 (Win32)** | `build_oci_hook.bat release win32` |
| SQL Developer 64-bit | 64 位 | `build_oci_hook.bat` |
| SQLPlus | 查看任务管理器标记 | 对应架构 |

**查看方式：** 任务管理器 → 详细信息 → 看进程名是否带 `(32 位)` 标记。

---

## 编译

**前置条件：** Visual Studio 2022 + "使用 C++ 的桌面开发" 工作负载

```cmd
build_oci_hook.bat                      # 64 位 Release（默认）
build_oci_hook.bat release win32        # 32 位 Release
build_oci_hook.bat debug win32          # 32 位 Debug
```

输出：`Win32\Release\` 或 `x64\Release\`

---

## 部署

只需将 3 个文件复制到同一目录（如 `C:\Tools\SqlProxy\`）：

```cmd
xcopy Win32\Release\SqlProxy.exe C:\Tools\SqlProxy\
xcopy Win32\Release\OciHook.dll  C:\Tools\SqlProxy\
xcopy SqlProxy\config.json       C:\Tools\SqlProxy\
```

**不需要部署到 Oracle 客户端目录，不修改任何 Oracle 文件。**

---

## 配置 (`config.json`)

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

| 参数 | 说明 |
|------|------|
| `PipeName` | Named Pipe 名称 |
| `LogFilePath` | SQL 审计日志路径 (JSON-lines) |
| `DebugLogPath` | 调试日志路径 |
| `DebugEnabled` | 启用调试日志 |
| `FlushIntervalMinutes` | 空闲多少分钟后批量上传至数据库 |
| `StartupLogPath` | 启动诊断日志路径（空字符串禁用） |
| `AuditDb` | Oracle 审计数据库连接信息 |

### ODBC 驱动

数据库连接通过 Windows ODBC 实现，需安装 Oracle ODBC 驱动。连接字符串格式：

```
Driver={Oracle in OraClient19Home1};Dbq=Host:Port/ServiceName;Uid=User;Pwd=Password;
```

如果 ODBC 驱动名不同，修改 `db_uploader.cpp` 中 `BuildConnectionString` 的驱动名。

---

## 运行

```cmd
C:\Tools\SqlProxy\SqlProxy.exe              # 控制台模式
C:\Tools\SqlProxy\SqlProxy.exe --service    # NSSM 服务模式
```

### 安装为 Windows 服务 (NSSM)

下载 NSSM：https://nssm.cc/download

```cmd
C:\Tools\nssm\win64\nssm.exe install SqlProxy
```

NSSM 窗口中设置：
- **Path:** `C:\Tools\SqlProxy\SqlProxy.exe`
- **Startup directory:** `C:\Tools\SqlProxy`
- **Arguments:** `--service`

```cmd
net start SqlProxy
```

**服务自动恢复：**

```cmd
nssm set SqlProxy AppExit Default Restart
nssm set SqlProxy AppThrottle 5000
```

---

## SQL 过滤规则

`OciHook.dll` 内置 100+ 条过滤规则，命中以下类别的 SQL 不记录：

| 类别 | 数量 | 示例 |
|------|------|------|
| DBMS 内部包 | 13 | `sys.dbms_session.*`, `sys.dbms_metadata.*` |
| IDE 对象浏览器 | 40+ | `sys.all_tables`, `sys.all_source`, `sys.all_external_locations` |
| V$ 动态视图 | 22 | `v$session`, `v$statname` |
| PL/SQL Developer 内部 | 3 | `sys.plsqldev_authorization`, `PLSQLDEV_SAVEPOINT` |
| 会话/驱动配置 | 5 | `alter session set`, `sys_context(`, `set plsql_code_type` |
| 审计表自身 | 1 | `han_sql_audit_log` |
| 探针查询 | 4 | `select 'x' from dual`, `select user from dual` |

过滤规则位于 `OciHook/oci_hook.cpp` 的 `InternalSqlPatterns` 数组，大小写不敏感子串匹配。

---

## 数据库表结构

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

| 字段 | 来源 |
|------|------|
| `wid` | 自动生成 GUID |
| `timestamp` | 捕获时间 `yyyy-MM-dd HH:mm:ss` |
| `source_ip` | 审计服务所在机器 IP |
| `username` | 执行 SQL 的 Windows 用户名 |
| `sql_text` | 完整 SQL 文本 |
| `client_host` | 机器名 |
| `client_user` | 交互登录用户（WTS API 检测） |

---

## 排错

**服务启动后立即退出：** 检查 `StartupLogPath` 配置的诊断日志。

**无 SQL 记录：** 用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 检查：
1. 管理员运行 → Capture → Capture Global Win32
2. 查看 `[OciHook]` 前缀消息
   - 无消息 → 注入失败（架构不匹配或 DLL 路径错误）
   - `TIMEOUT` → oci.dll 未加载
   - `Captured` 但无文件记录 → Named Pipe 不通

**ODBC 连接失败：** 确认 Oracle ODBC 驱动已安装且驱动名与代码一致。

---

## 许可证

Apache License 2.0
