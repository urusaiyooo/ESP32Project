#pragma once
#include <Arduino.h>

// Keep telemetry usable through either USB-C connector on the S3 board.
class HubConsole : public Stream {
 public:
  void begin(unsigned long baud) {
    const size_t usbRx=Serial.setRxBufferSize(4096);
    Serial.begin(baud);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const size_t uartRx=Serial0.setRxBufferSize(4096);
    Serial0.begin(baud);
    pipelined_=usbRx>=4096&&uartRx>=4096;
#else
    pipelined_=usbRx>=4096;
#endif
  }
  unsigned usbWindow() const {return pipelined_?8:1;}
  int available() override {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return Serial.available()+Serial0.available();
#else
    return Serial.available();
#endif
  }
  int read() override {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return Serial.available() ? Serial.read() : Serial0.read();
#else
    return Serial.read();
#endif
  }
  int peek() override {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return Serial.available() ? Serial.peek() : Serial0.peek();
#else
    return Serial.peek();
#endif
  }
  void flush() override {
    Serial.flush();
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial0.flush();
#endif
  }
  size_t write(uint8_t byte) override {return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count) override {
    Serial.write(bytes,count);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    Serial0.write(bytes,count);
#endif
    return count;
  }
 private:
  bool pipelined_=false;
};
inline HubConsole& hubConsole() { static HubConsole console;return console; }
