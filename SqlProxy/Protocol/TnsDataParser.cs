using System.Text;
using System.Text.RegularExpressions;
using SqlProxy.Models;

namespace SqlProxy.Protocol;

public static class TnsDataParser
{
    private static readonly string[] SqlKeywords =
        ["SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "ALTER", "DROP", "EXEC", "CALL"];

    public static SqlAuditRecord? ExtractSqlInfo(byte[] data, int length, string? sourceIp)
    {
        if (length < 10) return null;

        try
        {
            var str = Encoding.UTF8.GetString(data, 0, length);
            str = StripBinaryChars(str);

            var sqlMatch = ExtractSqlFromText(str);
            if (sqlMatch == null) return null;

            var record = new SqlAuditRecord
            {
                Timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
                SqlText = sqlMatch
            };

            return record;
        }
        catch
        {
            return null;
        }
    }

    public static string? ExtractLoginUsername(byte[] data, int length)
    {
        try
        {
            var str = Encoding.UTF8.GetString(data, 0, length);
            str = StripBinaryChars(str);

            int authTermIdx = str.IndexOf("AUTH_TERMINAL", StringComparison.Ordinal);
            if (authTermIdx > 0)
            {
                var prefix = str.Substring(0, authTermIdx).TrimEnd();
                var match = Regex.Match(prefix, @"([A-Za-z_][A-Za-z0-9_#\$]*)$");
                if (match.Success)
                    return match.Groups[1].Value;
            }

            return null;
        }
        catch
        {
            return null;
        }
    }

    public static string? ExtractTargetDatabase(byte[] data, int length)
    {
        try
        {
            var str = Encoding.UTF8.GetString(data, 0, length);
            str = StripBinaryChars(str);

            var svcMatch = Regex.Match(str, @"SERVICE_NAME[=:\s]+([^\s\),]+)", RegexOptions.IgnoreCase);
            if (svcMatch.Success)
                return svcMatch.Groups[1].Value.Trim();

            var sidMatch = Regex.Match(str, @"SID[=:\s]+([^\s\),]+)", RegexOptions.IgnoreCase);
            if (sidMatch.Success)
                return sidMatch.Groups[1].Value.Trim();

            return null;
        }
        catch
        {
            return null;
        }
    }

    public static string? ExtractConnectString(byte[] data, int length)
    {
        try
        {
            var str = Encoding.UTF8.GetString(data, 0, length);
            str = StripBinaryChars(str);

            var hostMatch = Regex.Match(str, @"HOST=([^\)\s]+)", RegexOptions.IgnoreCase);
            var portMatch = Regex.Match(str, @"PORT=(\d+)", RegexOptions.IgnoreCase);
            var svcMatch = Regex.Match(str, @"SERVICE_NAME=([^\)\s]+)", RegexOptions.IgnoreCase);

            if (hostMatch.Success)
            {
                var host = hostMatch.Groups[1].Value;
                var port = portMatch.Success ? portMatch.Groups[1].Value : "1521";
                var svc = svcMatch.Success ? svcMatch.Groups[1].Value : "?";
                return $"{host}:{port}/{svc}";
            }

            return null;
        }
        catch
        {
            return null;
        }
    }

    private static string? ExtractSqlFromText(string text)
    {
        foreach (var keyword in SqlKeywords)
        {
            var pattern = $@"\b{keyword}\b\s+.*?(?=\z|;|\)\s*$)";
            var match = Regex.Match(text, pattern, RegexOptions.IgnoreCase | RegexOptions.Singleline);
            if (match.Success)
            {
                var sql = match.Value.Trim();
                sql = CleanSqlText(sql);
                if (sql.Length > 10)
                {
                    return sql;
                }
            }

            int idx = text.IndexOf(keyword, StringComparison.OrdinalIgnoreCase);
            if (idx >= 0)
            {
                int start = idx;
                int end = FindSqlEnd(text, start);
                if (end > start)
                {
                    var sql = text.Substring(start, end - start).Trim();
                    sql = CleanSqlText(sql);
                    if (sql.Length > 10)
                    {
                        return sql;
                    }
                }
            }
        }
        return null;
    }

    private static int FindSqlEnd(string s, int start)
    {
        bool inString = false;
        int parenDepth = 0;
        for (int i = start; i < s.Length; i++)
        {
            char c = s[i];
            if (c == '\'')
            {
                inString = !inString;
            }
            else if (!inString)
            {
                if (c == '(') parenDepth++;
                else if (c == ')') parenDepth--;
                else if (c == ';' && parenDepth == 0) return i;
            }
        }
        return s.Length;
    }

    private static string CleanSqlText(string sql)
    {
        sql = sql.Trim();
        if (sql.EndsWith(")") && !sql.Contains("SELECT"))
        {
            sql = sql[..^1].Trim();
        }
        while (sql.EndsWith("NULL)"))
        {
            sql = sql[..^5].Trim().TrimEnd('(').TrimEnd(',');
        }
        return sql.TrimEnd(')').Trim();
    }

    public static string? ExtractUsername(string text)
    {
        var patterns = new[]
        {
            @"USER[=:\s]+([^\s\),]+)",
            @"USERNAME[=:\s]+([^\s\),]+)",
            @"session.*?user\s*=\s*([^\s\),]+)"
        };

        foreach (var pattern in patterns)
        {
            var match = Regex.Match(text, pattern, RegexOptions.IgnoreCase);
            if (match.Success && match.Groups.Count > 1)
            {
                return match.Groups[1].Value.Trim();
            }
        }
        return null;
    }

    private static string StripBinaryChars(string s)
    {
        var sb = new StringBuilder();
        foreach (char c in s)
        {
            if (c >= 32 && c < 127)
                sb.Append(c);
            else if (c == '\n' || c == '\r' || c == '\t')
                sb.Append(' ');
        }
        return sb.ToString();
    }
}
