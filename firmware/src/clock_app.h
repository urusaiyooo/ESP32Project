#pragma once
#include <Arduino.h>

namespace ClockApp {
struct MusicInfo { bool quotaFresh=false; float remaining=0; bool online=false; bool busy=false; };
MusicInfo musicInfo();
void begin();
void enter();
void leave();
void tick(bool foreground);
bool update(const String& json);
bool updateDashboard(const uint8_t* data,size_t length);
void printStatus();
}
