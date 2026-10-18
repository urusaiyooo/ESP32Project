#pragma once
namespace WifiSettings {
void open();
void close();
void tick();
void gesture(int x,int y,int dx,int dy);
void printStatus();
}
