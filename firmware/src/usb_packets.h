#pragma once
#include <Arduino.h>
namespace UsbPackets {
bool receiving();
bool line(const String& input);
void tick();
}
bool receiveUsbPacket(const String& kind,const uint8_t* data,size_t length);
void printUsbAppStatus();
