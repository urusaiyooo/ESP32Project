using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace MusicDisplay;

internal sealed class QqMusicLyricsService : IDisposable
{
    private static readonly Regex ParentheticalSuffix = new(
        @"\s*[\(（\[].*?[\)）\]]\s*$", RegexOptions.Compiled);
    private static readonly Regex Spaces = new(@"\s+", RegexOptions.Compiled);
    private static readonly Regex Timestamp = new(
        @"^\s*\[\d{1,3}:\d{2}(?:[\.:]\d{1,3})?\]", RegexOptions.Compiled);

    private readonly HttpClient _http = new()
    {
        Timeout = TimeSpan.FromSeconds(5)
    };

    public QqMusicLyricsService()
    {
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("MusicDisplay/0.8.0");
        _http.DefaultRequestHeaders.Referrer = new Uri("https://y.qq.com/");
    }

    public async Task<LyricsResult> GetLyricsAsync(MediaSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        var song = await FindSongAsync(snapshot, cancellationToken);
        if (song is null)
            return new LyricsResult(string.Empty, "qqmusic-none", false, 0);

        var uri = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg" +
                  $"?songmid={Uri.EscapeDataString(song.Mid)}&musicid={song.Id}" +
                  "&format=json&g_tk=5381&inCharset=utf8&outCharset=utf-8" +
                  "&notice=0&platform=yqq&needNewCode=0";
        using var response = await _http.GetAsync(uri, cancellationToken);
        response.EnsureSuccessStatusCode();
        await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var document = await JsonDocument.ParseAsync(stream,
            cancellationToken: cancellationToken);
        var root = document.RootElement;
        if (!root.TryGetProperty("code", out var code) || code.GetInt32() != 0 ||
            !root.TryGetProperty("lyric", out var lyricElement))
            return new LyricsResult(string.Empty, "qqmusic-none", false, 0);

        var encoded = lyricElement.GetString();
        if (string.IsNullOrWhiteSpace(encoded))
            return new LyricsResult(string.Empty, "qqmusic-none", false, 0);

        string lrc;
        try { lrc = Encoding.UTF8.GetString(Convert.FromBase64String(encoded)); }
        catch (FormatException) { return new LyricsResult(string.Empty, "qqmusic-invalid", false, 0); }
        lrc = NormalizeLrc(lrc);
        var lineCount = CountTimestampedLines(lrc);
        return lineCount > 0
            ? new LyricsResult(lrc, "qqmusic-synced", false, lineCount)
            : new LyricsResult(string.Empty, "qqmusic-none", false, 0);
    }

    private async Task<QqSong?> FindSongAsync(MediaSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        var query = string.IsNullOrWhiteSpace(snapshot.Artist)
            ? snapshot.Title : $"{snapshot.Title} {FirstArtist(snapshot.Artist)}";
        var payload = new
        {
            comm = new { ct = "19", cv = "1873", uin = "0" },
            req_1 = new
            {
                method = "DoSearchForQQMusicDesktop",
                module = "music.search.SearchCgiService",
                param = new
                {
                    grp = 1, num_per_page = 10, page_num = 1,
                    query, search_type = 0
                }
            }
        };
        using var response = await _http.PostAsJsonAsync(
            "https://u.y.qq.com/cgi-bin/musicu.fcg", payload, cancellationToken);
        response.EnsureSuccessStatusCode();
        await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var document = await JsonDocument.ParseAsync(stream,
            cancellationToken: cancellationToken);
        if (!TryGetSongList(document.RootElement, out var list)) return null;

        return EnumerateSongs(list)
            .Select(ParseSong)
            .Where(song => song is not null)
            .Select(song => song!)
            .Select(song => new { Song = song, Score = Score(song, snapshot) })
            .Where(item => item.Score >= 90)
            .OrderByDescending(item => item.Score)
            .ThenBy(item => Math.Abs(item.Song.DurationSeconds - snapshot.Duration.TotalSeconds))
            .Select(item => item.Song)
            .FirstOrDefault();
    }

    private static bool TryGetSongList(JsonElement root, out JsonElement list)
    {
        list = default;
        return root.TryGetProperty("req_1", out var request) &&
               request.TryGetProperty("data", out var data) &&
               data.TryGetProperty("body", out var body) &&
               body.TryGetProperty("song", out var song) &&
               song.TryGetProperty("list", out list) &&
               list.ValueKind == JsonValueKind.Array;
    }

    private static IEnumerable<JsonElement> EnumerateSongs(JsonElement list)
    {
        foreach (var item in list.EnumerateArray())
        {
            yield return item;
            if (item.TryGetProperty("grp", out var group) && group.ValueKind == JsonValueKind.Array)
                foreach (var variant in group.EnumerateArray()) yield return variant;
        }
    }

    private static QqSong? ParseSong(JsonElement item)
    {
        var mid = StringProperty(item, "mid");
        var name = StringProperty(item, "name");
        if (mid.Length == 0 || name.Length == 0) return null;
        var id = item.TryGetProperty("id", out var idElement) && idElement.TryGetInt64(out var parsedId)
            ? parsedId : 0;
        var duration = item.TryGetProperty("interval", out var interval) &&
                       interval.TryGetDouble(out var parsedDuration) ? parsedDuration : 0;
        var album = item.TryGetProperty("album", out var albumElement)
            ? StringProperty(albumElement, "name") : string.Empty;
        var singers = new List<string>();
        if (item.TryGetProperty("singer", out var singerList) &&
            singerList.ValueKind == JsonValueKind.Array)
            singers.AddRange(singerList.EnumerateArray()
                .Select(singer => StringProperty(singer, "name"))
                .Where(value => value.Length > 0));
        return new QqSong(id, mid, name, album, singers, duration);
    }

    private static int Score(QqSong song, MediaSnapshot snapshot)
    {
        if (!FieldMatches(song.Name, snapshot.Title) ||
            !song.Singers.Any(singer => ArtistMatches(singer, snapshot.Artist))) return 0;
        var durationDifference = Math.Abs(song.DurationSeconds - snapshot.Duration.TotalSeconds);
        if (snapshot.Duration > TimeSpan.Zero && durationDifference > 8) return 0;
        var score = 90;
        if (FieldMatches(song.Album, snapshot.Album)) score += 5;
        score += Math.Max(0, 5 - (int)Math.Ceiling(durationDifference));
        return score;
    }

    private static bool FieldMatches(string candidate, string value)
    {
        var left = Normalize(candidate);
        var right = Normalize(value);
        if (left.Length == 0 || right.Length == 0) return false;
        if (left == right) return true;
        return Math.Min(left.Length, right.Length) >= 3 &&
               (left.Contains(right, StringComparison.Ordinal) ||
                right.Contains(left, StringComparison.Ordinal));
    }

    private static bool ArtistMatches(string candidate, string value) =>
        SplitArtists(value).Any(part => FieldMatches(candidate, part));

    private static string FirstArtist(string value) =>
        SplitArtists(value).FirstOrDefault() ?? value.Trim();

    private static IEnumerable<string> SplitArtists(string value) =>
        value.Split(['/', '／', '、', ',', '，', ';', '；', '&'],
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static string Normalize(string value)
    {
        var normalized = value.Normalize(NormalizationForm.FormKC).ToLowerInvariant();
        normalized = ParentheticalSuffix.Replace(normalized, string.Empty);
        normalized = normalized.Replace("feat.", " ", StringComparison.Ordinal)
            .Replace("feat", " ", StringComparison.Ordinal)
            .Replace("ft.", " ", StringComparison.Ordinal);
        return Spaces.Replace(normalized, " ").Trim();
    }

    private static string NormalizeLrc(string lrc) =>
        string.Join('\n', lrc.Replace("\r", string.Empty, StringComparison.Ordinal)
            .Split('\n').Select(line => line.TrimEnd())).Trim() + "\n";

    private static int CountTimestampedLines(string lrc) =>
        lrc.Split('\n').Count(line => Timestamp.IsMatch(line));

    private static string StringProperty(JsonElement element, string name) =>
        element.TryGetProperty(name, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? string.Empty : string.Empty;

    public void Dispose() => _http.Dispose();

    private sealed record QqSong(long Id, string Mid, string Name, string Album,
        IReadOnlyList<string> Singers, double DurationSeconds);
}
