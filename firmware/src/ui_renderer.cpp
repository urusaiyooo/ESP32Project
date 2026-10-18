#include "hub_console.h"
#include "ui_renderer.h"
#include "font_fallback.h"
#include "ui_config.h"

namespace {
// Remove a whole UTF-8 codepoint, never leave a partial Chinese character.
void removeLastCodepoint(String& text) {
  if (text.isEmpty()) return;
  size_t start = text.length() - 1;
  while (start > 0 && (static_cast<uint8_t>(text[start]) & 0xC0) == 0x80) --start;
  text.remove(start);
}

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

void UiRenderer::drawCoverPlaceholder(const char* label) {
  using namespace ui_config;
  display_.fillRect(0, 0, kCoverWidth, kCoverHeight, kCoverBackground);
  display_.drawCircle(80, 66, 38, kAccent);
  display_.drawCircle(80, 66, 28, kMuted);
  display_.fillCircle(80, 66, 6, kAccent);
  display_.setFont(&lgfx::fonts::Font0);
  display_.setTextSize(1);
  display_.setTextColor(kMuted, kCoverBackground);
  display_.drawCenterString(label, 80, 127);
}

String UiRenderer::fitText(const char* text, int width, bool small, bool bold, float scale) {
  String result(text);
  if (font_fallback::measure(display_, result.c_str(), small, bold, scale) <= width) {
    return result;
  }
  const char* suffix = "...";
  const int available = width - font_fallback::measure(display_, suffix, small, bold, scale);
  while (!result.isEmpty() &&
         font_fallback::measure(display_, result.c_str(), small, bold, scale) > available) {
    removeLastCodepoint(result);
  }
  return result + suffix;
}

void UiRenderer::drawLine(const char* text, int y, const lgfx::IFont* font,
                          uint16_t color, bool centered) {
  display_.setFont(font);
  display_.setTextSize(1);
  const bool small = font == &lgfx::fonts::efontCN_16;
  const bool bold = !small;
  const float scale = small ? 1.0f : 1.1f;
  const String fitted = fitText(text, ui_config::kTextWidth, small, bold, scale);
  const int fittedWidth = font_fallback::measure(display_, fitted.c_str(), small, bold, scale);
  const int x = centered ? (display_config::kWidth - fittedWidth) / 2
                         : ui_config::kMargin;
  font_fallback::draw(display_, fitted.c_str(), x, y, small, bold, color,
                      ui_config::kBackground, scale);
}

void UiRenderer::drawMetadataLine(const char* text, int y, const lgfx::IFont* font,
                                  uint16_t color) {
  using namespace ui_config;
  display_.fillRect(kMetadataX, y, kMetadataWidth, 31, kBackground);
  const bool small = font == &lgfx::fonts::efontCN_16;
  const bool bold = !small;
  const float scale = small ? 1.0f : 1.1f;
  const String fitted = fitText(text, kMetadataWidth, small, bold, scale);
  font_fallback::draw(display_, fitted.c_str(), kMetadataX, y, small, bold, color,
                      kBackground, scale);
}

void UiRenderer::drawDemo(bool longText) {
  using namespace ui_config;
  const uint32_t started = micros();
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  display_.setTextWrap(false, false);
  display_.fillScreen(kBackground);
  // A procedural placeholder avoids a JPEG buffer before the cover phase.
  drawCoverPlaceholder("MUSIC DISPLAY / UI DEMO");

  const char* title = longText
      ? "这是一首用于测试超长中文歌名自动缩小以及安全省略的演示歌曲标题"
      : "窗边的晚风";
  drawMetadataLine(title, kHeaderTitleY, &lgfx::fonts::efontCN_24, kText);
  drawMetadataLine("演示歌手", kHeaderArtistY, &lgfx::fonts::efontCN_16, kMuted);
  drawMetadataLine("方形封面布局", kHeaderAlbumY, &lgfx::fonts::efontCN_16, kMuted);
  // Original demo text; this phase does not read real playback or lyrics.
  drawLine("窗外的灯慢慢亮起来", kPreviousY, &lgfx::fonts::efontCN_16, kMuted, true);
  drawLine(longText ? "这是一行用于检验中文歌词超出屏幕宽度后是否能完整按字符截断的文字"
                    : "把今天的心事交给晚风",
           kCurrentY, &lgfx::fonts::efontCN_24, kText, true);
  drawLine("明天还有新的旋律", kNextY, &lgfx::fonts::efontCN_16, kMuted, true);
  display_.fillRect(0, kProgressY, display_config::kWidth, kProgressHeight, kProgressTrack);
  display_.fillRect(0, kProgressY, display_config::kWidth * 2 / 5, kProgressHeight, kAccent);
  hubConsole().printf("[INFO][UI] static_demo=%s draw_us=%u title_width=%d\n",
                longText ? "long" : "normal", static_cast<uint32_t>(micros() - started),
                kTextWidth);
}

void UiRenderer::drawIdle(const char* status, const String& ip) {
  using namespace ui_config;
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  display_.setTextWrap(false, false);
  display_.fillScreen(kBackground);
  bitmapLyricsActive_ = false;
  marquee_.clear(false);
  drawCoverPlaceholder("MUSIC DISPLAY");
  drawMetadataLine(status, kHeaderTitleY, &lgfx::fonts::efontCN_24, kText);
  drawMetadataLine(ip.isEmpty() ? "Wi-Fi：等待配置" : "Wi-Fi：已连接",
                   kHeaderArtistY, &lgfx::fonts::efontCN_16, kMuted);
  drawMetadataLine(ip.isEmpty() ? "等待网络" : ip.c_str(),
                   kHeaderAlbumY, &lgfx::fonts::efontCN_16, kMuted);
  drawLine("播放音乐后自动显示歌词", kCurrentY,
           &lgfx::fonts::efontCN_16, kMuted, true);
  drawProgress(0, 0);
}

void UiRenderer::drawProvisioning(const String& ssid, const char* password,
                                  const String& ip) {
  using namespace ui_config;
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  display_.setTextWrap(false, false);
  display_.fillScreen(kBackground);
  bitmapLyricsActive_ = false;
  marquee_.clear(false);
  drawCoverPlaceholder("WI-FI SETUP");
  drawMetadataLine("Wi-Fi 配网模式", kHeaderTitleY, &lgfx::fonts::efontCN_24, kText);
  drawMetadataLine(ssid.c_str(), kHeaderArtistY, &lgfx::fonts::efontCN_16, kMuted);
  const String passwordLine = String("密码：") + password;
  drawMetadataLine(passwordLine.c_str(), kHeaderAlbumY, &lgfx::fonts::efontCN_16, kMuted);
  drawLine("连接热点后打开浏览器", kPreviousY,
           &lgfx::fonts::efontCN_16, kMuted, true);
  drawLine(ip.c_str(), kCurrentY, &lgfx::fonts::efontCN_24, kText, true);
  drawLine("也可用启动器通过 USB 配网", kNextY,
           &lgfx::fonts::efontCN_16, kMuted, true);
  drawProgress(0, 0);
}

void UiRenderer::drawTrack(const TrackState& state) {
  using namespace ui_config;
  const uint32_t started = micros();
  bitmapLyricsActive_ = false;
  marquee_.clear(false);
  display_.fillRect(kMargin,kPreviousY,kTextWidth,kLyricsHeight,kBackground);
  drawMetadataLine(state.title.c_str(),kHeaderTitleY,&lgfx::fonts::efontCN_24,kText);
  drawMetadataLine(state.artist.c_str(),kHeaderArtistY,&lgfx::fonts::efontCN_16,kMuted);
  drawMetadataLine(state.album.c_str(),kHeaderAlbumY,&lgfx::fonts::efontCN_16,kMuted);
  drawCoverPlaceholder("USB MUSIC");
  drawProgress(0,state.durationMs);
  hubConsole().printf("[PERF] device_render_track_us=%u\n",static_cast<uint32_t>(micros()-started));
}

void UiRenderer::drawLyricsPreview(const TrackState& state) {
  using namespace ui_config;
  display_.fillRect(kMargin, kPreviousY, kTextWidth, kLyricsHeight, kBackground);
  if (!state.lyricLineCount) {
    if (marquee_.ready()) marquee_.setLine("暂无歌词", false, millis());
    else drawLine("暂无歌词", kCurrentY, &lgfx::fonts::efontCN_16, kText, true);
    return;
  }
  drawLine(state.lyricPreview[0].c_str(), kPreviousY, &lgfx::fonts::efontCN_16, kMuted, true);
  const String current = state.lyricLineCount > 1 ? state.lyricPreview[1] : String("♪");
  if (marquee_.ready()) marquee_.setLine(current, state.playing, millis());
  else drawLine(current.c_str(), kCurrentY, &lgfx::fonts::efontCN_16, kText, true);
  drawLine(state.lyricLineCount > 2 ? state.lyricPreview[2].c_str() : "",
           kNextY, &lgfx::fonts::efontCN_16, kMuted, true);
}

void UiRenderer::drawLyricsWindow(const String& previous, const String& current,
                                  const String& next, bool hasLyrics,
                                  bool playing, uint32_t now) {
  using namespace ui_config;
  if (bitmapLyricsActive_) return;
  display_.fillRect(kMargin, kPreviousY, kTextWidth, kLyricsHeight, kBackground);
  if (!hasLyrics) {
    if (marquee_.ready()) marquee_.setLine("暂无歌词", false, now);
    else drawLine("暂无歌词", kCurrentY, &lgfx::fonts::efontCN_16, kText, true);
    return;
  }
  drawLine(previous.c_str(), kPreviousY, &lgfx::fonts::efontCN_16, kMuted, true);
  const String highlighted = current.isEmpty() ? String("♪") : current;
  if (marquee_.ready()) marquee_.setLine(highlighted, playing, now);
  else drawLine(highlighted.c_str(), kCurrentY, &lgfx::fonts::efontCN_16, kText, true);
  drawLine(next.c_str(), kNextY, &lgfx::fonts::efontCN_16, kMuted, true);
}

bool UiRenderer::drawLyricsBitmap(char role, const uint8_t* alpha4, size_t length,
                                  int width, int height, bool playing, uint32_t now) {
  using namespace ui_config;
  if (role == 't' || role == 'a' || role == 'b')
    return drawMetadataBitmap(role, alpha4, length, width, height);
  bitmapLyricsActive_ = true;
  if (role == 'c') return marquee_.setBitmap(alpha4, length, width, height, playing, now);
  if (role == 'p') {
    marquee_.drawBitmapLine(alpha4, length, width, height, kPreviousY, kMuted, true);
    return true;
  }
  if (role == 'n') {
    marquee_.drawBitmapLine(alpha4, length, width, height, kNextY, kMuted, true);
    return true;
  }
  return false;
}

bool UiRenderer::drawMetadataBitmap(char role, const uint8_t* alpha4, size_t length,
                                    int width, int height) {
  using namespace ui_config;
  if (role == 't')
    return renderMetadataBitmap(40, alpha4, length,
                                width, height, kHeaderTitleY, kText);
  if (role == 'a')
    return renderMetadataBitmap(28, alpha4, length,
                                width, height, kHeaderArtistY, kMuted);
  if (role == 'b')
    return renderMetadataBitmap(28, alpha4, length,
                                width, height, kHeaderAlbumY, kMuted);
  return false;
}

bool UiRenderer::renderMetadataBitmap(int spriteHeight, const uint8_t* alpha4, size_t length,
                                      int width, int height, int targetY, uint16_t color) {
  using namespace ui_config;
  if (!alpha4 || width <= 0 || width > kMetadataWidth || height <= 0 ||
      height > spriteHeight || length != (static_cast<size_t>(width) * height + 1) / 2)
    return false;
  // Metadata is static between updates. A scanline buffer saves the 39 KiB
  // retained by title/artist sprites, leaving room for long lyric JSON uploads.
  uint16_t row[kMetadataWidth];
  const int yOffset = max(0, (spriteHeight - height) / 2);
  const bool previousSwap=display_.getSwapBytes();
  display_.setSwapBytes(true);
  for (int y = 0; y < spriteHeight; ++y) {
    for (int x = 0; x < kMetadataWidth; ++x) {
      uint8_t alpha=0;
      const int sourceY=y-yOffset;
      if (x<width && sourceY>=0 && sourceY<height) {
        const size_t index=static_cast<size_t>(sourceY)*width+x;
        const uint8_t packed=alpha4[index/2];
        alpha=(index&1) ? packed&0x0F : packed>>4;
      }
      row[x]=alpha ? blend565(kBackground,color,alpha) : kBackground;
    }
    display_.pushImage(kMetadataX,targetY+y,kMetadataWidth,1,row);
  }
  display_.setSwapBytes(previousSwap);
  return true;
}

void UiRenderer::drawProgress(uint32_t positionMs, uint32_t durationMs) {
  using namespace ui_config;
  display_.fillRect(0, kProgressY, display_config::kWidth, kProgressHeight, kProgressTrack);
  if (!durationMs) return;
  const uint32_t width = static_cast<uint64_t>(min(positionMs, durationMs)) *
                         display_config::kWidth / durationMs;
  if (width) display_.fillRect(0, kProgressY, width, kProgressHeight, kAccent);
}

void UiRenderer::release() {
  bitmapLyricsActive_=false;
}
