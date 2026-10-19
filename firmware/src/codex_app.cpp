#include "hub_console.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <mbedtls/base64.h>
#include "display_driver.h"
#include "codex_app.h"
#include "app_shell.h"
#include "wifi_settings.h"
#include "clock_app.h"
#include "video_app.h"
#include "usb_packets.h"
#include "config.h"
namespace CodexApp {
// Hardware/network shell retained; terminal answers, history and WebSocket server removed.
DisplayDriver& lcd=sharedDisplay;
WebServer portal(80);DNSServer dns;Preferences prefs;
struct {uint16_t accent=0x249f,background=0xffff;} theme;
String deviceId,ssid,password,pendingSsid,pendingPass,serialLine;
bool portalActive=false,calibrated=false,networkStarted=false,wifiWasUp=false,dirty=false,wifiRestartPending=false;
int networkAttempt=0;uint32_t networkAttemptAt=0,wifiSince=0,wifiRestartAt=0;
void provision() {
  if (portalActive) return;
  stopMusicServer();
  String ap="DeskHub-"+deviceId.substring(deviceId.length()-4);
  WiFi.mode(WIFI_AP_STA); WiFi.softAP(ap.c_str()); dns.start(53,"*",WiFi.softAPIP());
  portal.on("/",HTTP_GET,[](){
    portal.send(200,"text/html","<html><meta name='viewport' content='width=device-width'><h2>DeskHub Wi-Fi</h2><form method='post' action='/save'>SSID <input name='ssid'><br>Password <input name='password' type='password'><br><button>Save</button></form></html>");
  });
  portal.on("/save",HTTP_POST,[](){
    String s=portal.arg("ssid"), p=portal.arg("password");
    if (!s.length() || s.length()>32 || p.length()>63) { portal.send(400,"text/plain","Invalid Wi-Fi details"); return; }
    prefs.putString("ssid",s); prefs.putString("pass",p);
    portal.send(200,"text/plain","Saved. Device will restart.");
    ESP.restart();
  });
  portal.onNotFound([](){ portal.sendHeader("Location","http://192.168.4.1/"); portal.send(302,"text/plain",""); });
  portal.begin(); portalActive=true; dirty=true;
}

bool hasSavedNetwork(const String& name) { return name.length() && name==ssid; }
int networkAttemptState() { return networkAttempt; }
bool connectNetwork(const String& name,const String& pass,bool useSaved) {
  String key=useSaved && hasSavedNetwork(name) ? password : pass;
  if(!name.length() || name.length()>32 || key.length()>63 || (key.length() && key.length()<8)) return false;
  if(portalActive) {portal.stop();dns.stop();WiFi.softAPdisconnect(true);portalActive=false;}
  stopMusicServer();esp_wifi_scan_stop();WiFi.scanDelete();
  pendingSsid=name;pendingPass=key;networkAttempt=1;networkAttemptAt=millis();networkStarted=false;
  WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);WiFi.mode(WIFI_STA);
  wifiSince=millis();dirty=true;
  return true;
}

void serialTick(uint32_t now) {
  if (wifiRestartPending && static_cast<int32_t>(now-wifiRestartAt)>=0) ESP.restart();
  while (hubConsole().available()) {
    char ch=static_cast<char>(hubConsole().read());
    if (ch=='\r') continue;
    if (ch=='\n') {
      if(serialLine.startsWith("HUB_TOUCH_MODE ")) {
        const bool soft=serialLine.substring(15)=="soft";
        bool ok=lcd.setTouchPolling(soft);
        if(ok)prefs.putBool("touchsoft",soft);
        hubConsole().println(ok?"TOUCH_MODE_OK":"TOUCH_MODE_ERROR");serialLine="";continue;
      }
      if(serialLine=="HUB_TOUCH_CALIBRATE") {
        serialLine="";requestAppByName("home");
        hubConsole().println("TOUCH_CALIBRATING");
        calibrate();requestAppByName("clock");
        hubConsole().println(isCalibrated()?"TOUCH_CALIBRATED":"TOUCH_ERROR save_failed");continue;
      }
      if(serialLine=="HUB_TOUCH_STATUS") {printShellTouchStatus();serialLine="";continue;}
      if(serialLine=="HUB_MUSIC_STATUS") {printMusicStatus();serialLine="";continue;}
      if(serialLine=="HUB_VIDEO_STATUS") {VideoApp::printStatus();serialLine="";continue;}
      if(serialLine.startsWith("HUB_SHELL_GESTURE ")) {
        int x0,y0,x1,y1;
        if(sscanf(serialLine.c_str(),"HUB_SHELL_GESTURE %d %d %d %d",&x0,&y0,&x1,&y1)==4)injectShellGesture(x0,y0,x1,y1);
        serialLine="";continue;
      }
      if(serialLine=="HUB_USB_STATUS") {printUsbAppStatus();serialLine="";continue;}
      if (UsbPackets::line(serialLine)) {serialLine="";continue;}
      if (serialLine=="HUB_CLOCK_STATUS") {ClockApp::printStatus();serialLine="";continue;}
      if (serialLine.startsWith("HUB_CLOCK ")) {
        hubConsole().println(ClockApp::update(serialLine.substring(10)) ? "CLOCK_OK" : "CLOCK_ERROR invalid_payload");
        serialLine="";continue;
      }
      if (serialLine=="HUB_DISPLAY_SOFT_TEST") {
        requestAppByName("music");sharedDisplay.softwareDisplayTest();serialLine="";continue;
      }
      if (serialLine=="HUB_DISPLAY_STATUS" || serialLine=="HUB_DISPLAY_TEST") {
        if(serialLine=="HUB_DISPLAY_TEST") {
          requestAppByName("music");
          sharedDisplay.getPanel()->setSleep(false);delay(150);
          sharedDisplay.getPanel()->setPowerSave(false);sharedDisplay.writeCommand(0x29);delay(50);
          const uint16_t colors[]={0xF800,0x07E0,0x001F,0xFFFF};
          for(int i=0;i<4;++i) sharedDisplay.fillRect(i*120,0,120,288,colors[i]);
          sharedDisplay.setTextColor(static_cast<uint16_t>(0));
          sharedDisplay.drawString("DISPLAY TEST",24,120);
        }
        sharedDisplay.printDiagnostics();serialLine="";continue;
      }
      if (serialLine=="HUB_WIFI_STATUS") {WifiSettings::printStatus();serialLine="";continue;}
      if (serialLine.startsWith("HUB_WIFI_TAP ")) {
        int x=0,y=0;if(sscanf(serialLine.c_str(),"HUB_WIFI_TAP %d %d",&x,&y)==2) WifiSettings::gesture(x,y,0,0);
        serialLine="";continue;
      }
      if (serialLine=="HUB_STATUS") {
        hubConsole().printf("HUB_STATUS app=%s ip=%s bridge=%u calibrated=%u heap=%u uptime_ms=%lu version=%s\n",
                      currentAppName(),WiFi.localIP().toString().c_str(),0,calibrated,ESP.getFreeHeap(),static_cast<unsigned long>(millis()),config::kVersion);
        serialLine=""; continue;
      }
      if (serialLine.startsWith("HUB_APP ")) {
        requestAppByName(serialLine.substring(8)); serialLine=""; continue;
      }
      JsonDocument d;
      if (serialLine == "WIFI_SETUP") {
        enterSetup(); hubConsole().println("WIFI_OK SETUP"); serialLine=""; continue;
      }
      bool valid=false;
      if (serialLine.startsWith("WIFI_SET ")) {
        const int split=serialLine.indexOf(' ',9);
        if (split>9) {
          auto decode=[](const String& encoded,String& out) {
            uint8_t bytes[97]; size_t count=0;
            if (mbedtls_base64_decode(bytes,96,&count,
                reinterpret_cast<const uint8_t*>(encoded.c_str()),encoded.length())!=0) return false;
            bytes[count]=0;
            if (strlen(reinterpret_cast<const char*>(bytes))!=count) return false;
            out=String(reinterpret_cast<const char*>(bytes)); return true;
          };
          String s,p;
          valid=decode(serialLine.substring(9,split),s) && decode(serialLine.substring(split+1),p);
          if (valid) { d["ssid"]=s; d["password"]=p; }
        }
      } else {
        valid=!deserializeJson(d,serialLine) && String(d["type"] | "")=="wifi_config";
      }
      if (!valid) {
        hubConsole().println("WIFI_ERROR invalid_command");
      } else {
        String nextSsid=d["ssid"] | "", nextPass=d["password"] | "";
        if (!nextSsid.length() || nextSsid.length()>32 || nextPass.length()>63) {
          hubConsole().println("WIFI_ERROR invalid_credentials");
        } else if (!prefs.putString("ssid",nextSsid) || !prefs.putString("pass",nextPass)) {
          hubConsole().println("WIFI_ERROR save_failed");
        } else {
          hubConsole().println("WIFI_OK SAVED");
          wifiRestartPending=true; wifiRestartAt=now+900;
        }
      }
      serialLine="";
    } else if (serialLine.length()<512) {
      serialLine+=ch;
    } else {
      serialLine="";
      hubConsole().println("WIFI_ERROR line_too_long");
    }
  }
}
void setup() {
  hubConsole().begin(config::kSerialBaud);
  prefs.begin("codexterm",false);
  uint64_t mac=ESP.getEfuseMac(); char id[11]; snprintf(id,sizeof(id),"CDT-%06X",static_cast<unsigned>(mac&0xFFFFFF)); deviceId=id;
  pinMode(display_config::kTouchCs,OUTPUT); digitalWrite(display_config::kTouchCs,HIGH);
  if (!lcd.beginDisplay()) hubConsole().println("[ERROR] LCD init failed");
  if(prefs.getBool("touchsoft",false))lcd.setTouchPolling(true);
  pinMode(display_config::kTouchIrq, INPUT_PULLUP);
  if (prefs.getBytesLength("touch")==16) { uint16_t cal[8]; prefs.getBytes("touch",cal,sizeof(cal)); lcd.setTouchCalibrate(cal); calibrated=true; }
  // Hold BOOT during startup to calibrate, even before touch coordinates work.
  pinMode(0,INPUT_PULLUP);
  if (digitalRead(0)==LOW) { uint16_t cal[8]; lcd.calibrateTouch(cal,theme.accent,theme.background,12); prefs.putBytes("touch",cal,sizeof(cal)); calibrated=true; }
  ssid=prefs.getString("ssid",""); password=prefs.getString("pass","");
  if (!ssid.length()) {
    Preferences old; old.begin("musicdisplay",true);
    JsonDocument saved;
    if (!deserializeJson(saved,old.getString("wifi",""))) {
      ssid=String(saved["ssid"] | ""); password=String(saved["password"] | "");
      if (ssid.length() && ssid.length()<=32 && password.length()<=63) {
        prefs.putString("ssid",ssid); prefs.putString("pass",password);
      } else { ssid=""; password=""; }
    }
    old.end();
  }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  if (!WiFi.setSleep(false)) hubConsole().println("[WARN] Could not disable Wi-Fi power save");
  if (ssid.length()) { WiFi.begin(ssid.c_str(),password.c_str()); wifiSince=millis(); } else provision();
  dirty=true;
  hubConsole().printf("[INFO] %s free_heap=%u psram=%u\n",deviceId.c_str(),ESP.getFreeHeap(),ESP.getPsramSize());
}
void loop() {
  uint32_t now=millis();
  serialTick(now);
  if (portalActive) { portal.handleClient(); dns.processNextRequest(); }
  // The shell owns touch input for every app.
  // hubConsole() commands can start a connection after this loop's initial time.
  // Refresh before unsigned elapsed-time checks to avoid instant timeouts.
  now=millis();
  bool wifiUp=WiFi.status()==WL_CONNECTED;
  if(networkAttempt==1) {
    // Disconnect is asynchronous. Starting immediately can make begin() think
    // a same-SSID connection is still active and skip the actual connection.
    if(!networkStarted && !wifiUp && now-networkAttemptAt>=150) {
      WiFi.begin(pendingSsid.c_str(),pendingPass.c_str());networkStarted=true;
      wifiUp=WiFi.status()==WL_CONNECTED;
    }
    if(networkStarted && wifiUp && WiFi.SSID()==pendingSsid) {
      prefs.putString("ssid",pendingSsid);prefs.putString("pass",pendingPass);
      if(prefs.getString("ssid")==pendingSsid && prefs.getString("pass")==pendingPass) {
        ssid=pendingSsid;password=pendingPass;networkAttempt=2;
      } else {networkAttempt=4;prefs.putString("ssid",ssid);prefs.putString("pass",password);}
      pendingPass=String();pendingSsid=String();WiFi.setAutoReconnect(true);
    } else if(now-networkAttemptAt>=20000) {
      pendingPass=String();pendingSsid=String();networkAttempt=3;
      WiFi.disconnect(false,false);WiFi.setAutoReconnect(true);wifiSince=now;
      if(ssid.length()) WiFi.begin(ssid.c_str(),password.c_str());else provision();
      wifiUp=false;
    }
  }
  if (wifiUp != wifiWasUp) {
    wifiWasUp=wifiUp; dirty=true;
    if (wifiUp) hubConsole().printf("[INFO] IP %s\n",WiFi.localIP().toString().c_str());
    else wifiSince=now;
  }
  if (wifiUp && portalActive) {
    portal.stop(); dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
    portalActive=false; dirty=true;
  }
  if (!wifiUp && networkAttempt!=1 && ssid.length() && now-wifiSince>30000 && !portalActive) provision();
}

void setForeground(bool){}
void handleGesture(int,int,int,int,uint32_t){}
void pointerDown(int,int,uint32_t){}
void pointerMove(int,int,uint32_t){}
bool pointerUp(uint32_t){return false;}
void printMotionStats(){}
bool isPortalActive(){return portalActive;}
bool isConnected(){return false;}
bool isCalibrated(){return calibrated;}
void enterSetup(){WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);provision();}
void calibrate(){uint16_t cal[8];lcd.calibrateTouch(cal,theme.accent,theme.background,12);calibrated=prefs.putBytes("touch",cal,sizeof(cal))==sizeof(cal);}
}
