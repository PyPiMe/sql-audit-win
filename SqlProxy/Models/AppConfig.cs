namespace SqlProxy.Models;

public class AppConfig
{
    public string ListenAddress { get; set; } = "127.0.0.1";
    public int ListenPort { get; set; } = 1521;
    public string DebugLogPath { get; set; } = "logs/debug.log";
    public bool DebugEnabled { get; set; } = true;
    public string LogFilePath { get; set; } = "logs/sql_audit.log";
    public int FlushIntervalMinutes { get; set; } = 10;
    public AuditDbConfig? AuditDb { get; set; }
}

public class AuditDbConfig
{
    public string Host { get; set; } = "";
    public int Port { get; set; } = 1521;
    public string ServiceName { get; set; } = "";
    public string Username { get; set; } = "";
    public string Password { get; set; } = "";
}
