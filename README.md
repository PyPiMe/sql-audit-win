# sql-audit-win

功能简介：在windows环境下，监控应用程序（如PL/SQL Developer），使用Oracle数据库，发出的SQL语句。并记录在数据库中。无需Oracle启用审计。

# 功能架构
┌─────────────────┐    ┌─────────────┐    ┌────────────────────────────┐
│ PL/SQL Developer│────▶│ SQL代理     │────▶│ Oralce 11g数据库服务器 │
└─────────────────┘    └─────────────┘    └────────────────────────────┘
                              │
                        ┌─────▼──────┐
                        │ 日志服务    │
                        │ 记录SQL     │
                        └────────────┘
# 使用方法
1. 下载源码，自行build。
    ```
        dotnet restore
        dotnet publish -c Release
    ```
2. 按照config.json配置进行配置。
3. 添加服务。
- 下载 NSSM
    下载地址：https://nssm.cc/download
    选择 nssm-2.24-101-g897c7ad.zip
    解压到 C:\Tools\nssm或任意目录
- 使用 NSSM 安装服务
    C:\Tools\nssm\win64\nssm.exe install SqlProxy
    然后在弹出的窗口中设置：
    Path: C:\Tools\SqlProxy\SqlProxy.exe
    Startup directory: C:\Tools\SqlProxy
    Arguments: --service
- 启动服务
    net start SqlProxy
4. 应用程序的TNSNAME.ORA配置类似下面结构。注意SERVICE_NAME和config.json配置中的ServiceName保持一致。
    ```
        (DESCRIPTION =
            (ADDRESS = (PROTOCOL = TCP)(HOST = 127.0.0.1)(PORT = 1521))
        (CONNECT_DATA =
            (SERVER = DEDICATED)
            (SERVICE_NAME = han)
            )
        )
    ```
5. config.json说明
    ```
    {
        "ListenAddress": "127.0.0.1", //应用程序监听配置的IP，无需更改
        "ListenPort": 1521, //应用程序监听端口，无需更改
        "DebugLogPath": "logs/debug.log", //debug文件路径
        "DebugEnabled": true, //debug启用开关
        "LogFilePath": "logs/sql_audit.log", //本地日志文件路径
        "FlushIntervalMinutes": 10, //10分钟后无动作开始上传本地日志
        "AuditDb": {
            "Host": "192.168.1.1", //实际数据库的IP
            "Port": 1521, //实际数据库监听端口
            "ServiceName": "your_db_servicename", //实际数据库的servicename
            "Username": "your_db_user", //登录名
            "Password": "your_password" //密码
        }
    }
    ```
# 使用 Open Code 开发
## 提示词
1. 使用C#编写SQL代理程序，该程序使用类似透明代理的方式，记录应用程序发出的SQL文本并记录，需要尽可能提取完整的SQL文本。
2. 程序实际流程如下图所示。步骤如下：
- 应用程序PL/SQL Developer使用下面的字符串连接，这个字符串配置在TNSNAME.ORA中，不在config.json配置文件中。
    ```
        (DESCRIPTION =
            (ADDRESS = (PROTOCOL = TCP)(HOST = 127.0.0.1)(PORT = 1521))
            (CONNECT_DATA =
                (SERVER = DEDICATED)
                (SERVICE_NAME = servicetestname)
            )
        )
    ```
- 即上面的连接字符串指向SQL代理程序。此时PL/SQL Developer输入的帐号密码，不作为连接实际数据库的帐号信息，可以随意填写。
- SQL代理程序截获这个请求，并用config.json配置文件中的配置连接数据库。
    ```
    "AuditDb": {
        "Host": "192.168.4.1",
        "Port": 1522,
        "ServiceName": "servicetestname",
        "Username": "user",
        "Password": "userpwd"
    },
    ```
- SQL代理程序使用时，按照配置文件中的信息，按照下面构建标准连接字符串。
    ```
        (DESCRIPTION =
            (ADDRESS = (PROTOCOL = TCP)(HOST = 192.168.4.1)(PORT = 1522))
            (CONNECT_DATA =
                (SERVER = DEDICATED)
                (SERVICE_NAME = urpjw)
            )
        )
    ```
- SQL代理程序记录通过应用程序PL/SQL Developer发出的所有SQL文本。SQL关键词包括"SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "ALTER", "DROP", "EXEC", "CALL" 。
- SQL代理程序将实际数据库运行结构返回给应用程序PL/SQL Developer供其使用。
3. 配置文件要求：
- config.json配置文件中读取日志文件的配置，包括路径和日志文件名。
- config.json配置文件中设置调试开关参数，并将相关日志输出到一个调试日志的配置，包括路径和日志文件名。若调试开关参数开启，需要完整记录日志。
4. 现在本地生成文件日志，当监测应用程序10分钟（可以在配置文件中调整）无任何动作后，将日志上传到数据库；
- 数据库为Oracle
- 数据库表已存在，表名 han_sql_audit_log ，表结构如下
    ```
        CREATE TABLE han_sql_audit_log (
            wid VARCHAR2(40) not null,
            timestamp VARCHAR2(40),
            source_ip VARCHAR2(100),
            username VARCHAR2(30),
            sql_text VARCHAR2(4000),
            client_host VARCHAR2(100),
            client_user VARCHAR2(50)
        );
        CREATE INDEX idx_audit_time ON han_sql_audit_log(timestamp);
        CREATE INDEX idx_audit_user ON han_sql_audit_log(username);
        CREATE INDEX idx_audit_ip ON han_sql_audit_log(source_ip);
        -- Add comments to the columns 
        comment on column HAN_SQL_AUDIT_LOG.wid is 'wid';
        comment on column HAN_SQL_AUDIT_LOG.timestamp is '提交时间';
        comment on column HAN_SQL_AUDIT_LOG.source_ip is '提交IP';
        comment on column HAN_SQL_AUDIT_LOG.username is '提交用户';
        comment on column HAN_SQL_AUDIT_LOG.sql_text is 'SQL文本';
        comment on column HAN_SQL_AUDIT_LOG.client_host is '提交主机名';
        comment on column HAN_SQL_AUDIT_LOG.client_user is '提交主机用户';
    ```
- wid 字段由SQL代理程序使用GUID生成
- timestamp 字段，为SQL代理程序根据捕获的SQL文本的时间生成，格式为YYYY-MM-DD HH:MM:SS
- source_ip 字段，为SQL代理程序获取程序运行的主机的真实IP
- username 字段，为config.json配置文件中"Username"的值
- client_host 字段，为SQL代理程序获取程序运行的主机的主机名
- client_user 字段，为SQL代理程序获取程序运行的主机的用户名（windows登录用户）
- 本地文件日志，也需要按照表中要求字段记录
5. 每次完成上传后，删除本地文件日志内容，避免数据重复。
6. 程序提供windows服务模式启动，参数“--service”
## 调试
- KVM下windows虚机进行调试
- SDK为 dotnet-sdk-10.0.201-win-x64.exe
