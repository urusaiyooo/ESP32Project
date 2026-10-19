#pragma once

#include "display_driver.h"
#include "compact_fonts.h"

namespace font_fallback {

struct Codepoint {
  uint32_t value;
  uint8_t length;
};

inline Codepoint decode(const char* text) {
  const uint8_t b0 = static_cast<uint8_t>(text[0]);
  if (!b0) return {0, 0};
  if (b0 < 0x80) return {b0, 1};
  const uint8_t b1 = static_cast<uint8_t>(text[1]);
  if ((b0 & 0xE0) == 0xC0 && (b1 & 0xC0) == 0x80) {
    return {static_cast<uint32_t>(b0 & 0x1F) << 6 | (b1 & 0x3F), 2};
  }
  const uint8_t b2 = static_cast<uint8_t>(text[2]);
  if ((b0 & 0xF0) == 0xE0 && (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
    return {static_cast<uint32_t>(b0 & 0x0F) << 12 |
                static_cast<uint32_t>(b1 & 0x3F) << 6 | (b2 & 0x3F), 3};
  }
  const uint8_t b3 = static_cast<uint8_t>(text[3]);
  if ((b0 & 0xF8) == 0xF0 && (b1 & 0xC0) == 0x80 &&
      (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
    return {static_cast<uint32_t>(b0 & 0x07) << 18 |
                static_cast<uint32_t>(b1 & 0x3F) << 12 |
                static_cast<uint32_t>(b2 & 0x3F) << 6 | (b3 & 0x3F), 4};
  }
  return {'?', 1};
}

inline const lgfx::IFont* chooseFont(uint32_t cp, bool bold, bool small) {
  const lgfx::IFont* candidates[]={
    small ? static_cast<const lgfx::IFont*>(&lgfx::fonts::efontCN_16) : &lgfx::fonts::efontCN_24,
    small ? static_cast<const lgfx::IFont*>(&compact_fonts::ja16) : &compact_fonts::ja24,
    small ? static_cast<const lgfx::IFont*>(&compact_fonts::tw16) : &compact_fonts::tw24};
  for(auto font:candidates) {
    lgfx::FontMetrics metric{};font->getDefaultMetric(&metric);
    if(cp<=0xffff && font->updateFontMetric(&metric,static_cast<uint16_t>(cp)))return font;
  }
  return candidates[0];
}
inline const lgfx::IFont* chooseFont(uint32_t cp, bool bold) {return chooseFont(cp,bold,false);}

template <typename Gfx>
int measure(Gfx& gfx, const char* text, bool small, bool bold, float scale = 1.0f) {
  int width = 0;
  for (const char* cursor = text; *cursor;) {
    const Codepoint cp = decode(cursor);
    if (!cp.length) break;
    String glyph;
    glyph.reserve(cp.length);
    for (uint8_t i = 0; i < cp.length; ++i) glyph += cursor[i];
    gfx.setFont(chooseFont(cp.value, bold, small));
    gfx.setTextSize(scale);
    width += gfx.textWidth(glyph.c_str());
    cursor += cp.length;
  }
  return width;
}

template <typename Gfx>
void draw(Gfx& gfx, const char* text, int x, int y, bool small, bool bold,
          uint16_t color, uint16_t background, float scale = 1.0f) {
  gfx.setTextColor(color, background);
  for (const char* cursor = text; *cursor;) {
    const Codepoint cp = decode(cursor);
    if (!cp.length) break;
    String glyph;
    glyph.reserve(cp.length);
    for (uint8_t i = 0; i < cp.length; ++i) glyph += cursor[i];
    gfx.setFont(chooseFont(cp.value, bold, small));
    gfx.setTextSize(scale);
    gfx.drawString(glyph.c_str(), x, y);
    x += gfx.textWidth(glyph.c_str());
    cursor += cp.length;
  }
}

}  // namespace font_fallback
