#pragma once
#include <LovyanGFX.hpp>
#include <type_traits>
#include "display_config.h"

using ConfiguredPanel = std::conditional<
    display_config::kPanel == display_config::PanelModel::ILI9488,
    lgfx::Panel_ILI9488,
    std::conditional<display_config::kPanel == display_config::PanelModel::ILI9486,
                     lgfx::Panel_ILI9486, lgfx::Panel_ILI9481>::type>::type;

class DisplayDriver : public lgfx::LGFX_Device {
 public:
  DisplayDriver();
  bool beginDisplay();
  bool setTouchPolling(bool softwareSpi=false);
  void printDiagnostics();
  void softwareDisplayTest();
 private:
  lgfx::Bus_SPI bus_;
  ConfiguredPanel panel_;
  lgfx::Touch_XPT2046 touch_;
};
