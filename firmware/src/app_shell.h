#pragma once
#include "display_driver.h"

enum class AppId { Home, Music, Codex, Wifi, Clock };
extern DisplayDriver sharedDisplay;
void stopMusicServer();
void requestAppByName(const String& name);
const char* currentAppName();

void printShellTouchStatus();
void printMusicStatus();
void injectShellGesture(int x0,int y0,int x1,int y1);
