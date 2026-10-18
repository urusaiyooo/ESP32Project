#pragma once
#include <Arduino.h>

struct TrackState {
  String trackId;
  String title;
  String artist;
  String album;
  uint32_t durationMs = 0;
  uint32_t basePositionMs = 0;
  uint32_t baseLocalMs = 0;
  uint32_t lastPlaybackMs = 0;
  bool playing = false;
  bool active = false;
  uint16_t lyricLineCount = 0;
  String lyricPreview[3];

  void setTrack(const String& id, const String& newTitle, const String& newArtist,
                const String& newAlbum, uint32_t duration, uint32_t now);
  void setPlayback(uint32_t position, uint32_t duration, bool isPlaying, uint32_t now);
  uint32_t estimatedPosition(uint32_t now) const;
  void clear();
  bool matches(const String& id) const { return active && !id.isEmpty() && id == trackId; }
};

String sanitizeUtf8(const String& input, size_t maxBytes);
