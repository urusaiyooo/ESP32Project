#pragma once
#include <Arduino.h>
namespace CodexApp {
void setup();
void loop();
void setForeground(bool active);
void handleGesture(int x, int y, int dx, int dy, uint32_t held);
bool isPortalActive();
bool isConnected();
bool isCalibrated();
void calibrate();
void provision();
void enterSetup();
void pointerDown(int x,int y,uint32_t now);
void pointerMove(int x,int y,uint32_t now);
bool pointerUp(uint32_t now);
void printMotionStats();
bool hasSavedNetwork(const String& name);
bool connectNetwork(const String& name,const String& pass,bool useSaved=false);
int networkAttemptState();
struct NetworkStatus {bool associated=false;uint8_t reason=0,channel=0,auth=0;uint32_t disconnects=0;};
NetworkStatus networkStatus();
const char* networkAttemptPhase();
String networkAttemptMessage();
bool retryNetwork();
}
