#pragma once
#include <memory>
#include "display_driver.h"

class LyricsMarquee {
 public:
  explicit LyricsMarquee(DisplayDriver& display) : sprite_(&display) {}
  bool begin();
  void release();
  void setLine(const String& text, bool playing, uint32_t now);
  bool setBitmap(const uint8_t* alpha4, size_t length, int width, int height,
                 bool playing, uint32_t now);
  void drawBitmapLine(const uint8_t* alpha4, size_t length, int width, int height,
                      int targetY, uint16_t color, bool centered);
  void setPlaying(bool playing, uint32_t now);
  void tick(uint32_t now);
  void clear(bool redraw = true);
  bool ready() const { return ready_; }

 private:
  enum class Mode : uint8_t { Static24, Static16, Marquee16 };
  enum class Phase : uint8_t { StartPause, Forward, EndPause, Backward };

  void selectMode();
  void draw();
  void renderBitmap(const uint8_t* alpha4, size_t length, int width, int height,
                    int sourceX, uint16_t color);
  lgfx::LGFX_Sprite sprite_;
  String text_;
  std::unique_ptr<uint8_t[]> bitmap_;
  size_t bitmapLength_ = 0;
  size_t bitmapCapacity_ = 0;
  int bitmapWidth_ = 0;
  int bitmapHeight_ = 0;
  bool bitmapMode_ = false;
  Mode mode_ = Mode::Static24;
  Phase phase_ = Phase::StartPause;
  bool ready_ = false;
  bool playing_ = false;
  int textWidth_ = 0;
  int offset_ = 0;
  uint32_t phaseStarted_ = 0;
  uint32_t lastFrame_ = 0;
  uint32_t pausedAt_ = 0;
};
