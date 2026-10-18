#pragma once
#include "display_driver.h"
#include "lyrics_marquee.h"
#include "track_state.h"

class UiRenderer {
 public:
  UiRenderer(DisplayDriver& display, LyricsMarquee& marquee)
      : marquee_(marquee), display_(display) {}
  void release();
  void drawDemo(bool longText = false);
  void drawIdle(const char* status, const String& ip);
  void drawProvisioning(const String& ssid, const char* password, const String& ip);
  void drawTrack(const TrackState& state);
  void drawLyricsPreview(const TrackState& state);
  void drawLyricsWindow(const String& previous, const String& current,
                        const String& next, bool hasLyrics,
                        bool playing, uint32_t now);
  void drawProgress(uint32_t positionMs, uint32_t durationMs);
  bool drawLyricsBitmap(char role, const uint8_t* alpha4, size_t length,
                        int width, int height, bool playing, uint32_t now);
 private:
  String fitText(const char* text, int width, bool small, bool bold, float scale);
  void drawLine(const char* text, int y, const lgfx::IFont* font,
                uint16_t color, bool centered);
  void drawMetadataLine(const char* text, int y, const lgfx::IFont* font,
                        uint16_t color);
  void drawCoverPlaceholder(const char* label);
  bool drawMetadataBitmap(char role, const uint8_t* alpha4, size_t length,
                          int width, int height);
  bool renderMetadataBitmap(int spriteHeight, const uint8_t* alpha4, size_t length,
                            int width, int height, int targetY, uint16_t color);
  LyricsMarquee& marquee_;
  DisplayDriver& display_;
  bool bitmapLyricsActive_ = false;
};
