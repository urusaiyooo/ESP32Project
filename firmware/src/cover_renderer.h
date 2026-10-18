#pragma once

#include <Arduino.h>
#include "display_driver.h"

enum class CoverRenderResult {
  Ok,
  InvalidJpeg,
  UnsupportedJpeg,
  WrongDimensions,
  DecodeFailed,
};

class CoverRenderer {
 public:
  explicit CoverRenderer(DisplayDriver& display) : display_(display) {}
  CoverRenderResult render(const uint8_t* data, size_t length,
                           uint16_t& width, uint16_t& height);

  static bool readDimensions(const uint8_t* data, size_t length,
                             uint16_t& width, uint16_t& height,
                             uint8_t& sofMarker);
 private:
  DisplayDriver& display_;
};
