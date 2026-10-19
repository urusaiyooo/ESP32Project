#include "hub_console.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
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
portMUX_TYPE networkMux=portMUX_INITIALIZER_UNLOCKED;
NetworkStatus radioStatus;
uint8_t failureReason=0;
bool startFailed=false,ipTimedOut=false;
uint32_t connectionStartedAt=0,diagnosedDisconnects=0;
uint32_t handledDisconnects=0,retryAt=0,associatedAt=0;
uint8_t attemptRetries=0;bool dhcpRenewed=false;
NetworkStatus networkStatus(){portENTER_CRITICAL(&networkMux);auto value=radioStatus;portEXIT_CRITICAL(&networkMux);if(networkAttempt==3)value.reason=failureReason;return value;}
void clearNetworkEvents(){portENTER_CRITICAL(&networkMux);radioStatus=NetworkStatus{};portEXIT_CRITICAL(&networkMux);diagnosedDisconnects=0;}
const char* networkAttemptPhase(){
  if(networkAttempt==1)return !networkStarted?"preparing":networkStatus().associated?"dhcp":"authenticating";
  if(networkAttempt==3||networkAttempt==4)return "failed";
  return WiFi.status()==WL_CONNECTED?"online":"idle";
}
String networkAttemptMessage(){
  if(networkAttempt==1)return !networkStarted?"正在重置无线连接…":networkStatus().associated?"认证成功，正在获取 IP 地址…":"正在连接 2.4 GHz 热点…";
  if(networkAttempt==2)return "连接成功，已保存 Wi-Fi";
  if(networkAttempt==4)return "已连接，但配置保存失败";
  if(startFailed)return "无线连接启动失败，请重试";
  if(ipTimedOut)return "已通过认证，但未分配到 IP 地址";
  switch(failureReason){
    case WIFI_REASON_NO_AP_FOUND:return "未找到 2.4 GHz 热点，请检查信号";
    case WIFI_REASON_AUTH_EXPIRE:return "热点认证响应超时，请靠近热点重试";
    case WIFI_REASON_AUTH_FAIL:return "热点认证失败，请核对密码及加密方式";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:return "认证握手超时，请核对密码及加密方式";
    case WIFI_REASON_BEACON_TIMEOUT:return "热点信号中断，请靠近路由器重试";
    case WIFI_REASON_ASSOC_FAIL:return "热点拒绝连接，请检查路由器限制";
    case WIFI_REASON_GROUP_CIPHER_INVALID:case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
    case WIFI_REASON_AKMP_INVALID:case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
    case WIFI_REASON_INVALID_RSN_IE_CAP:case WIFI_REASON_CIPHER_SUITE_REJECTED:
      return "热点加密方式不兼容，请使用 WPA2";
    default:return "连接超时，请检查 2.4 GHz 热点设置";
  }
}
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
  if(useSaved&&!hasSavedNetwork(name))return false;
  String key=useSaved && hasSavedNetwork(name) ? password : pass;
  if(!name.length() || name.length()>32 || key.length()>63 || (key.length() && key.length()<8)) return false;
  if(portalActive) {portal.stop();dns.stop();portalActive=false;}
  stopMusicServer();esp_wifi_scan_stop();WiFi.scanDelete();
  pendingSsid=name;pendingPass=key;networkAttempt=1;networkAttemptAt=millis();networkStarted=false;
  failureReason=0;startFailed=ipTimedOut=false;clearNetworkEvents();
  handledDisconnects=retryAt=associatedAt=0;attemptRetries=0;dhcpRenewed=false;
  // Stop the radio, including outstanding scan/AP work, before a fresh attempt.
  WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);WiFi.mode(WIFI_OFF);
  wifiSince=millis();dirty=true;
  return true;
}
bool retryNetwork(){
  if(networkAttempt==1)return true;
  // Reuse only the device's own last station configuration; never export its key.
  wifi_config_t previous{};
  if(esp_wifi_get_config(WIFI_IF_STA,&previous)==ESP_OK&&previous.sta.ssid[0]){
    char name[33]{},key[65]{};memcpy(name,previous.sta.ssid,32);memcpy(key,previous.sta.password,64);
    bool accepted=connectNetwork(String(name),String(key));
    memset(key,0,sizeof(key));memset(&previous,0,sizeof(previous));return accepted;
  }
  return ssid.length()&&connectNetwork(ssid,String(),true);
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
      if (serialLine=="HUB_WIFI_RETRY") {hubConsole().println(retryNetwork()?"WIFI_RETRY_OK":"WIFI_RETRY_ERROR no_saved_network");serialLine="";continue;}
      if (serialLine.startsWith("HUB_WIFI_TAP ")) {
        int x=0,y=0;if(sscanf(serialLine.c_str(),"HUB_WIFI_TAP %d %d",&x,&y)==2) WifiSettings::gesture(x,y,0,0);
        serialLine="";continue;
      }
      if (serialLine=="HUB_STATUS") {
        hubConsole().printf("HUB_STATUS app=%s ip=%s bridge=%u calibrated=%u heap=%u uptime_ms=%lu version=%s usb_window=%u\n",
                      currentAppName(),WiFi.localIP().toString().c_str(),0,calibrated,ESP.getFreeHeap(),static_cast<unsigned long>(millis()),config::kVersion,hubConsole().usbWindow());
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
  WiFi.onEvent([](arduino_event_id_t event,arduino_event_info_t info){
    portENTER_CRITICAL(&networkMux);
    if(event==ARDUINO_EVENT_WIFI_STA_CONNECTED){radioStatus.associated=true;radioStatus.reason=0;radioStatus.channel=info.wifi_sta_connected.channel;radioStatus.auth=info.wifi_sta_connected.authmode;}
    else if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){radioStatus.associated=false;radioStatus.reason=info.wifi_sta_disconnected.reason;++radioStatus.disconnects;}
    else if(event==ARDUINO_EVENT_WIFI_STA_STOP)radioStatus.associated=false;
    portEXIT_CRITICAL(&networkMux);
  });
  WiFi.persistent(false);WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
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
    if(!networkStarted && now-networkAttemptAt>=350) {
      bool enabled=WiFi.mode(WIFI_STA);WiFi.setSleep(false);clearNetworkEvents();
      networkStarted=true;connectionStartedAt=millis();
      auto result=enabled?WiFi.begin(pendingSsid.c_str(),pendingPass.c_str()):WL_CONNECT_FAILED;
      startFailed=result==WL_CONNECT_FAILED;
      wifiUp=WiFi.status()==WL_CONNECTED;
    }
    if(networkStarted&&!wifiUp&&!startFailed){
      auto state=networkStatus();
      if(state.associated){
        retryAt=0;
        if(!associatedAt)associatedAt=millis();
        // A fresh DHCP exchange can recover a lost initial discovery/offer.
        if(!dhcpRenewed&&millis()-associatedAt>=10000){
          auto netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
          if(netif){
            esp_netif_dhcp_status_t before=ESP_NETIF_DHCP_INIT;
            auto queried=esp_netif_dhcpc_get_status(netif,&before);
            auto stopped=esp_netif_dhcpc_stop(netif);
            auto started=esp_netif_dhcpc_start(netif);
            hubConsole().printf("WIFI_DHCP status=%d query=%d stop=%d start=%d\n",static_cast<int>(before),queried,stopped,started);
          }
          dhcpRenewed=true;
        }
      }else{
        associatedAt=0;dhcpRenewed=false;
        if(state.disconnects!=handledDisconnects){handledDisconnects=state.disconnects;retryAt=millis();}
        // Arduino's one initial retry is global, so later UI attempts need
        // their own bounded retries after transient authentication failures.
        if(retryAt&&millis()-retryAt>=2000&&attemptRetries<3){
          retryAt=0;++attemptRetries;
          auto result=esp_wifi_connect();
          hubConsole().printf("WIFI_RECONNECT retry=%u result=%d\n",attemptRetries,result);
        }
      }
    }
    if(networkStarted && wifiUp && WiFi.SSID()==pendingSsid) {
      prefs.putString("ssid",pendingSsid);prefs.putString("pass",pendingPass);
      if(prefs.getString("ssid")==pendingSsid && prefs.getString("pass")==pendingPass) {
        ssid=pendingSsid;password=pendingPass;networkAttempt=2;
      } else {networkAttempt=4;prefs.putString("ssid",ssid);prefs.putString("pass",password);}
      pendingPass=String();pendingSsid=String();WiFi.setAutoReconnect(true);
    } else if(networkStarted&&((startFailed&&millis()-connectionStartedAt>=2000)||millis()-connectionStartedAt>=45000)) {
      auto status=networkStatus();failureReason=status.reason;ipTimedOut=status.associated;
      pendingPass=String();pendingSsid=String();networkAttempt=3;
      WiFi.disconnect(false,false);WiFi.setAutoReconnect(true);wifiSince=now;
      if(ssid.length()) WiFi.begin(ssid.c_str(),password.c_str());else provision();
      wifiUp=false;
    }
  }
  auto diagnostic=networkStatus();
  if(diagnostic.disconnects!=diagnosedDisconnects){diagnosedDisconnects=diagnostic.disconnects;hubConsole().printf("WIFI_DIAG status=%u reason=%u associated=%u attempt=%d\n",static_cast<unsigned>(WiFi.status()),diagnostic.reason,diagnostic.associated,networkAttempt);}
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
