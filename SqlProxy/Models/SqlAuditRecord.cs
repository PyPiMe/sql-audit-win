namespace SqlProxy.Models;

public class SqlAuditRecord
{
    public string Wid { get; set; } = Guid.NewGuid().ToString("N");
    public string Timestamp { get; set; } = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
    public string? SourceIp { get; set; }
    public string? Username { get; set; }
    public string? SqlText { get; set; }
    public string? ClientHost { get; set; }
    public string? ClientUser { get; set; }
}
