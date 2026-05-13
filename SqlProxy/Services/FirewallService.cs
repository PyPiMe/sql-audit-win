using System.Diagnostics;
using System.Net;
using SqlProxy.Models;

namespace SqlProxy.Services;

public class FirewallService
{
    private readonly string _blockIp;
    private readonly int _oraclePort;
    private readonly DebugLogService _debugLog;
    private const string RulePrefix = "SqlProxy_Block_";

    private static readonly string[] KnownOracleClients =
    [
        "plsqldev.exe", "sqlplus.exe", "sqlplusw.exe",
        "sqldeveloper.exe", "sqldeveloper64W.exe", "toad.exe",
        "datagrip64.exe", "datagrip.exe", "dbeaver.exe"
    ];

    public FirewallService(AppConfig config, DebugLogService debugLog)
    {
        _oraclePort = config.AuditDb?.Port ?? 1521;
        _debugLog = debugLog;
        _blockIp = ResolveToIp(config.AuditDb?.Host);
    }

    public void SetupRules()
    {
        if (string.IsNullOrEmpty(_blockIp))
        {
            _debugLog.Log("[FIREWALL] No Oracle host configured, skipping");
            return;
        }

        try
        {
            DeleteExistingRules();
            var blocked = BlockKnownClients();

            if (blocked > 0)
                Console.WriteLine($"[INFO] Firewall: blocked {blocked} Oracle client(s) from direct access to {_blockIp}:{_oraclePort}");
            else
                Console.WriteLine($"[INFO] Firewall: no running Oracle clients found to block");
        }
        catch (Exception ex)
        {
            _debugLog.Log($"[FIREWALL] Error: {ex.Message}");
            Console.WriteLine($"[WARN] Firewall setup failed: {ex.Message}");
        }
    }

    public void RemoveRules()
    {
        try
        {
            DeleteExistingRules();
            Console.WriteLine("[INFO] Firewall rules removed");
        }
        catch { }
    }

    public int ScanAndBlock()
    {
        if (string.IsNullOrEmpty(_blockIp)) return 0;
        return BlockKnownClients();
    }

    private int BlockKnownClients()
    {
        var found = FindClientExes();
        foreach (var path in found)
        {
            var exeName = Path.GetFileNameWithoutExtension(path);
            var ruleName = $"{RulePrefix}{exeName}";
            RunNetsh($"advfirewall firewall delete rule name=\"{ruleName}\"");
            var args = $"advfirewall firewall add rule name=\"{ruleName}\" dir=out protocol=TCP remoteport={_oraclePort} remoteip={_blockIp} action=block program=\"{path}\"";
            RunNetsh(args);
            _debugLog.Log($"[FIREWALL] Blocked: {path} -> {_blockIp}:{_oraclePort}");
        }
        return found.Count;
    }

    private static List<string> FindClientExes()
    {
        var found = new List<string>();

        try
        {
            var procs = Process.GetProcesses();
            foreach (var proc in procs)
            {
                try
                {
                    var name = proc.ProcessName.ToLowerInvariant();
                    if (KnownOracleClients.Any(c =>
                            name.Equals(Path.GetFileNameWithoutExtension(c), StringComparison.OrdinalIgnoreCase)))
                    {
                        var path = proc.MainModule?.FileName;
                        if (path != null && File.Exists(path) &&
                            !found.Contains(path, StringComparer.OrdinalIgnoreCase))
                        {
                            found.Add(path);
                        }
                    }
                }
                catch { }
            }
        }
        catch { }

        return found;
    }

    private void DeleteExistingRules()
    {
        foreach (var client in KnownOracleClients)
        {
            var exeName = Path.GetFileNameWithoutExtension(client);
            RunNetsh($"advfirewall firewall delete rule name=\"{RulePrefix}{exeName}\"");
        }
    }

    private static void RunNetsh(string args)
    {
        try
        {
            var psi = new ProcessStartInfo("netsh", args)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            using var proc = Process.Start(psi);
            proc?.WaitForExit(5000);
        }
        catch { }
    }

    private static string ResolveToIp(string? host)
    {
        if (string.IsNullOrEmpty(host)) return "";
        if (IPAddress.TryParse(host, out _)) return host;
        try
        {
            var addresses = Dns.GetHostAddresses(host);
            if (addresses.Length > 0) return addresses[0].ToString();
        }
        catch { }
        return host;
    }
}
