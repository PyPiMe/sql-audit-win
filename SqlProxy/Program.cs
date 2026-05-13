using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SqlProxy;
using SqlProxy.Models;
using SqlProxy.Services;

var isService = OperatingSystem.IsWindows() && Environment.GetCommandLineArgs().Contains("--service");

HostApplicationBuilder builder = Host.CreateApplicationBuilder();

AppConfig config;
var configPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "config.json");
if (File.Exists(configPath))
{
    try
    {
        var json = File.ReadAllText(configPath);
        var options = new System.Text.Json.JsonSerializerOptions
        {
            ReadCommentHandling = System.Text.Json.JsonCommentHandling.Skip
        };
        config = System.Text.Json.JsonSerializer.Deserialize<AppConfig>(json, options) ?? new AppConfig();
    }
    catch (Exception ex)
    {
        Console.WriteLine($"[WARN] Failed to parse config.json: {ex.Message}");
        config = new AppConfig();
    }
}
else
{
    config = new AppConfig();
}

builder.Services.AddSingleton(config);
builder.Services.AddSingleton<DebugLogService>(sp => new DebugLogService(config.DebugLogPath, config.DebugEnabled));
builder.Services.AddSingleton<FileLogService>(sp => new FileLogService(config.LogFilePath));
builder.Services.AddSingleton<DatabaseUploadService>(sp => new DatabaseUploadService(config, sp.GetRequiredService<FileLogService>()));
builder.Services.AddSingleton<FirewallService>(sp => new FirewallService(config, sp.GetRequiredService<DebugLogService>()));
builder.Services.AddSingleton<TnsProxyService>(sp => new TnsProxyService(
    config,
    sp.GetRequiredService<FileLogService>(),
    sp.GetRequiredService<DebugLogService>(),
    sp.GetRequiredService<FirewallService>()));
builder.Services.AddHostedService<SqlProxyWindowsService>();

builder.Logging.AddConsole();

var host = builder.Build();

if (!isService)
{
    Console.WriteLine("=== SQL TAPD Proxy (Windows) ===");
    Console.WriteLine($"[INFO] Listen: {config.ListenAddress}:{config.ListenPort}");
    Console.WriteLine($"[INFO] Debug: {(config.DebugEnabled ? "Enabled" : "Disabled")}");
    if (config.AuditDb != null)
    {
        Console.WriteLine($"[INFO] Backend: {config.AuditDb.Host}:{config.AuditDb.Port}/{config.AuditDb.ServiceName}");
    }
    Console.WriteLine($"[INFO] SQL Log: {config.LogFilePath}");
    Console.WriteLine($"[INFO] Idle upload interval: {config.FlushIntervalMinutes} minutes");
    Console.WriteLine("[INFO] Press Ctrl+C to stop...\n");
}

await host.RunAsync();
