using System.Text;
using System.Text.RegularExpressions;
using SqlProxy.Models;

namespace SqlProxy.Protocol;

public static class TnsDataParser
{
    private static readonly string[] SqlKeywords =
    [
        "SELECT", "INSERT", "UPDATE", "DELETE", "CREATE", "ALTER", "DROP",
        "EXEC", "EXECUTE", "CALL", "MERGE", "TRUNCATE", "GRANT", "REVOKE",
        "BEGIN", "DECLARE", "COMMIT", "ROLLBACK", "WITH", "SAVEPOINT",
        "RENAME", "LOCK", "SET"
    ];

    private static readonly string[] InternalSqlPatterns =
    [
        "sys.dbms_transaction.local_transaction_id",
        "sys.dbms_session.",
        "sys.dbms_application_info.set_module",
        "sys.dbms_application_info.set_action",
        "sys.dbms_application_info.set_client_info",
        "sys.dbms_output.",
        "sys.dbms_alert.",
        "sys.dbms_pipe.",
        "sys.dbms_defer.",
        "sys.dbms_lock.",
    ];

    public static SqlAuditRecord? ExtractSqlInfo(byte[] data, int length, string? sourceIp)
    {
        if (length < 10) return null;

        try
        {
            var str = Encoding.UTF8.GetString(data, 0, length);
            str = StripBinaryChars(str);

            var sqlMatch = ExtractSqlFromText(str);
            if (sqlMatch == null) return null;
            if (IsInternalOracleCall(sqlMatch)) return null;

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
            int searchFrom = 0;
            while (searchFrom < text.Length)
            {
                int idx = text.IndexOf(keyword, searchFrom, StringComparison.OrdinalIgnoreCase);
                if (idx < 0) break;

                if (IsWordBoundary(text, idx, keyword.Length))
                {
                    int end = FindSqlEnd(text, idx);
                    if (end > idx)
                    {
                        var sql = text.Substring(idx, end - idx).Trim();
                        sql = CleanSqlText(sql);
                        if (sql.Length > 10)
                            return sql;
                    }
                }

                searchFrom = idx + keyword.Length;
            }
        }
        return null;
    }

    private static bool IsInternalOracleCall(string sql)
    {
        foreach (var pattern in InternalSqlPatterns)
        {
            if (sql.Contains(pattern, StringComparison.OrdinalIgnoreCase))
                return true;
        }
        return false;
    }

    private static bool IsWordBoundary(string text, int idx, int keywordLen)
    {
        if (idx > 0)
        {
            char before = text[idx - 1];
            if (char.IsLetterOrDigit(before) || before == '_') return false;
        }
        int afterIdx = idx + keywordLen;
        if (afterIdx < text.Length)
        {
            char after = text[afterIdx];
            if (char.IsLetterOrDigit(after) || after == '_') return false;
        }
        return true;
    }

    private static int FindSqlEnd(string s, int start)
    {
        bool inString = false;
        int parenDepth = 0;
        int beginDepth = 0;
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
                else if (c == ';' && parenDepth == 0 && beginDepth == 0) return i;

                if (MatchesKeywordAt(s, i, "BEGIN"))
                    beginDepth++;
                else if (MatchesKeywordAt(s, i, "END") && beginDepth > 0)
                    beginDepth--;
                else if (MatchesKeywordAt(s, i, "LOOP"))
                    beginDepth++;
            }
        }
        return s.Length;
    }

    private static bool MatchesKeywordAt(string s, int idx, string keyword)
    {
        if (idx + keyword.Length > s.Length) return false;
        if (string.Compare(s, idx, keyword, 0, keyword.Length, StringComparison.OrdinalIgnoreCase) != 0) return false;
        return IsWordBoundary(s, idx, keyword.Length);
    }

    private static string CleanSqlText(string sql)
    {
        sql = sql.Trim();

        sql = RemoveEmbeddedArtifactAt(sql);
        sql = TrimTrailingTnsArtifacts(sql);
        sql = sql.TrimEnd('@', '?').Trim();

        return sql;
    }

    private static string RemoveEmbeddedArtifactAt(string sql)
    {
        var sb = new StringBuilder(sql.Length);
        for (int i = 0; i < sql.Length; i++)
        {
            char c = sql[i];
            if (i > 0 && i < sql.Length - 1)
            {
                char prev = sql[i - 1];
                char next = sql[i + 1];
                bool prevWord = char.IsLetterOrDigit(prev) || prev == '_' || prev == '#';
                bool nextWord = char.IsLetterOrDigit(next) || next == '_' || next == '#';

                if (prevWord && nextWord)
                {
                    if (c == '_' || c == '#' || c == '$' || c == '.' || c == ' ')
                    {
                    }
                    else if (!char.IsLetterOrDigit(c))
                    {
                        continue;
                    }
                }
            }
            sb.Append(c);
        }
        return sb.ToString();
    }

    private static string TrimTrailingTnsArtifacts(string sql)
    {
        int bestCut = sql.Length;

        for (int i = 0; i < sql.Length - 3; i++)
        {
            if ((sql[i] == '@' && sql[i + 1] == 'T') ||
                (sql[i] == '@' && sql[i + 1] == '@'))
            {
                int repeatCount = 1;
                int step = (sql[i] == '@' && sql[i + 1] == 'T') ? 2 : 2;
                int j = i + step;
                while (j + 1 < sql.Length &&
                       sql[j] == sql[i] &&
                       sql[j + 1] == sql[i + 1])
                {
                    repeatCount++;
                    j += step;
                }

                if (repeatCount >= 3)
                {
                    bestCut = Math.Min(bestCut, i);
                    break;
                }
            }
        }

        if (bestCut < sql.Length)
            sql = sql.Substring(0, bestCut).TrimEnd();

        return sql;
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
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (c >= 32 && c < 127)
                sb.Append(c);
            else
                sb.Append(' ');
        }
        return NormalizeWhitespace(sb.ToString());
    }

    private static string NormalizeWhitespace(string s)
    {
        var sb = new StringBuilder(s.Length);
        bool lastWasSpace = false;
        for (int i = 0; i < s.Length; i++)
        {
            if (s[i] == ' ')
            {
                if (!lastWasSpace)
                {
                    sb.Append(' ');
                    lastWasSpace = true;
                }
            }
            else
            {
                sb.Append(s[i]);
                lastWasSpace = false;
            }
        }
        return sb.ToString();
    }
}
