using System.Text;
using System.Text.Json;
using SqlProxy.Models;

namespace SqlProxy.Services;

public class FileLogService
{
    private readonly string _logFilePath;
    private readonly object _lock = new();
    private readonly List<SqlAuditRecord> _buffer = [];

    public FileLogService(string logFilePath)
    {
        _logFilePath = logFilePath;
        EnsureLogDirectory();
    }

    private void EnsureLogDirectory()
    {
        var dir = Path.GetDirectoryName(_logFilePath);
        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
        {
            Directory.CreateDirectory(dir);
        }
    }

    public void Log(SqlAuditRecord record)
    {
        lock (_lock)
        {
            _buffer.Add(record);
            WriteToFile(record);
        }
    }

    public List<SqlAuditRecord> GetBufferedRecords()
    {
        lock (_lock)
        {
            return [.. _buffer];
        }
    }

    public void ClearBuffer()
    {
        lock (_lock)
        {
            _buffer.Clear();
        }
    }

    public void ClearFile()
    {
        lock (_lock)
        {
            try
            {
                if (File.Exists(_logFilePath))
                {
                    File.WriteAllText(_logFilePath, string.Empty);
                }
            }
            catch { }
        }
    }

    private void WriteToFile(SqlAuditRecord record)
    {
        try
        {
            var json = JsonSerializer.Serialize(record);
            var line = $"{record.Timestamp}|{json}";
            File.AppendAllText(_logFilePath, line + Environment.NewLine);
        }
        catch { }
    }
}
