using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SqlProxy.Models;
using SqlProxy.Services;

namespace SqlProxy;

public class SqlProxyWindowsService : BackgroundService
{
    private readonly ILogger<SqlProxyWindowsService> _logger;
    private readonly AppConfig _config;
    private readonly FileLogService _fileLog;
    private readonly DatabaseUploadService _dbUpload;
    private readonly TnsProxyService _proxy;
    private Timer? _idleTimer;

    public SqlProxyWindowsService(
        ILogger<SqlProxyWindowsService> logger,
        AppConfig config,
        FileLogService fileLog,
        DatabaseUploadService dbUpload,
        TnsProxyService proxy)
    {
        _logger = logger;
        _config = config;
        _fileLog = fileLog;
        _dbUpload = dbUpload;
        _proxy = proxy;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("SQL Proxy Service starting on {Address}:{Port}", _config.ListenAddress, _config.ListenPort);

        _idleTimer = new Timer(async _ => await CheckIdleAsync(), null, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(30));

        try
        {
            await _proxy.StartAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Proxy service error");
        }
    }

    public override async Task StopAsync(CancellationToken cancellationToken)
    {
        _logger.LogInformation("SQL Proxy Service stopping...");

        _idleTimer?.Dispose();
        _proxy.Stop();

        await base.StopAsync(cancellationToken);
        _logger.LogInformation("SQL Proxy Service stopped");
    }

    private async Task CheckIdleAsync()
    {
        try
        {
            var idle = DateTime.Now - _proxy.LastActivity;
            if (idle.TotalMinutes >= _config.FlushIntervalMinutes)
            {
                _logger.LogInformation("Application idle for {Minutes:F0} minutes, flushing logs...", idle.TotalMinutes);
                var records = _fileLog.GetBufferedRecords();
                if (records.Count > 0)
                {
                    await _dbUpload.UploadToDatabaseAsync(records);
                    _fileLog.ClearBuffer();
                    _fileLog.ClearFile();
                }
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Idle check failed");
        }
    }
}
