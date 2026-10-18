#pragma once
#include <stdint.h>

// All board/module assumptions live here. Verify against the actual module.
namespace display_config {
enum class PanelModel { ILI9488, ILI9486, ILI9481 };
constexpr PanelModel kPanel = PanelModel::ILI9488;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr int kCs = 10;
constexpr int kReset = 8;
constexpr int kDc = 9;
constexpr int kMosi = 11;
constexpr int kSck = 12;
constexpr int kMiso = -1; // LCD SDO is disconnected; this bus is write-only.
#else
constexpr int kCs = 27;
constexpr int kReset = 25;  // Standard active-low RESET, driven by LovyanGFX.
constexpr int kDc = 26;
constexpr int kMosi = 23;
constexpr int kSck = 18;
constexpr int kMiso = 19;
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint32_t kSpiWriteHz = 40000000; // Verified with this S3, panel and LCD wiring.
constexpr uint32_t kSpiReadHz = 1000000;
#else
constexpr uint32_t kSpiWriteHz = 40000000;
constexpr uint32_t kSpiReadHz = 16000000;
#endif
constexpr uint8_t kSpiMode = 0;
constexpr uint8_t kRotation = 1; // Landscape: 1 or 3; 5/7 also mirror.
constexpr uint8_t kRotationOffset = 0;
constexpr bool kRgbOrder = false; // Toggle if RED and BLUE are swapped.
constexpr bool kInvert = true; // Actual IPS module showed inverted black/white with false.
constexpr bool kReadable = false; // Do not depend on a connected SDO line.
constexpr int kNativeWidth = 320;
constexpr int kNativeHeight = 480;
constexpr int kWidth = 480;
constexpr int kHeight = 320;
// ADS7843 uses independent HSPI pins; LCD wiring remains unchanged.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr int kTouchSck = 16;
constexpr int kTouchMosi = 17;
constexpr int kTouchMiso = 18;
constexpr int kTouchCs = 15;
constexpr int kTouchIrq = 7;
#else
constexpr int kTouchSck = 32;
constexpr int kTouchMosi = 21;
constexpr int kTouchMiso = 34; // Input-only GPIO is suitable for MISO.
constexpr int kTouchCs = 33;
constexpr int kTouchIrq = 22;
#endif
constexpr uint32_t kTouchHz = 1000000;
static_assert(kTouchSck != kSck && kTouchMosi != kMosi &&
              kTouchMiso != kMiso && kTouchCs != kCs,
              "Touch wiring must be independent of LCD wiring");

constexpr bool pinsAreDistinct(const int* pins, unsigned count,
                              unsigned i = 0, unsigned j = 1) {
  return i >= count ? true : j >= count ? pinsAreDistinct(pins, count, i + 1, i + 2) :
      (pins[i] < 0 || pins[j] < 0 || pins[i] != pins[j]) &&
      pinsAreDistinct(pins, count, i, j + 1);
}
constexpr int kSignalPins[] = {kCs, kReset, kDc, kMosi, kSck, kMiso,
    kTouchCs, kTouchSck, kTouchMosi, kTouchMiso, kTouchIrq};
static_assert(pinsAreDistinct(kSignalPins, sizeof(kSignalPins) / sizeof(kSignalPins[0])),
              "LCD and touch signal pins must not overlap");

enum class BacklightMode { ALWAYS_ON, GPIO, MODULE_CONTROLLED };
constexpr BacklightMode kBacklightMode = BacklightMode::MODULE_CONTROLLED;
// GPIO mode is ONLY for a verified 3.3 V logic input on a module driver.
// Never drive a raw LED backlight directly from an ESP32 GPIO.
constexpr int kBacklightPin = -1;
constexpr bool kBacklightActiveHigh = true;
static_assert(kBacklightMode != BacklightMode::GPIO ||
              (kBacklightPin >= 0 && kBacklightPin <= 33),
              "GPIO backlight mode requires a valid output GPIO");
static_assert(kRotation <= 7 && (kRotation % 2) == 1,
              "Use landscape rotation 1, 3, 5 or 7");
static_assert(kSpiWriteHz >= 1000000 && kSpiWriteHz <= 40000000,
              "Hardware validation frequency must be between 1 and 40 MHz");
}
