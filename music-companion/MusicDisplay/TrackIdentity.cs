using System.Security.Cryptography;
using System.Text;

namespace MusicDisplay;

public static class TrackIdentity
{
    public static string Create(string title, string artist, string album, TimeSpan duration)
    {
        var normalized = string.Join('\n', title.Trim(), artist.Trim(), album.Trim(),
            Math.Max(0L, (long)duration.TotalMilliseconds).ToString());
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(normalized)))
            .ToLowerInvariant()[..24];
    }
}
