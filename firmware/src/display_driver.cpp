#include "hub_console.h"
#include "display_driver.h"
#include <Arduino.h>

DisplayDriver::DisplayDriver() {
  using namespace display_config;
  auto bus = bus_.config();
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  bus.spi_host = SPI2_HOST;
#else
  bus.spi_host = VSPI_HOST;
#endif
  bus.spi_mode = kSpiMode;
  bus.freq_write = kSpiWriteHz;
  bus.freq_read = kSpiReadHz;
  bus.spi_3wire = false;
  bus.use_lock = true;
  bus.dma_channel = SPI_DMA_CH_AUTO;
  bus.pin_sclk = kSck;
  bus.pin_mosi = kMosi;
  bus.pin_miso = kMiso;
  bus.pin_dc = kDc;
  bus_.config(bus);
  panel_.setBus(&bus_);
  auto panel = panel_.config();
  panel.pin_cs = kCs;
  panel.pin_rst = kReset;
  panel.pin_busy = -1;
  panel.panel_width = kNativeWidth;
  panel.panel_height = kNativeHeight;
  panel.memory_width = kNativeWidth;
  panel.memory_height = kNativeHeight;
  panel.offset_rotation = kRotationOffset;
  panel.rgb_order = kRgbOrder;
  panel.invert = kInvert;
  panel.readable = kReadable;
  panel.bus_shared = false;
  panel_.config(panel);
  auto touch = touch_.config();
  touch.pin_cs = kTouchCs;
  touch.pin_int = -1; // Poll ADS7843 pressure; PEN may stay high on this module.
  touch.bus_shared = false;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  touch.spi_host = SPI3_HOST;
#else
  touch.spi_host = HSPI_HOST;
#endif
  touch.freq = kTouchHz;
  touch.pin_sclk = kTouchSck;
  touch.pin_mosi = kTouchMosi;
  touch.pin_miso = kTouchMiso;
  touch_.config(touch);
  panel_.setTouch(&touch_);
  setPanel(&panel_);
}

bool DisplayDriver::beginDisplay() {
  using namespace display_config;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // Use the bounded reset sequence already verified by the S3 screen test.
  digitalWrite(kCs,HIGH);pinMode(kCs,OUTPUT);
  digitalWrite(kReset,HIGH);pinMode(kReset,OUTPUT);delay(20);
  digitalWrite(kReset,LOW);delay(20);
  digitalWrite(kReset,HIGH);delay(150);
#endif
  if (!init()) return false;
  setRotation(kRotation);
  setColorDepth(16);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // Wake after both SPI buses and the touch controller have been initialized.
  panel_.setSleep(false);delay(150);
  panel_.setPowerSave(false);
  startWrite();writeCommand(0x29);endWrite();delay(50);
#endif
  if (kBacklightMode == BacklightMode::GPIO) {
    digitalWrite(kBacklightPin, kBacklightActiveHigh ? HIGH : LOW);
    pinMode(kBacklightPin, OUTPUT);
  }
  return width() == kWidth && height() == kHeight;
}

bool DisplayDriver::setTouchPolling(bool softwareSpi) {
  auto cfg=touch_.config();cfg.pin_int=-1;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  cfg.spi_host=softwareSpi ? -1 : SPI3_HOST;
#else
  cfg.spi_host=softwareSpi ? -1 : HSPI_HOST;
#endif
  touch_.config(cfg);
  return touch_.init();
}

void DisplayDriver::printDiagnostics() {
  auto b=bus_.config();auto t=touch_.config();auto p=panel_.config();
  hubConsole().printf("DISPLAY_HW chip=%s flash=%u psram=%u host=%d cs=%d rst=%d dc=%d mosi=%d sck=%d miso=%d hz=%u width=%d height=%d\n",
    ESP.getChipModel(),ESP.getFlashChipSize(),ESP.getPsramSize(),b.spi_host,p.pin_cs,p.pin_rst,
    b.pin_dc,b.pin_mosi,b.pin_sclk,b.pin_miso,b.freq_write,width(),height());
  hubConsole().printf("TOUCH_HW host=%d cs=%d sck=%d mosi=%d miso=%d irq=%d\n",
    t.spi_host,t.pin_cs,t.pin_sclk,t.pin_mosi,t.pin_miso,t.pin_int);
  if (b.pin_miso < 0) {
    hubConsole().println("DISPLAY_READ skipped: LCD SDO disconnected (write-only); visual verification required");
    return;
  }
  uint32_t id=panel_.readCommand(0xD3,0,4);
  uint32_t manufacturer=panel_.readCommand(0x04,0,4);
  auto shortReadConfig=p;shortReadConfig.dummy_read_bits=0;panel_.config(shortReadConfig);
  uint32_t power=panel_.readCommand(0x0A,0,2);
  hubConsole().printf("DISPLAY_READ id=%08X manufacturer=%08X power=%08X rst_level=%d\n",
    id,manufacturer,power,digitalRead(p.pin_rst));
  uint32_t madctl=panel_.readCommand(0x0B,0,1);
  startWrite();writeCommand(0x3A);writeData(static_cast<uint8_t>(0x55));endWrite();
  uint32_t format55=panel_.readCommand(0x0C,0,1);
  startWrite();writeCommand(0x3A);writeData(static_cast<uint8_t>(0x66));endWrite();
  uint32_t format66=panel_.readCommand(0x0C,0,1);
  hubConsole().printf("DISPLAY_REGISTER_TEST madctl=%02X wrote55_read=%02X wrote66_read=%02X\n",madctl,format55,format66);
  panel_.config(p);
}

void DisplayDriver::softwareDisplayTest() {
  using namespace display_config;
  waitDMA();bus_.release();
  digitalWrite(kCs,HIGH);pinMode(kCs,OUTPUT);
  digitalWrite(kDc,HIGH);pinMode(kDc,OUTPUT);
  digitalWrite(kSck,LOW);pinMode(kSck,OUTPUT);
  digitalWrite(kMosi,LOW);pinMode(kMosi,OUTPUT);
  if (kMiso >= 0) pinMode(kMiso,INPUT);
  auto send=[&](uint8_t value) {
    for(int bit=7;bit>=0;--bit) {
      digitalWrite(kMosi,(value>>bit)&1);delayMicroseconds(1);
      digitalWrite(kSck,HIGH);delayMicroseconds(1);digitalWrite(kSck,LOW);
    }
  };
  auto command=[&](uint8_t cmd,const uint8_t* data,int length) {
    digitalWrite(kCs,LOW);digitalWrite(kDc,LOW);send(cmd);digitalWrite(kDc,HIGH);
    for(int i=0;i<length;++i) send(data[i]);
    digitalWrite(kCs,HIGH);
  };
  auto read=[&](uint8_t cmd,int count) {
    if (kMiso < 0) return uint32_t(0);
    digitalWrite(kCs,LOW);digitalWrite(kDc,LOW);send(cmd);digitalWrite(kDc,HIGH);
    // Datasheet figure 10: only the multi-byte ID/status reads have a dummy clock.
    if(cmd==0x04 || cmd==0x09) {
      digitalWrite(kSck,HIGH);delayMicroseconds(2);digitalWrite(kSck,LOW);
    }
    uint32_t result=0;
    for(int i=0;i<count;++i) {
      uint8_t value=0;
      for(int bit=0;bit<8;++bit) {
        digitalWrite(kSck,HIGH);delayMicroseconds(2);
        value=(value<<1)|digitalRead(kMiso);digitalWrite(kSck,LOW);delayMicroseconds(2);
      }
      result|=static_cast<uint32_t>(value)<<(i*8);
    }
    digitalWrite(kCs,HIGH);return result;
  };
  digitalWrite(kReset,LOW);pinMode(kReset,OUTPUT);delay(50);
  digitalWrite(kReset,HIGH);delay(150);
  uint32_t identity=read(0x04,3);
  // Complete ILI9488 setup, independent of the graphics-library SPI driver.
  const uint8_t setup[]={
    0xC0,2,0x17,0x15, 0xC1,1,0x41, 0xC5,3,0,0x12,0x80,
    0xB1,1,0xA0, 0xB4,1,2, 0xB6,3,2,0x22,0x3B,
    0xB7,1,0xC6, 0xF7,4,0xA9,0x51,0x2C,0x82, 0x36,1,0x28
  };
  for(size_t at=0;at<sizeof(setup);) {
    uint8_t cmd=setup[at++],length=setup[at++];command(cmd,setup+at,length);at+=length;
  }
  command(0x11,nullptr,0);delay(150);command(0x38,nullptr,0);command(0x29,nullptr,0);delay(50);
  command(0x13,nullptr,0);command(0x21,nullptr,0);
  uint8_t brightness=255;command(0x51,&brightness,1);
  uint8_t control=0x2C;command(0x53,&control,1);
  uint8_t cabc=0;command(0x55,&cabc,1);
  uint8_t format=0x55;command(0x3A,&format,1);uint32_t read55=read(0x0C,1);
  format=0x66;command(0x3A,&format,1);uint32_t read66=read(0x0C,1);
  hubConsole().printf("DISPLAY_SOFT_TEST identity=%06X wrote55_read=%02X wrote66_read=%02X power=%02X\n",
    identity,read55,read66,read(0x0A,1));
  const uint8_t window[]={0,0,0,159};
  command(0x2A,window,4);command(0x2B,window,4);
  digitalWrite(kCs,LOW);digitalWrite(kDc,LOW);send(0x2C);digitalWrite(kDc,HIGH);
  for(int row=0;row<160;++row) {
    for(int col=0;col<160;++col) {
      uint8_t value=((row/20)^(col/20))&1 ? 255 : 0;
      send(value);send(value);send(value);
    }
    yield();
  }
  digitalWrite(kCs,HIGH);bus_.init();
  hubConsole().println("DISPLAY_SOFT_TEST checkerboard_written");
}
