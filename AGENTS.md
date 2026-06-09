# sql-audit-win

Oracle数据库SQL审计工具，纯C++17，通过进程注入+Inline Hook在OCI库层面捕获SQL。Windows-only，需Visual Studio 2022编译。

## 项目结构

- `SqlProxy/` — 主服务：进程扫描、DLL注入、日志管理、OCI上传
- `OciHook/` — 注入DLL：Inline Hook `OCIStmtPrepare`/`OCIStmtPrepare2`，Named Pipe回传SQL
- `build_oci_hook.bat` — 统一构建脚本
- `SqlProxy/config.json` — 运行时配置

## 构建命令

```cmd
build_oci_hook.bat                       # 64位 Release（默认）
build_oci_hook.bat release win32         # 32位 Release
build_oci_hook.bat debug win32           # 32位 Debug
```

输出：`Win32\Release\` 或 `x64\Release\`，含 `SqlProxy.exe` + `OciHook.dll`

当前平台为Linux，无法执行构建。

## 关键约定与陷阱

- **架构必须匹配**：注入器和目标进程位数必须一致。32位进程注入32位DLL，64位进程注入64位DLL。PL/SQL Developer 是32位，SQL Developer 64-bit 是64位。
- **添加过滤规则**：编辑 `OciHook/oci_hook.cpp` 中 `InternalSqlPatterns` 数组，大小写不敏感子串匹配。
- **调试手段**：启用 `config.json` 中 `DebugEnabled`，用 DebugView 捕获 `[OciHook]` 前缀消息。

## 验证步骤

1. 确认 `SqlProxy/config.json` 配置正确
2. 编译目标架构版本
3. 目标机器启动目标Oracle客户端进程
4. 启动 `SqlProxy.exe`，检查日志文件是否有SQL记录
