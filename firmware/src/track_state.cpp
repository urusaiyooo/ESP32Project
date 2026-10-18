#include "track_state.h"

String sanitizeUtf8(const String& input, size_t maxBytes) {
  String out;
  out.reserve(min(input.length(), maxBytes));
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
  size_t i = 0;
  while (i < input.length() && out.length() < maxBytes) {
    const uint8_t lead = bytes[i];
    size_t count = 0;
    if (lead < 0x80) count = (lead >= 0x20 || lead == '\t') ? 1 : 0;
    else if ((lead & 0xE0) == 0xC0) count = 2;
    else if ((lead & 0xF0) == 0xE0) count = 3;
    else if ((lead & 0xF8) == 0xF0) count = 4;
    if (count == 0 || i + count > input.length() || out.length() + count > maxBytes) {
      if (count == 0) ++i;
      else break;
      continue;
    }
    bool valid = true;
    for (size_t j = 1; j < count; ++j) valid &= (bytes[i + j] & 0xC0) == 0x80;
    if (!valid) { ++i; continue; }
    for (size_t j = 0; j < count; ++j) out += static_cast<char>(bytes[i + j]);
    i += count;
  }
  return out;
}

void TrackState::setTrack(const String& id, const String& newTitle, const String& newArtist,
                          const String& newAlbum, uint32_t duration, uint32_t now) {
  trackId = sanitizeUtf8(id, 96);
  title = sanitizeUtf8(newTitle, 192);
  artist = sanitizeUtf8(newArtist, 160);
  album = sanitizeUtf8(newAlbum, 160);
  durationMs = duration;
  basePositionMs = 0;
  baseLocalMs = now;
  lastPlaybackMs = now;
  playing = false;
  active = true;
  lyricLineCount = 0;
  for (auto& line : lyricPreview) line = "";
}

void TrackState::setPlayback(uint32_t position, uint32_t duration, bool isPlaying, uint32_t now) {
  durationMs = duration ? duration : durationMs;
  basePositionMs = durationMs ? min(position, durationMs) : position;
  baseLocalMs = now;
  lastPlaybackMs = now;
  playing = isPlaying;
}

uint32_t TrackState::estimatedPosition(uint32_t now) const {
  uint32_t position = basePositionMs;
  if (playing) position += static_cast<uint32_t>(now - baseLocalMs);
  return durationMs ? min(position, durationMs) : position;
}

void TrackState::clear() {
  *this = TrackState{};
}
