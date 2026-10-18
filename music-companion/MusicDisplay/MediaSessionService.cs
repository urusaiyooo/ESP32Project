using Windows.Media.Control;

namespace MusicDisplay;

public sealed class MediaSessionService : IAsyncDisposable
{
    private static readonly string[] MusicAppIds =
    [
        "QQMusic.exe", "Spotify.exe", "AppleMusic.exe", "Music.UI.exe",
        "cloudmusic.exe", "KuGou.exe", "KuwoMusic.exe", "foobar2000.exe",
        "MusicBee.exe", "AIMP.exe"
    ];

    private readonly GlobalSystemMediaTransportControlsSessionManager _manager;
    private readonly SemaphoreSlim _refreshLock = new(1, 1);
    private GlobalSystemMediaTransportControlsSession? _session;
    private MediaSnapshot? _lastSnapshot;
    private string? _lastLoggedTrackId;
    private bool? _lastLoggedPlaying;
    private bool _lastLoggedNoSession;
    private bool _hasPublished;
    private bool _disposed;

    private MediaSessionService(GlobalSystemMediaTransportControlsSessionManager manager)
    {
        _manager = manager;
        _manager.CurrentSessionChanged += OnCurrentSessionChanged;
        AttachSession(SelectMusicSession());
    }

    public event EventHandler<MediaSnapshot?>? SnapshotChanged;
    public MediaSnapshot? LatestSnapshot { get; private set; }

    public static async Task<MediaSessionService> CreateAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
        return new MediaSessionService(manager);
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        await RefreshAsync("startup", cancellationToken);
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        while (await timer.WaitForNextTickAsync(cancellationToken))
            await RefreshAsync("poll", cancellationToken);
    }

    private void OnCurrentSessionChanged(
        GlobalSystemMediaTransportControlsSessionManager sender,
        CurrentSessionChangedEventArgs args)
    {
        AttachSession(SelectMusicSession());
        _ = RefreshSafelyAsync("current-session");
    }

    private void AttachSession(GlobalSystemMediaTransportControlsSession? session)
    {
        if (ReferenceEquals(_session, session)) return;
        if (_session is not null)
        {
            _session.MediaPropertiesChanged -= OnMediaPropertiesChanged;
            _session.PlaybackInfoChanged -= OnPlaybackInfoChanged;
            _session.TimelinePropertiesChanged -= OnTimelinePropertiesChanged;
        }
        _session = session;
        if (_session is not null)
        {
            _session.MediaPropertiesChanged += OnMediaPropertiesChanged;
            _session.PlaybackInfoChanged += OnPlaybackInfoChanged;
            _session.TimelinePropertiesChanged += OnTimelinePropertiesChanged;
            Console.WriteLine($"Selected music session: {_session.SourceAppUserModelId}");
        }
    }

    private GlobalSystemMediaTransportControlsSession? SelectMusicSession()
    {
        var current = _manager.GetCurrentSession();
        return _manager.GetSessions()
            .Where(session => IsMusicApp(session.SourceAppUserModelId))
            .OrderByDescending(session =>
                session.GetPlaybackInfo().PlaybackStatus ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
            .ThenByDescending(session => ReferenceEquals(session, current))
            .ThenByDescending(session =>
                string.Equals(session.SourceAppUserModelId, "QQMusic.exe",
                    StringComparison.OrdinalIgnoreCase))
            .FirstOrDefault();
    }

    private static bool IsMusicApp(string? sourceAppId) =>
        !string.IsNullOrWhiteSpace(sourceAppId) && MusicAppIds.Any(appId =>
            sourceAppId.EndsWith(appId, StringComparison.OrdinalIgnoreCase));

    private void OnMediaPropertiesChanged(GlobalSystemMediaTransportControlsSession sender,
        MediaPropertiesChangedEventArgs args) => _ = RefreshSafelyAsync("media");

    private void OnPlaybackInfoChanged(GlobalSystemMediaTransportControlsSession sender,
        PlaybackInfoChangedEventArgs args) => _ = RefreshSafelyAsync("playback");

    private void OnTimelinePropertiesChanged(GlobalSystemMediaTransportControlsSession sender,
        TimelinePropertiesChangedEventArgs args) => _ = RefreshSafelyAsync("timeline");

    private async Task RefreshSafelyAsync(string reason)
    {
        try { await RefreshAsync(reason, CancellationToken.None); }
        catch (Exception exception) { Console.Error.WriteLine($"Media refresh failed: {exception.Message}"); }
    }

    private async Task RefreshAsync(string reason, CancellationToken cancellationToken)
    {
        await _refreshLock.WaitAsync(cancellationToken);
        try
        {
            AttachSession(SelectMusicSession());
            var session = _session;
            if (session is null)
            {
                Publish(null, reason);
                return;
            }

            var properties = await session.TryGetMediaPropertiesAsync();
            var playback = session.GetPlaybackInfo();
            var timeline = session.GetTimelineProperties();
            var duration = timeline.EndTime > timeline.StartTime
                ? timeline.EndTime - timeline.StartTime : TimeSpan.Zero;
            var snapshot = new MediaSnapshot(
                session.SourceAppUserModelId ?? string.Empty,
                TrackIdentity.Create(properties.Title ?? string.Empty,
                    properties.Artist ?? string.Empty, properties.AlbumTitle ?? string.Empty, duration),
                properties.Title ?? string.Empty,
                properties.Artist ?? string.Empty,
                properties.AlbumTitle ?? string.Empty,
                timeline.Position,
                duration,
                playback.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing,
                properties.Thumbnail);
            Publish(snapshot, reason);
        }
        finally { _refreshLock.Release(); }
    }

    private void Publish(MediaSnapshot? snapshot, string reason)
    {
        if (_hasPublished && snapshot == _lastSnapshot) return;
        _hasPublished = true;
        _lastSnapshot = snapshot;
        LatestSnapshot = snapshot;
        SnapshotChanged?.Invoke(this, snapshot);
        if (snapshot is null)
        {
            if (!_lastLoggedNoSession)
                Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] ({reason}) no active media session");
            _lastLoggedNoSession = true;
            _lastLoggedTrackId = null;
            _lastLoggedPlaying = null;
            return;
        }
        _lastLoggedNoSession = false;
        var trackChanged = !string.Equals(_lastLoggedTrackId, snapshot.TrackId,
            StringComparison.Ordinal);
        var playbackChanged = _lastLoggedPlaying != snapshot.Playing;
        if (!trackChanged && !playbackChanged) return;
        _lastLoggedTrackId = snapshot.TrackId;
        _lastLoggedPlaying = snapshot.Playing;
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] ({reason}) {snapshot.Title} — {snapshot.Artist}");
        Console.WriteLine($"  app={snapshot.SourceAppId} album={snapshot.Album} id={snapshot.TrackId}");
        Console.WriteLine($"  position={snapshot.Position:c} duration={snapshot.Duration:c} " +
                          $"playing={snapshot.Playing} thumbnail={snapshot.HasThumbnail}");
    }

    public ValueTask DisposeAsync()
    {
        if (_disposed) return ValueTask.CompletedTask;
        _disposed = true;
        _manager.CurrentSessionChanged -= OnCurrentSessionChanged;
        AttachSession(null);
        _refreshLock.Dispose();
        return ValueTask.CompletedTask;
    }
}
