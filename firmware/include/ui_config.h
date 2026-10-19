#pragma once
#include <stdint.h>

namespace ui_config {
constexpr uint16_t kBackground = 0xFFFF;
constexpr uint16_t kCoverBackground = 0xEEDC;
constexpr uint16_t kText = 0x18C5;
constexpr uint16_t kMuted = 0x8410;
constexpr uint16_t kAccent = 0x43D8;
constexpr uint16_t kProgressTrack = 0xE71C;
constexpr int kMargin = 16;
constexpr int kTextWidth = 448;
constexpr int kCoverX = 10;
constexpr int kCoverY = 36;
constexpr int kCoverWidth = 128;
constexpr int kCoverHeight = 128;
constexpr int kMetadataX = 176;
constexpr int kMetadataWidth = 288;
constexpr int kHeaderTitleY = 10;
constexpr int kHeaderArtistY = 64;
constexpr int kHeaderAlbumY = 98;
constexpr int kTitleY = 168;
constexpr int kArtistY = 198;
constexpr int kPreviousY = 174;
constexpr int kCurrentY = 211;
constexpr int kNextY = 266;
constexpr int kProgressY = 317;
constexpr int kProgressHeight = 3;
constexpr int kLyricsHeight = 124;
constexpr int kLyricLinePitch = 42;
constexpr int kMarqueeHeight = 42;
constexpr uint32_t kMarqueeFrameMs = 40;
constexpr uint32_t kMarqueePauseMs = 900;
constexpr uint32_t kMarqueePixelsPerSecond = 44;
constexpr uint32_t kAnimationFrameMs = 33;
constexpr uint32_t kAnimationDurationMs = 250; // Final SPI push adds about one frame.
constexpr uint32_t kDemoLineMs = 3000;
}
