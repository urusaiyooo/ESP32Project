using MusicDisplay;
using System.Text;
using System.Text.Json;
using Windows.Storage.Streams;

internal static class Program
{
    private static readonly object Gate = new();
    private static TextWriter Output = Console.Out;
    private static void Emit(object value)
    {
        lock (Gate) { Output.WriteLine(JsonSerializer.Serialize(value)); Output.Flush(); }
    }

    public static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        Output = Console.Out;
        Console.SetOut(Console.Error); // Only machine-readable records go to the worker pipe.
        if (args.Contains("--self-test"))
        {
            Emit(new { kind = "collector_ready", rendering = "device", version = "1.3.0" });
            return 0;
        }
        using var quit = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; quit.Cancel(); };
        try
        {
            await using var media = await MediaSessionService.CreateAsync(quit.Token);
            var mediaTask = media.RunAsync(quit.Token);
            string? lastTrack = null;
            CancellationTokenSource? assets = null;
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
            do
            {
                var snapshot = media.LatestSnapshot;
                if (snapshot is null)
                {
                    Emit(new { kind = "idle" });lastTrack = null;
                }
                else
                {
                    Emit(new { kind = "playback", track_id = snapshot.TrackId,
                        title = snapshot.Title, artist = snapshot.Artist, album = snapshot.Album,
                        duration_ms = Math.Max(0L,(long)snapshot.Duration.TotalMilliseconds),
                        position_ms = Math.Max(0L,(long)snapshot.Position.TotalMilliseconds), playing = snapshot.Playing });
                    if (snapshot.TrackId != lastTrack)
                    {
                        assets?.Cancel();assets?.Dispose();assets = CancellationTokenSource.CreateLinkedTokenSource(quit.Token);
                        lastTrack = snapshot.TrackId;
                        _ = ReadAssets(snapshot, assets.Token);
                    }
                }
            } while (await timer.WaitForNextTickAsync(quit.Token));
            await mediaTask;
        }
        catch (OperationCanceledException) { }
        catch (Exception e) { Console.Error.WriteLine(e.GetType().Name);return 1; }
        return 0;
    }

    private static async Task ReadAssets(MediaSnapshot snapshot,CancellationToken token)
    {
        await Task.WhenAll(ReadLyrics(snapshot,token), ReadCover(snapshot,token));
    }
    private static async Task ReadCover(MediaSnapshot snapshot,CancellationToken token)
    {
        try
        {
            if(snapshot.Thumbnail is null)return;
            using var stream = await snapshot.Thumbnail.OpenReadAsync();
            if(stream.Size == 0 || stream.Size > 512*1024)return;
            using var reader = new DataReader(stream.GetInputStreamAt(0));
            var count=await reader.LoadAsync((uint)stream.Size);token.ThrowIfCancellationRequested();
            byte[] bytes=new byte[count];reader.ReadBytes(bytes);
            // Original JPEG/PNG bytes: no Windows bitmap decoder, resizing or re-encoding.
            Emit(new {kind="cover",track_id=snapshot.TrackId,data=Convert.ToBase64String(bytes)});
        }
        catch(Exception e) {Console.Error.WriteLine("Cover: "+e.GetType().Name);}
    }
    private static async Task ReadLyrics(MediaSnapshot snapshot,CancellationToken token)
    {
        try
        {
            if(snapshot.SourceAppId.EndsWith("QQMusic.exe",StringComparison.OrdinalIgnoreCase))
            {
                using var qq=new QqMusicLyricsService();
                var result=await qq.GetLyricsAsync(snapshot,token);
                if(result.Lrc.Length>0 && Encoding.UTF8.GetByteCount(result.Lrc)<=32768)
                {Emit(new {kind="lyrics",track_id=snapshot.TrackId,lrc=result.Lrc,plain=""});return;}
            }
            using var http=new HttpClient{Timeout=TimeSpan.FromSeconds(12)};
            http.DefaultRequestHeaders.UserAgent.ParseAdd("DeskHub/1.3.0");
            var url="https://lrclib.net/api/get?track_name="+Uri.EscapeDataString(snapshot.Title)+
                "&artist_name="+Uri.EscapeDataString(snapshot.Artist)+"&album_name="+Uri.EscapeDataString(snapshot.Album)+
                "&duration="+Math.Max(0,Math.Round(snapshot.Duration.TotalSeconds));
            using var response=await http.GetAsync(url,token);
            if(!response.IsSuccessStatusCode)return;
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(token));
            string lrc=doc.RootElement.TryGetProperty("syncedLyrics",out var a)?a.GetString()??"":"";
            string plain=doc.RootElement.TryGetProperty("plainLyrics",out var b)?b.GetString()??"":"";
            if(Encoding.UTF8.GetByteCount(lrc)+Encoding.UTF8.GetByteCount(plain)>32768)return;
            // No lyric clock or bitmap window on the PC. S3 parses/estimates the timeline.
            Emit(new {kind="lyrics",track_id=snapshot.TrackId,lrc,plain});
        }
        catch(Exception e) {Console.Error.WriteLine("Lyrics: "+e.GetType().Name);}
    }
}
