using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MusicDisplay;

internal sealed class MusicCache
{
    internal sealed record Entry
    {
        public int Schema { get; init; } = 1;
        public string SourceAppId { get; init; } = "";
        public string TrackId { get; init; } = "";
        public string Title { get; init; } = "";
        public string Artist { get; init; } = "";
        public string Album { get; init; } = "";
        public long DurationMs { get; init; }
        public string Lrc { get; init; } = "";
        public string Plain { get; init; } = "";
        public bool LyricsReady { get; init; }
        public string CoverSha256 { get; init; } = "";
        public DateTimeOffset UpdatedUtc { get; init; } = DateTimeOffset.UtcNow;
    }
    private readonly string _root;
    private readonly object _gate = new();
    private DateTime _lastTrim = DateTime.MinValue;
    public MusicCache(string? root=null)
    {
        _root=Path.GetFullPath(root??Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"DeskHub","cache","music","v1"));
        Directory.CreateDirectory(_root);
    }
    private string Folder(MediaSnapshot track) => Path.Combine(_root,Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(track.SourceAppId+"\n"+track.TrackId))).ToLowerInvariant());
    private static void Atomic(string path,byte[] data)
    {
        string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {File.WriteAllBytes(temp,data);File.Move(temp,path,true);}
        finally {if(File.Exists(temp))File.Delete(temp);}
    }
    public (Entry? Entry,byte[]? Cover) Load(MediaSnapshot track)
    {
        lock(_gate)
        {
            try
            {
                string folder=Folder(track),path=Path.Combine(folder,"metadata.json");
                if(!File.Exists(path)||new FileInfo(path).Length>128*1024)return (null,null);
                var entry=JsonSerializer.Deserialize<Entry>(File.ReadAllText(path));
                if(entry is null||entry.Schema!=1||entry.TrackId!=track.TrackId||entry.SourceAppId!=track.SourceAppId)return (null,null);
                byte[]? cover=null;string image=Path.Combine(folder,"cover.bin");
                if(File.Exists(image)&&new FileInfo(image).Length is >0 and <=512*1024)
                {
                    var bytes=File.ReadAllBytes(image);
                    if(Convert.ToHexString(SHA256.HashData(bytes))==entry.CoverSha256)cover=bytes;
                }
                File.SetLastWriteTimeUtc(path,DateTime.UtcNow);
                return (entry,cover);
            }
            catch(Exception ex) when(ex is IOException or UnauthorizedAccessException or JsonException){return (null,null);}
        }
    }
    public void Save(MediaSnapshot track,string? lrc=null,string? plain=null,byte[]? cover=null)
    {
        lock(_gate)
        {
            try
            {
                var old=Load(track).Entry??new Entry();
                var entry=old with {SourceAppId=track.SourceAppId,TrackId=track.TrackId,Title=track.Title,Artist=track.Artist,Album=track.Album,DurationMs=(long)track.Duration.TotalMilliseconds,UpdatedUtc=DateTimeOffset.UtcNow};
                if(lrc is not null&&plain is not null&&Encoding.UTF8.GetByteCount(lrc)+Encoding.UTF8.GetByteCount(plain)<=32768)
                    entry=entry with {Lrc=lrc,Plain=plain,LyricsReady=lrc.Length>0||plain.Length>0};
                string folder=Folder(track);Directory.CreateDirectory(folder);
                if(cover is {Length:>0 and <=512*1024})
                {Atomic(Path.Combine(folder,"cover.bin"),cover);entry=entry with {CoverSha256=Convert.ToHexString(SHA256.HashData(cover))};}
                Atomic(Path.Combine(folder,"metadata.json"),JsonSerializer.SerializeToUtf8Bytes(entry,new JsonSerializerOptions{WriteIndented=true}));
                Trim();
            }
            catch(Exception ex) when(ex is IOException or UnauthorizedAccessException){Console.Error.WriteLine("Music cache: "+ex.GetType().Name);}
        }
    }
    private void Trim()
    {
        if(DateTime.UtcNow-_lastTrim<TimeSpan.FromMinutes(5))return;_lastTrim=DateTime.UtcNow;
        var folders=new DirectoryInfo(_root).EnumerateDirectories()
            .Where(d=>d.Name.Length==64&&d.Name.All(Uri.IsHexDigit)&&!d.Attributes.HasFlag(FileAttributes.ReparsePoint))
            .Select(d=>new {Dir=d,Bytes=d.EnumerateFiles().Sum(f=>f.Length),Used=File.Exists(Path.Combine(d.FullName,"metadata.json"))?File.GetLastWriteTimeUtc(Path.Combine(d.FullName,"metadata.json")):d.LastWriteTimeUtc})
            .OrderBy(d=>d.Used).ToList();
        long total=folders.Sum(d=>d.Bytes);int count=folders.Count;
        foreach(var item in folders)
        {
            if(total<=512L*1024*1024&&count<=1000)break;
            string full=Path.GetFullPath(item.Dir.FullName);
            if(!full.StartsWith(_root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))continue;
            // Only cache files we own; no recursive traversal or junction following.
            foreach(string name in new[]{"metadata.json","cover.bin"})File.Delete(Path.Combine(full,name));
            if(!Directory.EnumerateFileSystemEntries(full).Any())Directory.Delete(full,false);
            total-=item.Bytes;--count;
        }
    }
    public static object SelfTest(string root)
    {
        var cache=new MusicCache(root);
        var track=new MediaSnapshot("QQMusic.exe","../untrusted-id","测试歌曲","测试歌手","测试专辑",TimeSpan.Zero,TimeSpan.FromSeconds(120),true,null);
        byte[] cover={0xff,0xd8,0xff,0xd9};
        cache.Save(track,lrc:"[00:01.00]测试歌词",plain:"");cache.Save(track,cover:cover);
        var loaded=cache.Load(track);
        if(loaded.Entry?.Title!=track.Title||loaded.Entry.Artist!=track.Artist||loaded.Entry.Lrc!="[00:01.00]测试歌词"||loaded.Cover is null||!loaded.Cover.SequenceEqual(cover))throw new InvalidDataException("Cache round trip failed");
        string folder=cache.Folder(track);if(!Path.GetFullPath(folder).StartsWith(Path.GetFullPath(root)+Path.DirectorySeparatorChar))throw new InvalidDataException("Unsafe cache path");
        File.WriteAllBytes(Path.Combine(folder,"cover.bin"),new byte[]{1,2,3});
        if(cache.Load(track).Cover is not null)throw new InvalidDataException("Corrupt cover accepted");
        File.WriteAllText(Path.Combine(folder,"metadata.json"),"{broken");
        if(cache.Load(track).Entry is not null)throw new InvalidDataException("Corrupt metadata accepted");
        return new {metadata_round_trip=true,independent_asset_updates=true,hashed_paths=true,corrupt_cover_rejected=true,corrupt_metadata_rejected=true};
    }
}
