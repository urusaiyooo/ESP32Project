using MusicDisplay;
using System.Text;
using System.Text.Json;
using Windows.Storage.Streams;
using System.Threading.Channels;

internal static class Program
{
    private static readonly object Gate = new();
    private static TextWriter Output = Console.Out;
    private static MusicCache Cache = null!;
    private static void Emit(object value)
    {
        lock (Gate) { Output.WriteLine(JsonSerializer.Serialize(value)); Output.Flush(); }
    }

    public static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        Console.InputEncoding = new UTF8Encoding(false);
        Output = Console.Out;
        Console.SetOut(Console.Error); // Only machine-readable records go to the worker pipe.
        if(args.Contains("--self-test-cache"))
        {Emit(MusicCache.SelfTest(args[Array.IndexOf(args,"--self-test-cache")+1]));return 0;}
        if (args.Contains("--self-test"))
        {
            Emit(new { kind = "collector_ready", rendering = "device", version = "1.4.7",controls=true,persistent_cache=true });
            return 0;
        }
        using var quit = new CancellationTokenSource();
        Cache=new MusicCache();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; quit.Cancel(); };
        try
        {
            await using var media = await MediaSessionService.CreateAsync(quit.Token);
            var updates=Channel.CreateBounded<MediaSnapshot?>(new BoundedChannelOptions(1){FullMode=BoundedChannelFullMode.DropOldest,SingleReader=true});
            media.SnapshotChanged+=(_,snapshot)=>updates.Writer.TryWrite(snapshot);
            var mediaTask = media.RunAsync(quit.Token);
            // Console.In's synchronized reader may block before returning its Task.
            // Keep it off the metadata publisher thread, including when stdin is idle.
            _=Task.Run(()=>ReadCommands(media,quit.Token));
            string? lastTrack = null;
            CancellationTokenSource? assets = null;
            await foreach(var snapshot in updates.Reader.ReadAllAsync(quit.Token))
            {
                if (snapshot is null)
                {
                    assets?.Cancel();Emit(new { kind = "idle" });lastTrack = null;
                }
                else
                {
                    Emit(new { kind = "playback", track_id = snapshot.TrackId,
                        title = snapshot.Title, artist = snapshot.Artist, album = snapshot.Album,
                        duration_ms = Math.Max(0L,(long)snapshot.Duration.TotalMilliseconds),
                        position_ms = Math.Max(0L,(long)snapshot.Position.TotalMilliseconds), playing = snapshot.Playing,
                        observed_at_ms=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() });
                    if (snapshot.TrackId != lastTrack)
                    {
                        assets?.Cancel();assets?.Dispose();assets = CancellationTokenSource.CreateLinkedTokenSource(quit.Token);
                        lastTrack = snapshot.TrackId;
                        var token=assets.Token;
                        _ = Task.Run(()=>ReadAssets(snapshot, token),token);
                    }
                }
            }
            await mediaTask;
        }
        catch (OperationCanceledException) { }
        catch (Exception e) { Console.Error.WriteLine(e.GetType().Name);return 1; }
        return 0;
    }

    private static async Task ReadAssets(MediaSnapshot snapshot,CancellationToken token)
    {
        var cached=Cache.Load(snapshot);if(token.IsCancellationRequested)return;
        Emit(new {kind="cache_status",track_id=snapshot.TrackId,cover_hit=cached.Cover is not null,lyrics_hit=cached.Entry?.LyricsReady==true});
        if(cached.Entry?.LyricsReady==true)Emit(new {kind="lyrics",track_id=snapshot.TrackId,lrc=cached.Entry.Lrc,plain=cached.Entry.Plain,cache_hit=true});
        if(cached.Cover is not null)Emit(new {kind="cover",track_id=snapshot.TrackId,data=Convert.ToBase64String(cached.Cover),cache_hit=true});
        Cache.Save(snapshot);
        await Task.WhenAll(cached.Entry?.LyricsReady==true?Task.CompletedTask:ReadLyrics(snapshot,token),cached.Cover is not null?Task.CompletedTask:ReadCover(snapshot,token));
    }
    private static async Task ReadCommands(MediaSessionService media,CancellationToken token)
    {
        try {
            while(await Console.In.ReadLineAsync(token) is string line) {
                if(line.Length>1024)continue;
                try {
                    using var doc=JsonDocument.Parse(line);
                    uint id=doc.RootElement.GetProperty("id").GetUInt32();
                    string command=doc.RootElement.GetProperty("command").GetString()??"";
                    if(command is not ("previous" or "next" or "play" or "pause"))continue;
                    bool success=false;
                    try {success=await media.ControlAsync(command,token);}catch(Exception e){Console.Error.WriteLine("Control: "+e.GetType().Name);}
                    Emit(new {kind="control_result",id,command,success});
                } catch(JsonException){} catch(InvalidOperationException){} catch(KeyNotFoundException){}
            }
        }catch(OperationCanceledException){}
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
            Cache.Save(snapshot,cover:bytes);
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
                try
                {
                using var qq=new QqMusicLyricsService();
                var result=await qq.GetLyricsAsync(snapshot,token);
                if(result.Lrc.Length>0 && Encoding.UTF8.GetByteCount(result.Lrc)<=32768)
                {Cache.Save(snapshot,lrc:result.Lrc,plain:"");Emit(new {kind="lyrics",track_id=snapshot.TrackId,lrc=result.Lrc,plain=""});return;}
                }
                catch(OperationCanceledException) when(token.IsCancellationRequested) {return;}
                catch(Exception e) {Console.Error.WriteLine("QQ lyrics fallback: "+e.GetType().Name);}
            }
            token.ThrowIfCancellationRequested();
            using var http=new HttpClient{Timeout=TimeSpan.FromSeconds(12)};
            http.DefaultRequestHeaders.UserAgent.ParseAdd("DeskHub/1.4.7");
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
            Cache.Save(snapshot,lrc:lrc,plain:plain);
            Emit(new {kind="lyrics",track_id=snapshot.TrackId,lrc,plain});
        }
        catch(Exception e) {Console.Error.WriteLine("Lyrics: "+e.GetType().Name);}
    }
}
