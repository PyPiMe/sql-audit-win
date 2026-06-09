# AGENTS.md

## 项目概述

**sql-audit-win** - Oracle数据库专用SQL审计工具，纯C++实现，通过进程注入和Inline Hook技术在OCI库层面捕获SQL语句。

## 技术栈

- **编程语言**: C++ (C++17标准)
- **构建系统**: Visual Studio 2022, MSBuild
- **平台**: Windows 10/11
- **API**: Windows API, ATL
- **数据库**: Oracle OCI API
- **架构**: 32位/64位（需匹配目标进程）

## 项目结构

```
sql-audit-win/
├── SqlProxy/              # 主服务程序
│   ├── main.cpp           # 程序入口点
│   ├── config.json        # 配置文件
│   ├── file_logger.cpp    # 文件日志实现
│   ├── pipe_server.cpp    # Named Pipe服务器
│   ├── injector.cpp       # DLL注入器
│   ├── db_uploader.cpp    # 数据库上传器
│   ├── user_detect.cpp    # 用户检测
│   └── *.h               # 头文件
├── OciHook/               # 注入DLL
│   ├── oci_hook.cpp       # OCI Hook实现
│   ├── named_pipe.cpp     # Named Pipe通信
│   └── *.h               # 头文件
├── build_oci_hook.bat     # 构建脚本
└── README.md              # 项目文档
```

## 核心模块

### SqlProxy (主服务)
- **功能**: 进程扫描、DLL注入、日志管理、数据库上传
- **入口**: `SqlProxy.exe`
- **构建**: `SqlProxy\SqlProxy.vcxproj`
- **依赖**: Windows API, ATL库

### OciHook (注入库)
- **功能**: Hook OCI API，捕获SQL语句
- **入口**: `OciHook.dll`
- **构建**: `OciHook\OciHook.vcxproj`
- **依赖**: Oracle OCI API, Windows API

## 构建配置

### 构建环境
- **Visual Studio**: 2022 (Community/Professional/Enterprise)
- **工作负载**: "使用C++的桌面开发"
- **平台**: Win32 / x64

### 构建命令
```cmd
# 默认构建 (64位Release)
build_oci_hook.bat

# 32位Release
build_oci_hook.bat release win32

# 32位Debug
build_oci_hook.bat debug win32
```

### 输出文件
- `Win32\Release\SqlProxy.exe` - 32位主程序
- `Win32\Release\OciHook.dll` - 32位注入库
- `x64\Release\SqlProxy.exe` - 64位主程序
- `x64\Release\OciHook.dll` - 64位注入库

## 运行模式

### 主要组件
1. **SqlProxy.exe** - 主服务程序
2. **OciHook.dll** - 注入的动态链接库
3. **config.json** - 配置文件

### 运行时依赖
- **Oracle客户端**: PL/SQL Developer, SQL Developer, SQLPlus
- **Windows服务**: NSSM (可选)
- **调试工具**: DebugView (可选)

## 配置系统

### 配置文件位置
- **项目配置**: `SqlProxy/config.json`
- **用户配置**: 通过Named Pipe动态更新

### 关键配置项
```json
{
    "PipeName": "SqlProxyOciHook",
    "LogFilePath": "logs/sql_audit.log",
    "DebugLogPath": "logs/debug.log",
    "FlushIntervalMinutes": 2,
    "AuditDb": {
        "Host": "192.168.1.1",
        "Port": 1521,
        "ServiceName": "your_db_servicename",
        "Username": "your_db_user",
        "Password": "your_password"
    }
}
```

## 核心功能

### SQL捕获机制
- **Hook点**: `OCIStmtPrepare`, `OCIStmtPrepare2`
- **过滤规则**: 100+条内置过滤规则
- **输出格式**: JSON-lines

### 进程注入
- **方法**: `CreateRemoteThread` + `LoadLibrary`
- **架构匹配**: 32位进程注入32位DLL，64位进程注入64位DLL
- **目标进程**: Oracle客户端应用程序

## 安全考虑

### 权限要求
- **最低权限**: 以普通用户权限运行
- **管理员权限**: 进程注入需要管理员权限
- **网络隔离**: 审计服务与数据库网络隔离

### 数据保护
- **传输加密**: 使用Oracle OCI加密连接
- **日志安全**: JSON格式日志，便于审计
- **访问控制**: 基于Windows用户身份

## 开发指南

### 添加新的过滤规则
- **位置**: `OciHook/oci_hook.cpp`
- **数组**: `InternalSqlPatterns`
- **格式**: C风格字符串，子串匹配

### 调试方法
- **调试日志**: 启用`DebugEnabled`配置项
- **工具**: DebugView查看系统消息
- **日志**: `StartupLogPath`诊断启动问题

## 性能优化

### 内存管理
- **批处理**: 定期批量上传SQL记录
- **日志轮转**: 支持日志文件大小限制
- **内存泄漏**: 定期检查注入DLL内存使用

### 网络优化
- **连接池**: 复用Oracle数据库连接
- **压缩传输**: 可选压缩上传数据
- **重试机制**: 网络故障自动重试

## 故障排除

### 常见问题
1. **架构不匹配**: 确保注入器与目标进程架构一致
2. **注入失败**: 检查DLL路径和权限
3. **数据库连接**: 验证网络连接和凭证
4. **进程未找到**: 确认Oracle客户端正在运行

### 调试步骤
1. 使用DebugView查看`[OciHook]`消息
2. 检查`StartupLogPath`诊断日志
3. 验证`config.json`配置正确性
4. 确认进程架构匹配

## 扩展性

### 自定义Hook
- **OCI API**: 可以扩展Hook更多OCI函数
- **过滤规则**: 可以添加自定义过滤模式
- **输出格式**: 可以支持其他日志格式

### 集成能力
- **日志系统**: 可与SIEM系统集成
- **监控工具**: 支持Prometheus监控
- **告警系统**: 可配置SQL异常告警