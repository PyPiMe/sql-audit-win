namespace SqlProxy.Services;

public class DebugLogService
{
    private readonly string _logFilePath;
    private readonly bool _enabled;
    private readonly object _lock = new();

    public DebugLogService(string logFilePath, bool enabled)
    {
        _logFilePath = logFilePath;
        _enabled = enabled;
        if (_enabled)
        {
            EnsureLogDirectory();
        }
    }

    private void EnsureLogDirectory()
    {
        var dir = Path.GetDirectoryName(_logFilePath);
        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
        {
            Directory.CreateDirectory(dir);
        }
    }

    public void Log(string message)
    {
        if (!_enabled) return;

        lock (_lock)
        {
            try
            {
                var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}|{message}";
                File.AppendAllText(_logFilePath, line + Environment.NewLine);
            }
            catch { }
        }
    }

    public void LogHex(string prefix, byte[] data, int length)
    {
        if (!_enabled) return;

        var actualLength = Math.Min(length, data.Length);
        var hex = BitConverter.ToString(data.Take(actualLength).ToArray()).Replace("-", " ");
        Log($"{prefix}: {hex}");
    }
}
