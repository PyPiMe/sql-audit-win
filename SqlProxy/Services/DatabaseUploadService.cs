using System.Text;
using Oracle.ManagedDataAccess.Client;
using SqlProxy.Models;

namespace SqlProxy.Services;

public class DatabaseUploadService
{
    private readonly string _connectionString;
    private readonly FileLogService _fileLog;

    public DatabaseUploadService(AppConfig config, FileLogService fileLog)
    {
        if (config.AuditDb != null && !string.IsNullOrEmpty(config.AuditDb.Username))
        {
            _connectionString = BuildConnectionString(config.AuditDb);
        }
        else
        {
            _connectionString = "";
        }
        _fileLog = fileLog;
    }

    private static string BuildConnectionString(AuditDbConfig config)
    {
        var password = config.Password.Replace(";", "\\;");
        return $"Data Source=(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST={config.Host})(PORT={config.Port}))(CONNECT_DATA=(SERVICE_NAME={config.ServiceName})));User Id={config.Username};Password={password};";
    }

    public async Task<int> UploadToDatabaseAsync(List<SqlAuditRecord> records)
    {
        if (records.Count == 0) return 0;
        if (string.IsNullOrEmpty(_connectionString))
        {
            Console.WriteLine("[WARN] No audit database configured");
            return 0;
        }

        int uploaded = 0;
        var sql = new StringBuilder();
        sql.AppendLine("INSERT INTO han_sql_audit_log (wid, timestamp, source_ip, username, sql_text, client_host, client_user)");
        sql.AppendLine("VALUES (:wid, :timestamp, :sourceIp, :username, :sqlText, :clientHost, :clientUser)");

        try
        {
            await using var conn = new OracleConnection(_connectionString);
            await conn.OpenAsync();

            await using var cmd = new OracleCommand(sql.ToString(), conn);
            cmd.CommandType = System.Data.CommandType.Text;

            var pWid = cmd.Parameters.Add(":wid", OracleDbType.Varchar2, 40);
            var pTimestamp = cmd.Parameters.Add(":timestamp", OracleDbType.Varchar2, 40);
            var pSourceIp = cmd.Parameters.Add(":sourceIp", OracleDbType.Varchar2);
            var pUsername = cmd.Parameters.Add(":username", OracleDbType.Varchar2);
            var pSqlText = cmd.Parameters.Add(":sqlText", OracleDbType.Varchar2, 4000);
            var pClientHost = cmd.Parameters.Add(":clientHost", OracleDbType.Varchar2);
            var pClientUser = cmd.Parameters.Add(":clientUser", OracleDbType.Varchar2);

            foreach (var record in records)
            {
                try
                {
                    pWid.Value = record.Wid;
                    pTimestamp.Value = record.Timestamp;
                    pSourceIp.Value = record.SourceIp ?? (object)DBNull.Value;
                    pUsername.Value = record.Username ?? (object)DBNull.Value;
                    pSqlText.Value = TruncateSql(record.SqlText, 4000);
                    pClientHost.Value = record.ClientHost ?? (object)DBNull.Value;
                    pClientUser.Value = record.ClientUser ?? (object)DBNull.Value;

                    await cmd.ExecuteNonQueryAsync();
                    uploaded++;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[WARN] Failed to upload record: {ex.Message}");
                }
            }

            Console.WriteLine($"[INFO] Uploaded {uploaded} records to database");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ERROR] Database upload failed: {ex.Message}");
        }

        return uploaded;
    }

    private static string TruncateSql(string? sql, int maxLength)
    {
        if (string.IsNullOrEmpty(sql)) return string.Empty;
        return sql.Length <= maxLength ? sql : sql[..maxLength];
    }
}
