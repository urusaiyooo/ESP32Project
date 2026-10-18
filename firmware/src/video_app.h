#pragma once
namespace VideoApp {
void begin();
bool start();
void stop();
bool playing();
// Returns true when playback ends or fails, so the clock can repaint itself.
bool tick();
void printStatus();
}
