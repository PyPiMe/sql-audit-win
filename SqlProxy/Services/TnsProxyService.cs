using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using SqlProxy.Models;
using SqlProxy.Protocol;

namespace SqlProxy.Services;

public class TnsProxyService
{
    private readonly string _listenAddress;
    private readonly int _listenPort;
    private readonly FileLogService _fileLog;
    private readonly DebugLogService _debugLog;
    private readonly AppConfig _config;
    private readonly ConcurrentDictionary<string, ClientSession> _sessions = new();
    private CancellationTokenSource? _cts;
    private DateTime _lastActivity = DateTime.Now;
    private readonly string _sourceIp;
    private readonly string _clientHost;
    private readonly string _clientUser;

    public DateTime LastActivity => _lastActivity;

    public TnsProxyService(AppConfig config, FileLogService fileLog, DebugLogService debugLog)
    {
        _listenAddress = config.ListenAddress;
        _listenPort = config.ListenPort;
        _config = config;
        _fileLog = fileLog;
        _debugLog = debugLog;

        _sourceIp = GetLocalIpAddress();
        _clientHost = Environment.MachineName;
        _clientUser = Environment.UserName;
    }

    private static string GetLocalIpAddress()
    {
        try
        {
            using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            socket.Connect("8.8.8.8", 65530);
            var endPoint = socket.LocalEndPoint as IPEndPoint;
            return endPoint?.Address.ToString() ?? "127.0.0.1";
        }
        catch
        {
            return "127.0.0.1";
        }
    }

    public async Task StartAsync()
    {
        _cts = new CancellationTokenSource();
        var listener = new TcpListener(IPAddress.Any, _listenPort);
        listener.Start();

        LogStartup();
        Console.WriteLine($"[INFO] SQL Proxy listening on {_listenAddress}:{_listenPort}");

        while (!_cts.Token.IsCancellationRequested)
        {
            try
            {
                var client = await listener.AcceptTcpClientAsync(_cts.Token);
                var endPoint = client.Client.RemoteEndPoint?.ToString() ?? "unknown";
                _debugLog.Log($"[CONNECT] Client connected: {endPoint}");
                _ = HandleClientAsync(client, endPoint, _cts.Token);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _debugLog.Log($"[ERROR] Accept failed: {ex}");
            }
        }
    }

    private void LogStartup()
    {
        if (!_config.DebugEnabled) return;

        _debugLog.Log("========================================");
        _debugLog.Log("[START] SQL Proxy Service started");
        _debugLog.Log($"[START] Listening on: {_listenAddress}:{_listenPort}");
        if (_config.AuditDb != null)
        {
            _debugLog.Log($"[START] Backend: {_config.AuditDb.Host}:{_config.AuditDb.Port}/{_config.AuditDb.ServiceName}");
            _debugLog.Log($"[START] User: {_config.AuditDb.Username}");
        }
        _debugLog.Log($"[START] Flush interval: {_config.FlushIntervalMinutes} minutes");
        _debugLog.Log("========================================");
    }

    public void Stop()
    {
        _debugLog.Log("[STOP] SQL Proxy Service stopping...");
        _cts?.Cancel();
        foreach (var session in _sessions.Values)
        {
            session.Dispose();
        }
        _sessions.Clear();
    }

    private async Task HandleClientAsync(TcpClient client, string endPoint, CancellationToken ct)
    {
        var sessionId = Guid.NewGuid().ToString("N")[..8];
        _debugLog.Log($"[SESSION:{sessionId}] ========== New Session ==========");

        TcpClient? backend = null;
        try
        {
            if (_config.AuditDb == null || string.IsNullOrEmpty(_config.AuditDb.Host))
            {
                _debugLog.Log($"[SESSION:{sessionId}] FAIL: No backend configured");
                client.Close();
                return;
            }

            _debugLog.Log($"[SESSION:{sessionId}] Connecting to backend: {_config.AuditDb.Host}:{_config.AuditDb.Port}");

            backend = new TcpClient();
            backend.NoDelay = true;

            using (var connectCts = new CancellationTokenSource(TimeSpan.FromSeconds(10)))
            {
                await backend.ConnectAsync(_config.AuditDb.Host, _config.AuditDb.Port, connectCts.Token);
            }

            _debugLog.Log($"[SESSION:{sessionId}] Backend connected successfully");

            var session = new ClientSession
            {
                Client = client,
                Backend = backend,
                LastActivity = DateTime.Now,
                ClientEndPoint = endPoint
            };
            _sessions[sessionId] = session;

            var clientStream = client.GetStream();
            var backendStream = backend.GetStream();

            var clientToBackend = ProxyDataAsync(clientStream, backendStream, true, endPoint, sessionId, session, ct);
            var backendToClient = ProxyDataAsync(backendStream, clientStream, false, endPoint, sessionId, session, ct);

            await Task.WhenAny(
                Task.WhenAll(clientToBackend, backendToClient),
                Task.Delay(TimeSpan.FromDays(1), ct));

            _debugLog.Log($"[SESSION:{sessionId}] Session ended");
        }
        catch (Exception ex)
        {
            _debugLog.Log($"[SESSION:{sessionId}] ERROR: {ex.Message}");
        }
        finally
        {
            _sessions.TryRemove(sessionId, out _);
            client.Close();
            backend?.Close();
            _debugLog.Log($"[SESSION:{sessionId}] ========== Session Closed ==========");
        }
    }

    private async Task ProxyDataAsync(NetworkStream src, NetworkStream dst, bool fromClient, string endPoint, string sessionId, ClientSession session, CancellationToken ct)
    {
        var buffer = new byte[32768];
        var direction = fromClient ? "C->B" : "B->C";
        var packetCount = 0;

        try
        {
            while (src.Socket.Connected && dst.Socket.Connected)
            {
                var bytesRead = await src.ReadAsync(buffer, ct);
                if (bytesRead == 0) break;

                packetCount++;
                _lastActivity = DateTime.Now;
                session.LastActivity = DateTime.Now;

                if (fromClient && packetCount > 1)
                {
                    ExtractAndLogSql(buffer, bytesRead, endPoint, session);
                }

                await dst.WriteAsync(buffer.AsMemory(0, bytesRead), ct);

                if (_config.DebugEnabled && packetCount <= 5)
                {
                    _debugLog.LogHex($"[SESSION:{sessionId}] {direction}[{packetCount}]", buffer, Math.Min(bytesRead, 256));
                }
            }
        }
        catch { }
        finally
        {
            _debugLog.Log($"[SESSION:{sessionId}] {direction}: ended ({packetCount} packets)");
        }
    }

    private void ExtractAndLogSql(byte[] buffer, int length, string endPoint, ClientSession session)
    {
        try
        {
            var record = TnsDataParser.ExtractSqlInfo(buffer, length, endPoint);
            if (record != null && !string.IsNullOrWhiteSpace(record.SqlText))
            {
                record.SourceIp = _sourceIp;
                record.Username = _config.AuditDb?.Username;
                record.ClientHost = _clientHost;
                record.ClientUser = _clientUser;
                _fileLog.Log(record);

                _debugLog.Log($"[SQL] [{record.Timestamp}]");
                _debugLog.Log($"[SQL] SourceIp={_sourceIp}, Username={record.Username}");
                _debugLog.Log($"[SQL] ClientHost={_clientHost}, ClientUser={_clientUser}");
                _debugLog.Log($"[SQL] {record.SqlText}");
                Console.WriteLine($"[SQL] {record.Timestamp} [{record.Username}] {record.SqlText}");
            }
        }
        catch (Exception ex)
        {
            _debugLog.Log($"[ERROR] ExtractAndLogSql failed: {ex.Message}");
        }
    }

    private class ClientSession : IDisposable
    {
        public TcpClient? Client { get; set; }
        public TcpClient? Backend { get; set; }
        public string? ClientEndPoint { get; set; }
        public string? Username { get; set; }
        public DateTime LastActivity { get; set; }

        public void Dispose()
        {
            Client?.Dispose();
            Backend?.Dispose();
        }
    }
}
