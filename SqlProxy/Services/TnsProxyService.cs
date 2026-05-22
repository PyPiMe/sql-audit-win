using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using SqlProxy.Models;
using SqlProxy.Protocol;

namespace SqlProxy.Services;

public class TnsProxyService
{
    private readonly string _listenAddress;
    private readonly int _listenPort;
    private readonly FileLogService _fileLog;
    private readonly DebugLogService _debugLog;
    private readonly FirewallService _firewall;
    private readonly AppConfig _config;
    private readonly ConcurrentDictionary<string, ClientSession> _sessions = new();
    private CancellationTokenSource? _cts;
    private DateTime _lastActivity = DateTime.Now;
    private readonly string _sourceIp;
    private readonly string _clientHost;
    private readonly string _clientUser;

    public DateTime LastActivity => _lastActivity;

    public TnsProxyService(AppConfig config, FileLogService fileLog, DebugLogService debugLog, FirewallService firewall)
    {
        _listenAddress = config.ListenAddress;
        _listenPort = config.ListenPort;
        _config = config;
        _fileLog = fileLog;
        _debugLog = debugLog;
        _firewall = firewall;

        _sourceIp = GetLocalIpAddress();
        _clientHost = Environment.MachineName;
        _clientUser = GetActiveConsoleUser();
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

    [DllImport("wtsapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool WTSQuerySessionInformation(
        IntPtr hServer, int sessionId, int infoClass,
        out IntPtr ppBuffer, out int pBytesReturned);

    [DllImport("wtsapi32.dll")]
    private static extern void WTSFreeMemory(IntPtr memory);

    [DllImport("kernel32.dll")]
    private static extern int WTSGetActiveConsoleSessionId();

    private static string GetActiveConsoleUser()
    {
        try
        {
            int sessionId = WTSGetActiveConsoleSessionId();
            if (sessionId != -1 && WTSQuerySessionInformation(IntPtr.Zero, sessionId, 5, out IntPtr buffer, out _))
            {
                var user = Marshal.PtrToStringUni(buffer) ?? "";
                WTSFreeMemory(buffer);
                if (!string.IsNullOrEmpty(user) && !user.EndsWith("$"))
                    return user;
            }
        }
        catch { }

        try
        {
            var explorers = Process.GetProcessesByName("explorer");
            if (explorers.Length > 0)
            {
                var user = GetProcessOwner(explorers[0]);
                if (!string.IsNullOrEmpty(user))
                    return user;
            }
        }
        catch { }

        var envUser = Environment.UserName;
        return envUser.EndsWith("$") ? envUser.TrimEnd('$') : envUser;
    }

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr hProcess, uint desiredAccess, out IntPtr hToken);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool LookupAccountSid(
        string? systemName, IntPtr sid,
        StringBuilder name, ref int nameLen,
        StringBuilder domain, ref int domainLen,
        out int use);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool GetTokenInformation(
        IntPtr hToken, int infoClass,
        IntPtr info, int infoLen, out int retLen);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint TOKEN_QUERY = 0x0008;
    private const int TokenUser = 1;

    private static string GetProcessOwner(Process proc)
    {
        try
        {
            if (!OpenProcessToken(proc.Handle, TOKEN_QUERY, out IntPtr hToken))
                return "";

            int len = 0;
            GetTokenInformation(hToken, TokenUser, IntPtr.Zero, 0, out len);
            IntPtr tokenInfo = Marshal.AllocHGlobal(len);

            try
            {
                if (!GetTokenInformation(hToken, TokenUser, tokenInfo, len, out _))
                    return "";

                var sidPtr = Marshal.ReadIntPtr(tokenInfo);
                var name = new StringBuilder(256);
                var domain = new StringBuilder(256);
                int nameLen = name.Capacity;
                int domainLen = domain.Capacity;

                if (LookupAccountSid(null, sidPtr, name, ref nameLen, domain, ref domainLen, out _))
                    return name.ToString();
            }
            finally
            {
                Marshal.FreeHGlobal(tokenInfo);
                CloseHandle(hToken);
            }
        }
        catch { }
        return "";
    }

    public async Task StartAsync()
    {
        _firewall.SetupRules();

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

        if (_config.PersistFirewallRules)
        {
            _debugLog.Log("[STOP] PersistFirewallRules=true, keeping firewall rules");
            Console.WriteLine("[INFO] Firewall rules persisted (PersistFirewallRules=true)");
        }
        else
        {
            _firewall.RemoveRules();
        }
    }

    private async Task HandleClientAsync(TcpClient client, string endPoint, CancellationToken ct)
    {
        var sessionId = Guid.NewGuid().ToString("N")[..8];
        _debugLog.Log($"[SESSION:{sessionId}] ========== New Session ==========");

        TcpClient? backend = null;
        NetworkStream? backendStream = null;
        NetworkStream? clientStream = null;
        byte[]? clientConnectPacket = null;
        byte[]? finalResponseToClient = null;
        int finalResponseLength = 0;
        var session = new ClientSession();

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

            session.Client = client;
            session.Backend = backend;
            session.LastActivity = DateTime.Now;
            session.ClientEndPoint = endPoint;
            _sessions[sessionId] = session;

            clientStream = client.GetStream();
            backendStream = backend.GetStream();

            _debugLog.Log($"[SESSION:{sessionId}] Starting to read client data...");

            var buffer = new byte[32768];
            var bytesRead = await clientStream.ReadAsync(buffer, ct);
            if (bytesRead == 0)
            {
                _debugLog.Log($"[SESSION:{sessionId}] Client closed before sending data");
                return;
            }

            clientConnectPacket = buffer.Take(bytesRead).ToArray();

            _debugLog.LogHex($"[SESSION:{sessionId}] C->B[1]", clientConnectPacket, Math.Min(bytesRead, 256));

            await backendStream.WriteAsync(clientConnectPacket, ct);
            await backendStream.FlushAsync(ct);

            _debugLog.Log($"[SESSION:{sessionId}] Waiting for backend response...");

            var responseBuffer = new byte[32768];
            var responseBytes = await backendStream.ReadAsync(responseBuffer, ct);
            if (responseBytes == 0)
            {
                _debugLog.Log($"[SESSION:{sessionId}] Backend closed without response");
                return;
            }

            var totalResponseBytes = responseBytes;

            var firstPacketType = TnsPacketHelper.GetPacketType(responseBuffer, responseBytes);
            if (firstPacketType == TnsPacketHelper.TNS_TYPE_REDIRECT)
            {
                var firstPacketLen = TnsPacketHelper.GetPacketLength(responseBuffer, responseBytes);
                while (totalResponseBytes < firstPacketLen)
                {
                    var more = await backendStream.ReadAsync(responseBuffer.AsMemory(totalResponseBytes), ct);
                    if (more == 0) break;
                    totalResponseBytes += more;
                }

                while (totalResponseBytes < 128)
                {
                    var more = await backendStream.ReadAsync(responseBuffer.AsMemory(totalResponseBytes), ct);
                    if (more == 0) break;
                    totalResponseBytes += more;
                }
            }

            _debugLog.LogHex($"[SESSION:{sessionId}] B->C[1]", responseBuffer, Math.Min(totalResponseBytes, 256));

            responseBytes = totalResponseBytes;

            RedirectInfo? redirectInfo = null;

            var fullText = TnsPacketHelper.ExtractAllAsciiText(responseBuffer, responseBytes);
            _debugLog.Log($"[SESSION:{sessionId}] Full response text: {fullText.Substring(0, Math.Min(100, fullText.Length))}...");

            var redirect = TnsPacketHelper.FindRedirectInTnsData(responseBuffer, responseBytes);
            if (redirect != null)
            {
                redirectInfo = redirect;
                _debugLog.Log($"[SESSION:{sessionId}] REDIRECT found: {redirectInfo.Host}:{redirectInfo.Port}");
            }
            else
            {
                _debugLog.Log($"[SESSION:{sessionId}] No redirect found in response");
            }

            if (redirectInfo != null)
            {
                _debugLog.Log($"[SESSION:{sessionId}] Redirecting to real node: {redirectInfo.Host}:{redirectInfo.Port}");

                backend.Close();
                backend = null;
                backendStream = null;

                backend = new TcpClient();
                backend.NoDelay = true;

                using (var connectCts = new CancellationTokenSource(TimeSpan.FromSeconds(10)))
                {
                    await backend.ConnectAsync(redirectInfo.Host, redirectInfo.Port, connectCts.Token);
                }

                _debugLog.Log($"[SESSION:{sessionId}] Connected to real node: {redirectInfo.Host}:{redirectInfo.Port}");

                session.Backend = backend;
                backendStream = backend.GetStream();

                await backendStream.WriteAsync(clientConnectPacket, ct);
                await backendStream.FlushAsync(ct);

                responseBytes = await backendStream.ReadAsync(responseBuffer, ct);
                if (responseBytes == 0)
                {
                    _debugLog.Log($"[SESSION:{sessionId}] Real node closed without response");
                    return;
                }

                _debugLog.LogHex($"[SESSION:{sessionId}] B->C[2]", responseBuffer, Math.Min(responseBytes, 256));

                var hasRedirect2 = TnsPacketHelper.FindRedirectInTnsData(responseBuffer, responseBytes) != null;
                if (hasRedirect2)
                {
                    _debugLog.Log($"[SESSION:{sessionId}] Another REDIRECT received, giving up");
                    return;
                }

                var firstPacketType = TnsPacketHelper.GetPacketType(responseBuffer, responseBytes);
                if (firstPacketType != TnsPacketHelper.TNS_TYPE_ACCEPT && 
                    firstPacketType != TnsPacketHelper.TNS_TYPE_DATA &&
                    firstPacketType != TnsPacketHelper.TNS_TYPE_RESEND &&
                    firstPacketType != TnsPacketHelper.TNS_TYPE_MARKER)
                {
                    _debugLog.Log($"[SESSION:{sessionId}] Unexpected packet type: {firstPacketType}");
                    return;
                }

                _debugLog.Log($"[SESSION:{sessionId}] Got response from real node, starting relay (type={firstPacketType})");

                finalResponseToClient = responseBuffer;
                finalResponseLength = responseBytes;
            }
            else
            {
                finalResponseToClient = responseBuffer;
                finalResponseLength = responseBytes;
            }

            await clientStream.WriteAsync(finalResponseToClient.AsMemory(0, finalResponseLength), ct);
            await clientStream.FlushAsync(ct);

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

                if (fromClient)
                {
                    if (session.LoginUser == null)
                        ExtractLoginInfo(buffer, bytesRead, sessionId, session);

                    var moreData = IsMoreDataFlag(buffer, bytesRead);

                    if (moreData)
                    {
                        var payload = ExtractTnsPayload(buffer, bytesRead);
                        if (payload != null)
                            session.SqlFragmentBuffer.AddRange(payload);
                    }
                    else
                    {
                        var payload = ExtractTnsPayload(buffer, bytesRead);
                        if (payload != null)
                            session.SqlFragmentBuffer.AddRange(payload);

                        if (session.SqlFragmentBuffer.Count > 0)
                        {
                            var fullData = session.SqlFragmentBuffer.ToArray();
                            ExtractAndLogSql(fullData, fullData.Length, endPoint, session);
                            session.SqlFragmentBuffer.Clear();
                        }
                    }
                }

                await dst.WriteAsync(buffer.AsMemory(0, bytesRead), ct);

                if (_config.DebugEnabled && packetCount <= 5)
                {
                    _debugLog.LogHex($"[SESSION:{sessionId}] {direction}[{packetCount}]", buffer, Math.Min(bytesRead, 256));
                }
            }
        }
        catch { }

        if (fromClient && session.SqlFragmentBuffer.Count > 0)
        {
            var fullData = session.SqlFragmentBuffer.ToArray();
            ExtractAndLogSql(fullData, fullData.Length, endPoint, session);
            session.SqlFragmentBuffer.Clear();
        }

        _debugLog.Log($"[SESSION:{sessionId}] {direction}: ended ({packetCount} packets)");
    }

    private static bool IsMoreDataFlag(byte[] data, int length)
    {
        if (length < 10 || data[4] != TnsPacketHelper.TNS_TYPE_DATA) return false;
        return ((data[8] << 8 | data[9]) & 0x0040) != 0;
    }

    private static byte[]? ExtractTnsPayload(byte[] data, int length)
    {
        if (length < 10 || data[4] != TnsPacketHelper.TNS_TYPE_DATA) return null;
        var payloadLen = length - 10;
        if (payloadLen <= 0) return null;
        var payload = new byte[payloadLen];
        Buffer.BlockCopy(data, 10, payload, 0, payloadLen);
        return payload;
    }

    private void ExtractLoginInfo(byte[] buffer, int length, string sessionId, ClientSession session)
    {
        try
        {
            var username = TnsDataParser.ExtractLoginUsername(buffer, length);
            if (!string.IsNullOrEmpty(username))
            {
                session.LoginUser = username;
                _debugLog.Log($"[SESSION:{sessionId}] Login user detected: {username}");
                Console.WriteLine($"[LOGIN] User: {username}");
            }
        }
        catch (Exception ex)
        {
            _debugLog.Log($"[ERROR] ExtractLoginInfo failed: {ex.Message}");
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
                record.Username = session.LoginUser ?? _config.AuditDb?.Username;
                record.ClientHost = _clientHost;
                record.ClientUser = _clientUser;
                _fileLog.Log(record);

                _debugLog.Log($"[SQL] [{record.Timestamp}]");
                _debugLog.Log($"[SQL] SourceIp={_sourceIp}, User={record.Username}");
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
        public string? LoginUser { get; set; }
        public DateTime LastActivity { get; set; }
        public List<byte> SqlFragmentBuffer { get; } = [];

        public void Dispose()
        {
            Client?.Dispose();
            Backend?.Dispose();
        }
    }
}
