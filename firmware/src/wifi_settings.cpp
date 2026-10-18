#include "hub_console.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "wifi_settings.h"
#include "app_shell.h"
#include "codex_app.h"
#include <vector>
#include <algorithm>

namespace WifiSettings {
namespace {
constexpr uint16_t bg=0x0841,surface=0x10C4,ink=0xEF7D,muted=0x94B2,accent=0x4B7F;
struct Network {String name;int rssi;wifi_auth_mode_t auth;};
std::vector<Network> networks;
enum class Page {List,Name,Password,Connecting,Result};
Page page=Page::List;
bool active=false,dirty=false,scanning=false,reveal=false;
int first=0,keyboard=0,lastAttempt=0;
String selected,input,message;
wifi_auth_mode_t selectedAuth=WIFI_AUTH_WPA2_PSK;
uint32_t scanAt=0,lastStatus=0;
String previousConnection;
const char* keys[]={"abcdefghijklmnopqrstuvwxyz0123456789","ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"};
void text(const String& value,int x,int y,uint16_t color=ink) {
  auto& d=sharedDisplay;d.setFont(&lgfx::fonts::efontCN_16);d.setTextSize(1);
  d.setTextDatum(lgfx::textdatum_t::top_left);d.setTextWrap(false,false);d.setTextColor(color);d.drawString(value,x,y);
}
String fit(String value,int width) {
  while(value.length() && sharedDisplay.textWidth(value)>width) {
    int end=value.length()-1;while(end>0 && (static_cast<uint8_t>(value[end])&0xC0)==0x80) --end;
    value.remove(end);
  }
  return value;
}
void button(const String& title,int x,int y,int width,uint16_t color=surface,int height=30) {
  sharedDisplay.fillRoundRect(x,y,width,height,5,color);
  text(title,x+max(4,(width-sharedDisplay.textWidth(title))/2),y+6);
}
void startScan() {
  if(scanning) return;
  networks.clear();first=0;message="搜索附近的 2.4 GHz 网络…";page=Page::List;dirty=true;
  // Arduino derives its overall scan timeout from the per-channel dwell.
  // Connected/background scans need a larger budget than a foreground scan.
  WiFi.scanDelete();int result=WiFi.scanNetworks(true,false,false,500);
  scanning=result==WIFI_SCAN_RUNNING;scanAt=millis();
  if(!scanning) message="搜索启动失败，请点击刷新";
}
void connect(bool saved) {
  if(selectedAuth==WIFI_AUTH_WPA2_ENTERPRISE) {message="暂不支持需要用户名的企业网络";dirty=true;return;}
  if(!CodexApp::connectNetwork(selected,input,saved)) {message="密码需 8–63 位；开放网络留空";dirty=true;return;}
  scanning=false;
  input=String();page=Page::Connecting;message="正在连接，请稍候…";lastAttempt=1;dirty=true;
}
void editPassword() {
  input=String();keyboard=0;reveal=false;message=String();page=Page::Password;dirty=true;
}
void render() {
  auto& d=sharedDisplay;d.setClipRect(0,0,480,320);d.fillScreen(bg);
  text("Wi-Fi 设置",16,14,accent);button("返回",408,8,64);
  if(page==Page::List) {
    text(WiFi.status()==WL_CONNECTED ? "已连接："+fit(WiFi.SSID(),230) : "未连接 Wi-Fi",16,49);
    text(scanning?message:networks.empty()?message:"点选网络，输入密码后连接",16,74,muted);
    for(int i=0;i<5 && first+i<static_cast<int>(networks.size());++i) {
      const auto& n=networks[first+i];int y=101+i*32;
      d.fillRoundRect(16,y,448,29,4,surface);
      text(fit(n.name,270),24,y+5);
      text(n.auth==WIFI_AUTH_OPEN?"开放":"加密",306,y+5,muted);
      text(String(n.rssi)+"dBm",370,y+5,muted);
    }
    button("刷新",16,278,76);button("手动输入",100,278,116);
    button("上页",270,278,76);button("下页",354,278,76);
    return;
  }
  text(fit(selected,440),16,49,muted);
  if(page==Page::Connecting || page==Page::Result) {
    text(message,16,113,page==Page::Result?ink:accent);
    if(page==Page::Connecting) text("连接成功后自动保存，下次开机自动连接",16,152,muted);
    if(page==Page::Result && CodexApp::networkAttemptState()==2) text("IP: "+WiFi.localIP().toString(),16,152,muted);
    button(page==Page::Connecting?"返回首页":"返回列表",16,278,140);
    if(page==Page::Result && CodexApp::networkAttemptState()!=2) button("重新输入",304,278,160);
    return;
  }
  String shown;
  if(page==Page::Name || reveal) shown=input;else for(unsigned i=0;i<input.length();++i) shown+='*';
  d.fillRoundRect(16,74,374,30,4,surface);
  while(shown.length() && d.textWidth(shown)>354) shown.remove(0,1);
  text(shown.length()?shown:page==Page::Name?"输入网络名称":"输入密码",24,81,shown.length()?ink:muted);
  button(page==Page::Name?"名称":reveal?"隐藏":"显示",398,74,66);
  button("abc",16,110,142,keyboard==0?0x2945:surface);
  button("ABC",169,110,142,keyboard==1?0x2945:surface);
  button("符号",322,110,142,keyboard==2?0x2945:surface);
  const char* chars=keys[keyboard];
  for(int i=0;chars[i];++i) button(String(chars[i]),16+(i%10)*45,148+(i/10)*28,42,surface,24);
  button("空格",16,278,70);button("删除",94,278,70);
  button(page==Page::Password && CodexApp::hasSavedNetwork(selected)?"已存密码":"清空",172,278,130);
  button(page==Page::Name?"下一步":"连接",310,278,154,0x2945);
  if(message.length()) {d.fillRect(16,260,448,17,bg);text(message,16,260,0xFD65);}
}
}
void open() {
  active=true;networks.clear();selected=String();input=String();previousConnection=String();
  if(CodexApp::networkAttemptState()==1) {page=Page::Connecting;lastAttempt=1;message="正在连接，请稍候…";dirty=true;}
  else if(scanning) {page=Page::List;dirty=true;}
  else startScan();
}
void close() {
  active=false;input=String();selected=String();networks.clear();networks.shrink_to_fit();
  if(!scanning) WiFi.scanDelete();
}
void tick() {
  if(scanning) {
    int count=WiFi.scanComplete();
    if(count>=0) {
      networks.clear();
      if(active) for(int i=0;i<count && networks.size()<40;++i) {
        String name=WiFi.SSID(i);if(!name.length()) continue;
        auto existing=std::find_if(networks.begin(),networks.end(),[&](const Network& n){return n.name==name;});
        if(existing==networks.end()) networks.push_back({name,WiFi.RSSI(i),WiFi.encryptionType(i)});
        else if(WiFi.RSSI(i)>existing->rssi) {existing->rssi=WiFi.RSSI(i);existing->auth=WiFi.encryptionType(i);}
      }
      std::sort(networks.begin(),networks.end(),[](const Network& a,const Network& b){return a.rssi>b.rssi;});
      WiFi.scanDelete();scanning=false;message=networks.empty()?"没有找到网络，请确认热点已开启":"";dirty=true;
    } else if(count==WIFI_SCAN_FAILED || millis()-scanAt>15000) {
      scanning=false;WiFi.scanDelete();message="搜索失败，请点击刷新重试";dirty=true;
    }
  }
  if(!active) return;
  if(page==Page::Connecting) {
    int state=CodexApp::networkAttemptState();
    if(state!=lastAttempt) {
      lastAttempt=state;page=Page::Result;
      message=state==2?"连接成功，已保存 Wi-Fi":state==4?"已连接，但配置保存失败":"连接失败，请检查密码或信号";dirty=true;
    }
  }
  if(page==Page::List && millis()-lastStatus>=1000) {
    lastStatus=millis();String state=String(WiFi.status())+WiFi.SSID();
    if(state!=previousConnection) {previousConnection=state;dirty=true;}
  }
  if(dirty) {render();dirty=false;}
}
void gesture(int x,int y,int dx,int dy) {
  if(!active) return;
  if(abs(dx)<10 && abs(dy)<10 && x>=408 && y<40) {requestAppByName("home");return;}
  if(page==Page::List && abs(dy)>30 && abs(dy)>abs(dx)) {
    first=constrain(first+(dy<0?5:-5),0,max(0,static_cast<int>(networks.size())-5));dirty=true;return;
  }
  if(abs(dx)>=10 || abs(dy)>=10) return;
  if(page==Page::List) {
    if(y>=101 && y<261) {
      int row=(y-101)/32,index=first+row;
      if(index<static_cast<int>(networks.size())) {selected=networks[index].name;selectedAuth=networks[index].auth;editPassword();}
    } else if(y>=278 && y<308) {
      if(x<92) startScan();
      else if(x<216) {page=Page::Name;input=String();selected=String();keyboard=0;message=String();dirty=true;}
      else if(x>=270 && x<346) {first=max(0,first-5);dirty=true;}
      else if(x>=354) {first=min(max(0,static_cast<int>(networks.size())-5),first+5);dirty=true;}
    }
    return;
  }
  if(page==Page::Connecting || page==Page::Result) {
    if(y>=278 && y<308) {
      if(x<156 && page==Page::Connecting) requestAppByName("home");
      else if(x<156 && CodexApp::networkAttemptState()!=1) startScan();
      else if(x>=304 && page==Page::Result) editPassword();
    }
    return;
  }
  if(y>=74 && y<104 && x>=398 && page==Page::Password) reveal=!reveal;
  else if(y>=110 && y<140) keyboard=x<164?0:x<316?1:2;
  else if(y>=148 && y<260 && x>=16 && x<466) {
    int col=(x-16)/45,row=(y-148)/28,index=row*10+col;
    if(col<10 && index<static_cast<int>(strlen(keys[keyboard])) && input.length()<(page==Page::Name?32:63)) input+=keys[keyboard][index];
  } else if(y>=278 && y<308) {
    if(x<86) {if(input.length()<(page==Page::Name?32:63)) input+=' ';}
    else if(x<164) {if(input.length()) input.remove(input.length()-1);}
    else if(x<302) {if(page==Page::Password && CodexApp::hasSavedNetwork(selected)) {connect(true);return;}input=String();}
    else if(page==Page::Name) {
      if(input.length()) {selected=input;selectedAuth=WIFI_AUTH_WPA2_PSK;editPassword();}
    } else {connect(false);return;}
  }
  dirty=true;
}
void printStatus() {
  JsonDocument doc;doc["page"]=static_cast<int>(page);doc["scanning"]=scanning;
  doc["attempt"]=CodexApp::networkAttemptState();doc["input_length"]=input.length();doc["keyboard"]=keyboard;
  doc["first"]=first;doc["selected"]=selected;doc["connected"]=WiFi.status()==WL_CONNECTED;
  doc["message"]=message;doc["scan_code"]=WiFi.scanComplete();
  auto list=doc["networks"].to<JsonArray>();for(const auto& n:networks) list.add(n.name);
  hubConsole().print("WIFI_UI ");serializeJson(doc,hubConsole());hubConsole().println();
}
}
