#include "cover_renderer.h"
#include "display_config.h"
#include "ui_config.h"

namespace {
bool isStartOfFrame(uint8_t marker) {
  return (marker >= 0xC0 && marker <= 0xC3) ||
         (marker >= 0xC5 && marker <= 0xC7) ||
         (marker >= 0xC9 && marker <= 0xCB) ||
         (marker >= 0xCD && marker <= 0xCF);
}
}

bool CoverRenderer::readDimensions(const uint8_t* data, size_t length,
                                   uint16_t& width, uint16_t& height,
                                   uint8_t& sofMarker) {
  width = 0;
  height = 0;
  sofMarker = 0;
  if (!data || length < 4 || data[0] != 0xFF || data[1] != 0xD8 ||
      data[length - 2] != 0xFF || data[length - 1] != 0xD9) return false;

  size_t cursor = 2;
  while (cursor + 1 < length) {
    while (cursor < length && data[cursor] != 0xFF) ++cursor;
    while (cursor < length && data[cursor] == 0xFF) ++cursor;
    if (cursor >= length) return false;
    const uint8_t marker = data[cursor++];
    if (marker == 0xD9 || marker == 0xDA) break;
    if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;
    if (cursor + 2 > length) return false;
    const uint16_t segmentLength = (static_cast<uint16_t>(data[cursor]) << 8) |
                                   data[cursor + 1];
    if (segmentLength < 2 || cursor + segmentLength > length) return false;
    if (isStartOfFrame(marker)) {
      if (segmentLength < 8) return false;
      height = (static_cast<uint16_t>(data[cursor + 3]) << 8) | data[cursor + 4];
      width = (static_cast<uint16_t>(data[cursor + 5]) << 8) | data[cursor + 6];
      sofMarker = marker;
      return width && height;
    }
    cursor += segmentLength;
  }
  return false;
}

CoverRenderResult CoverRenderer::render(const uint8_t* data, size_t length,
                                        uint16_t& width, uint16_t& height) {
  uint8_t sofMarker = 0;
  if (!readDimensions(data, length, width, height, sofMarker)) {
    return CoverRenderResult::InvalidJpeg;
  }
  // LovyanGFX's embedded decoder supports baseline DCT JPEG (SOF0).
  if (sofMarker != 0xC0) return CoverRenderResult::UnsupportedJpeg;
  if (width != ui_config::kCoverWidth || height != ui_config::kCoverHeight) {
    return CoverRenderResult::WrongDimensions;
  }
  display_.setTextDatum(lgfx::textdatum_t::top_left);
  if (!display_.drawJpg(data, static_cast<uint32_t>(length), 0, 0,
                        ui_config::kCoverWidth, ui_config::kCoverHeight)) {
    return CoverRenderResult::DecodeFailed;
  }
  return CoverRenderResult::Ok;
}
