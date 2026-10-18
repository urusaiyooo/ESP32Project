#include "hub_console.h"
#include "lyrics_marquee.h"
#include "font_fallback.h"
#include "ui_config.h"

namespace {
uint16_t blend565(uint16_t background, uint16_t foreground, uint8_t alpha) {
  const uint8_t inverse = 15 - alpha;
  const uint16_t red = (((background >> 11) & 0x1F) * inverse +
                        ((foreground >> 11) & 0x1F) * alpha) / 15;
  const uint16_t green = (((background >> 5) & 0x3F) * inverse +
                          ((foreground >> 5) & 0x3F) * alpha) / 15;
  const uint16_t blue = ((background & 0x1F) * inverse +
                         (foreground & 0x1F) * alpha) / 15;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}
}

bool LyricsMarquee::begin() {
  sprite_.setColorDepth(16);
  if (!sprite_.createSprite(ui_config::kTextWidth, ui_config::kMarqueeHeight)) {
    hubConsole().println("[ERROR][MARQUEE] Sprite allocation failed; using static ellipsis fallback");
    return false;
  }
  sprite_.setTextDatum(lgfx::textdatum_t::top_left);
  sprite_.setTextWrap(false, false);
  ready_ = true;
  clear();
  hubConsole().printf("[INFO][MARQUEE] sprite=%dx%d depth=16 payload_bytes=%u\n",
                ui_config::kTextWidth, ui_config::kMarqueeHeight,
                ui_config::kTextWidth * ui_config::kMarqueeHeight * 2);
  return true;
}

void LyricsMarquee::setLine(const String& text, bool playing, uint32_t now) {
  bitmapMode_ = false;
  bitmapLength_ = 0;
  bitmapWidth_ = 0;
  bitmapHeight_ = 0;
  // Keep the allocated alpha buffer for the next Companion bitmap. Resetting
  // the pointer while retaining bitmapCapacity_ made the next setBitmap()
  // memcpy into nullptr after a track change.
  const String next = text.isEmpty() ? String("♪") : text;
  if (text_ == next) {
    setPlaying(playing, now);
    if (ready_) draw();
    return;
  }
  text_ = next;
  offset_ = 0;
  phase_ = Phase::StartPause;
  phaseStarted_ = now;
  lastFrame_ = now;
  pausedAt_ = playing ? 0 : now;
  playing_ = playing;
  if (!ready_) return;
  selectMode();
  draw();
}

bool LyricsMarquee::setBitmap(const uint8_t* alpha4, size_t length, int width, int height,
                              bool playing, uint32_t now) {
  if (!ready_ || !alpha4 || width <= 0 || height <= 0 || height > ui_config::kMarqueeHeight ||
      length != (static_cast<size_t>(width) * height + 1) / 2) return false;
  if (bitmapCapacity_ < length) {
    std::unique_ptr<uint8_t[]> next(new (std::nothrow) uint8_t[length]);
    if (!next) return false;
    bitmap_ = std::move(next);
    bitmapCapacity_ = length;
  }
  memcpy(bitmap_.get(), alpha4, length);
  bitmapLength_ = length;
  bitmapWidth_ = width;
  bitmapHeight_ = height;
  bitmapMode_ = true;
  text_ = "";
  textWidth_ = width;
  mode_ = width <= ui_config::kTextWidth ? Mode::Static24 : Mode::Marquee16;
  offset_ = 0;
  phase_ = Phase::StartPause;
  phaseStarted_ = now;
  lastFrame_ = now;
  pausedAt_ = playing ? 0 : now;
  playing_ = playing;
  draw();
  return true;
}

void LyricsMarquee::drawBitmapLine(const uint8_t* alpha4, size_t length, int width, int height,
                                   int targetY, uint16_t color, bool centered) {
  if (!ready_) return;
  const int sourceX = centered && width < ui_config::kTextWidth
      ? -(ui_config::kTextWidth - width) / 2 : 0;
  renderBitmap(alpha4, length, width, height, sourceX, color);
  sprite_.pushSprite(ui_config::kMargin, targetY);
}

void LyricsMarquee::setPlaying(bool playing, uint32_t now) {
  if (playing_ == playing) return;
  if (!playing) {
    pausedAt_ = now;
    playing_ = false;
    return;
  }
  if (pausedAt_) phaseStarted_ += now - pausedAt_;
  pausedAt_ = 0;
  lastFrame_ = now;
  playing_ = true;
}

void LyricsMarquee::tick(uint32_t now) {
  if (!ready_ || mode_ != Mode::Marquee16 || !playing_) return;
  if (static_cast<uint32_t>(now - lastFrame_) < ui_config::kMarqueeFrameMs) return;
  lastFrame_ = now;
  const int maximum = max(0, textWidth_ - ui_config::kTextWidth);
  const uint32_t elapsed = now - phaseStarted_;
  const int previousOffset = offset_;
  if (phase_ == Phase::StartPause) {
    if (elapsed < ui_config::kMarqueePauseMs) return;
    phase_ = Phase::Forward;
    phaseStarted_ = now;
  } else if (phase_ == Phase::Forward) {
    offset_ = min(maximum, static_cast<int>(
        static_cast<uint64_t>(elapsed) * ui_config::kMarqueePixelsPerSecond / 1000));
    if (offset_ >= maximum) {
      phase_ = Phase::EndPause;
      phaseStarted_ = now;
    }
  } else if (phase_ == Phase::EndPause) {
    if (elapsed < ui_config::kMarqueePauseMs) return;
    phase_ = Phase::Backward;
    phaseStarted_ = now;
  } else {
    const int travelled = static_cast<int>(
        static_cast<uint64_t>(elapsed) * ui_config::kMarqueePixelsPerSecond / 1000);
    offset_ = max(0, maximum - travelled);
    if (offset_ <= 0) {
      phase_ = Phase::StartPause;
      phaseStarted_ = now;
    }
  }
  if (offset_ != previousOffset) draw();
}

void LyricsMarquee::clear(bool redraw) {
  text_ = "";
  bitmapMode_ = false;
  bitmapLength_ = 0;
  textWidth_ = 0;
  offset_ = 0;
  phase_ = Phase::StartPause;
  playing_ = false;
  pausedAt_ = 0;
  if (!ready_ || !redraw) return;
  sprite_.fillScreen(ui_config::kBackground);
  sprite_.pushSprite(ui_config::kMargin, ui_config::kCurrentY);
}

void LyricsMarquee::selectMode() {
  textWidth_ = font_fallback::measure(sprite_, text_.c_str(), false, true, 1.1f);
  if (textWidth_ <= ui_config::kTextWidth) {
    mode_ = Mode::Static24;
    return;
  }
  textWidth_ = font_fallback::measure(sprite_, text_.c_str(), true, false);
  mode_ = textWidth_ <= ui_config::kTextWidth ? Mode::Static16 : Mode::Marquee16;
}

void LyricsMarquee::draw() {
  if (bitmapMode_ && bitmap_) {
    int sourceX = mode_ == Mode::Marquee16 ? offset_ :
        -(ui_config::kTextWidth - bitmapWidth_) / 2;
    renderBitmap(bitmap_.get(), bitmapLength_, bitmapWidth_, bitmapHeight_,
                 sourceX, ui_config::kText);
    sprite_.pushSprite(ui_config::kMargin, ui_config::kCurrentY);
    return;
  }
  sprite_.fillScreen(ui_config::kBackground);
  sprite_.setTextColor(ui_config::kText, ui_config::kBackground);
  int x = 0;
  int y = 0;
  if (mode_ == Mode::Static24) {
    x = (ui_config::kTextWidth - textWidth_) / 2;
    y = 1;
    font_fallback::draw(sprite_, text_.c_str(), x, y, false, true,
                        ui_config::kText, ui_config::kBackground, 1.1f);
  } else {
    x = mode_ == Mode::Static16 ? (ui_config::kTextWidth - textWidth_) / 2 : -offset_;
    y = 6;
    font_fallback::draw(sprite_, text_.c_str(), x, y, true, false,
                        ui_config::kText, ui_config::kBackground);
  }
  sprite_.pushSprite(ui_config::kMargin, ui_config::kCurrentY);
}

void LyricsMarquee::renderBitmap(const uint8_t* alpha4, size_t length, int width, int height,
                                 int sourceX, uint16_t color) {
  if (!alpha4 || length != (static_cast<size_t>(width) * height + 1) / 2) return;
  const int yOffset = max(0, (ui_config::kMarqueeHeight - height) / 2);
  uint16_t row[ui_config::kTextWidth];
  // The row buffer contains host-order RGB565 values. LovyanGFX pushImage()
  // needs byte swapping enabled for uint16_t buffers; otherwise 0x0861 is
  // transferred as 0x6108 and the intended dark-gray background turns purple.
  const bool previousSwapBytes = sprite_.getSwapBytes();
  sprite_.setSwapBytes(true);
  uint16_t colors[16];
  for (uint8_t alpha = 0; alpha < 16; ++alpha) {
    colors[alpha] = alpha ? blend565(ui_config::kBackground, color, alpha)
                          : ui_config::kBackground;
  }
  for (int y = 0; y < ui_config::kMarqueeHeight; ++y) {
    for (int x = 0; x < ui_config::kTextWidth; ++x) {
      uint8_t alpha = 0;
      const int sourceY = y - yOffset;
      const int sx = sourceX + x;
      if (sourceY >= 0 && sourceY < height && sx >= 0 && sx < width) {
        const size_t index = static_cast<size_t>(sourceY) * width + sx;
        const uint8_t packed = alpha4[index / 2];
        alpha = (index & 1) ? packed & 0x0F : packed >> 4;
      }
      row[x] = colors[alpha];
    }
    sprite_.pushImage(0, y, ui_config::kTextWidth, 1, row);
  }
  sprite_.setSwapBytes(previousSwapBytes);
}

void LyricsMarquee::release() {
  clear(false); sprite_.deleteSprite(); ready_=false;
  bitmap_.reset(); bitmapCapacity_=0; bitmapWidth_=bitmapHeight_=0; text_=String();
}
