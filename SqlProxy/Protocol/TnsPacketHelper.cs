using System.Text;
using System.Text.RegularExpressions;

namespace SqlProxy.Protocol;

public static class TnsPacketHelper
{
    public const byte TNS_TYPE_CONNECT = 1;
    public const byte TNS_TYPE_ACCEPT = 2;
    public const byte TNS_TYPE_ACK = 3;
    public const byte TNS_TYPE_REFUSE = 4;
    public const byte TNS_TYPE_REDIRECT = 5;
    public const byte TNS_TYPE_DATA = 6;
    public const byte TNS_TYPE_RESEND = 11;
    public const byte TNS_TYPE_MARKER = 12;
    public const byte TNS_TYPE_ATTENTION = 13;
    public const byte TNS_TYPE_CONTROL = 14;

    public static byte GetPacketType(byte[] data, int length)
    {
        if (length < 8) return 0;
        return data[4];
    }

    public static int GetPacketLength(byte[] data, int length)
    {
        if (length < 2) return 0;
        return (data[0] << 8) | data[1];
    }

    public static RedirectInfo? ParseRedirect(byte[] data, int length)
    {
        return ParseRedirect(data, 0, length);
    }

    public static RedirectInfo? ParseRedirect(byte[] data, int offset, int length)
    {
        if (length < 20) return null;
        if (offset + length > data.Length) return null;

        var text = ExtractAsciiText(data, offset, length);

        var hostMatch = Regex.Match(text, @"HOST=([^\)\s]+)", RegexOptions.IgnoreCase);
        var portMatch = Regex.Match(text, @"PORT=(\d+)", RegexOptions.IgnoreCase);

        if (!hostMatch.Success) return null;

        return new RedirectInfo
        {
            Host = hostMatch.Groups[1].Value,
            Port = portMatch.Success ? int.Parse(portMatch.Groups[1].Value) : 1521
        };
    }

    private static string ExtractAsciiText(byte[] data, int offset, int length)
    {
        var sb = new StringBuilder();
        int end = Math.Min(offset + length, data.Length);
        for (int i = offset + 8; i < end; i++)
        {
            if (data[i] >= 32 && data[i] < 127)
                sb.Append((char)data[i]);
        }
        return sb.ToString();
    }

    public static RedirectInfo? FindRedirectInTnsData(byte[] data, int length)
    {
        if (length < 20) return null;

        var fullText = ExtractAllAsciiText(data, length);

        var hostMatch = Regex.Match(fullText, @"HOST=([^\)\s]+)", RegexOptions.IgnoreCase);
        var portMatch = Regex.Match(fullText, @"PORT=(\d+)", RegexOptions.IgnoreCase);

        if (!hostMatch.Success) return null;

        return new RedirectInfo
        {
            Host = hostMatch.Groups[1].Value,
            Port = portMatch.Success ? int.Parse(portMatch.Groups[1].Value) : 1521
        };
    }

    public static string ExtractAllAsciiText(byte[] data, int length)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < length && i < data.Length; i++)
        {
            if (data[i] >= 32 && data[i] < 127)
                sb.Append((char)data[i]);
        }
        return sb.ToString();
    }

    public static byte[] BuildRedirectPacket(string host, int port, byte[] originalData, int originalLength)
    {
        var newAddr = $"(ADDRESS=(PROTOCOL=TCP)(HOST={host})(PORT={port}))";

        var text = ExtractAsciiText(originalData, 0, originalLength);

        int descIdx = text.IndexOf("(DESCRIPTION=", StringComparison.OrdinalIgnoreCase);

        string newPayload;
        if (descIdx >= 0)
        {
            var descPart = text.Substring(descIdx);
            newPayload = newAddr + "\0" + descPart;
        }
        else
        {
            newPayload = newAddr;
        }

        var payloadBytes = Encoding.ASCII.GetBytes(newPayload);

        var headerLen = 20;
        var totalLen = headerLen + payloadBytes.Length;

        var packet = new byte[totalLen];

        Array.Copy(originalData, 8, packet, 8, Math.Min(12, originalLength - 8));

        packet[0] = (byte)((totalLen >> 8) & 0xFF);
        packet[1] = (byte)(totalLen & 0xFF);
        packet[4] = TNS_TYPE_REDIRECT;

        Buffer.BlockCopy(payloadBytes, 0, packet, headerLen, payloadBytes.Length);

        return packet;
    }
}

public class RedirectInfo
{
    public string Host { get; set; } = "";
    public int Port { get; set; } = 1521;
}