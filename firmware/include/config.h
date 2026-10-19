#pragma once
#include <stdint.h>

namespace config {
constexpr uint32_t kSerialBaud = 460800;
constexpr uint32_t kColorHoldMs = 2500;
constexpr uint32_t kHeapReportMs = 5000;
constexpr uint32_t kPlaybackStaleMs = 10000;
constexpr uint32_t kIdleTimeoutMs = 60000;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr size_t kMaxLyricsBytes = 32768;
constexpr size_t kMaxCoverBytes = 80 * 1024;
constexpr char kVersion[] = "1.4.7-music-dashboard";
}
