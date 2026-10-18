namespace MusicDisplay;

using Windows.Storage.Streams;

public sealed record MediaSnapshot(
    string SourceAppId,
    string TrackId,
    string Title,
    string Artist,
    string Album,
    TimeSpan Position,
    TimeSpan Duration,
    bool Playing,
    IRandomAccessStreamReference? Thumbnail)
{
    public bool HasThumbnail => Thumbnail is not null;
}
