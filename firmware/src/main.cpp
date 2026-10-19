#include "hub_console.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "app_shell.h"
#include "codex_app.h"
#include "api_server.h"
#include "config.h"
#include "wifi_settings.h"
#include "clock_app.h"
#include "video_app.h"
#include "usb_packets.h"
#include <ArduinoJson.h>
#include "progressive_cover.h"
#include "ui_config.h"

DisplayDriver sharedDisplay;
namespace {
AppId activeApp=AppId::Clock;
TrackState track;
LyricsEngine lyrics;
LyricsMarquee marquee(sharedDisplay);
UiRenderer musicUi(sharedDisplay,marquee);
CoverRenderer cover(sharedDisplay);
ApiServer api(track,musicUi,cover,lyrics);
bool homeDirty=true, mdns=false, touchDown=false, bootDown=false, bootLong=false;
uint32_t touchSamples=0;
uint32_t lastTouch=0, downAt=0, bootAt=0, lastButton=0, lastStatus=0;
int downX=0,downY=0,lastX=0,lastY=0;
uint32_t controlSequence=0,lastControl=0;
String lastHomeStatus;
constexpr uint16_t bg=0xFFFF, card=0xF7BE, text=0x0000, muted=0x8410;

void shellText(const String& value,int x,int y,uint16_t color=text) {
  sharedDisplay.setFont(&lgfx::fonts::efontCN_16);
  sharedDisplay.setTextSize(1);
  sharedDisplay.setTextDatum(lgfx::textdatum_t::top_left);
  sharedDisplay.setTextWrap(false,false);
  // Home is freshly painted; preserve the card color beneath each glyph.
  sharedDisplay.setTextColor(color);
  sharedDisplay.drawString(value,x,y);
}

void drawHome() {
  auto& d=sharedDisplay;
  d.fillScreen(bg);
  shellText("DeskHub",24,18);
  shellText("选择一个应用",24,48,muted);
  d.fillRoundRect(342,40,114,30,7,card);shellText("Wi-Fi 设置",357,47);
  d.fillRoundRect(218,40,114,30,7,card);shellText("桌面时钟",235,47);
  d.fillRoundRect(24,86,204,156,16,card);
  d.fillRoundRect(252,86,204,156,16,card);
  // Native shapes keep icons small and crisp on the SPI display.
  const uint16_t green=0x5E9B, blue=0x4B7F;
  d.drawCircle(126,133,27,green); d.drawCircle(126,133,18,green);
  d.fillCircle(126,133,5,green);
  d.drawRoundRect(320,108,68,47,7,blue);
  d.drawLine(334,123,345,132,blue); d.drawLine(345,132,334,141,blue);
  d.drawFastHLine(355,141,17,blue);
  shellText("音乐屏",102,180,green);
  shellText("桌面时钟",308,180,blue);
  shellText("封面 · 歌词 · 播放进度",44,211,muted);
  shellText("天气 · 积分 · 额度",280,211,muted);
  const bool wifi=WiFi.status()==WL_CONNECTED;
  if (CodexApp::isPortalActive()) {
    shellText("配网热点：" + WiFi.softAPSSID(),24,255,muted);
    shellText("连接后打开 192.168.4.1",24,278,muted);
  } else {
    shellText(wifi ? "Wi-Fi  " + WiFi.localIP().toString() : "Wi-Fi 连接中…",24,255,muted);
    shellText("数据通过 USB 自动同步",24,278,muted);
  }
  d.drawRoundRect(342,275,114,30,7,0x31A6);
  shellText("触摸校准",365,282,muted);
  homeDirty=false;
}

void drawHomeButton() {
  const uint16_t buttonBg=activeApp==AppId::Music?0xef5f:0x2945;
  sharedDisplay.fillRoundRect(408,289,64,activeApp==AppId::Music?20:25,6,buttonBg);
  sharedDisplay.setFont(&lgfx::fonts::efontCN_16);
  sharedDisplay.setTextDatum(lgfx::textdatum_t::top_left);
  sharedDisplay.setTextSize(1);
  sharedDisplay.setTextColor(activeApp==AppId::Music?0x5a93:0xFFFF,buttonBg);
  sharedDisplay.drawString("应用",423,293);
}

void selectApp(AppId next) {
  if (next==activeApp) return;
  if (activeApp==AppId::Clock) ClockApp::leave();
  if(activeApp==AppId::Wifi) WifiSettings::close();
  api.setForeground(false);
  if (activeApp==AppId::Music) {
    track.clear(); lyrics.release(); musicUi.release(); marquee.release();
  }
  CodexApp::setForeground(false);
  activeApp=next;
  sharedDisplay.setClipRect(0,0,480,320);
  sharedDisplay.setTextDatum(lgfx::textdatum_t::top_left);
  sharedDisplay.setTextWrap(false,false);
  if (next==AppId::Clock) ClockApp::enter();
  else if (next==AppId::Music) {
    musicUi.drawIdle("等待电脑音乐同步",WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : String());
    api.setForeground(true);
  } else if (next==AppId::Codex) {
    CodexApp::setForeground(true);
  } else if(next==AppId::Wifi) WifiSettings::open();
  else homeDirty=true;
  hubConsole().printf("HUB_APP %s heap=%u largest=%u\n",currentAppName(),ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void dispatchGesture(int x,int y,int dx,int dy,uint32_t held) {
  const bool tap=abs(dx)<10 && abs(dy)<10;
  if(activeApp==AppId::Wifi) {WifiSettings::gesture(x,y,dx,dy);return;}
  const bool buttonTap=abs(dx)<=28 && abs(dy)<=28;
  if(activeApp==AppId::Music&&buttonTap&&held<900) {
    const char* start=musicUi.controlAt(downX,downY,track.playing);
    const char* end=musicUi.controlAt(x,y,track.playing);
    if(start&&end&&!strcmp(start,end)&&millis()-lastControl>=300) {
      lastControl=millis();uint32_t id=++controlSequence;musicUi.controlPending(id,lastControl);
      hubConsole().printf("USB_EVENT {\"kind\":\"music_control\",\"command\":\"%s\",\"id\":%lu}\n",start,(unsigned long)id);
      return;
    }
  }
  // Invisible target over the central time digits; no extra rendering layer.
  if (activeApp==AppId::Clock) {
    if (VideoApp::playing()) {
      if(buttonTap){VideoApp::stop();ClockApp::enter();}
      return;
    }
    if (buttonTap && downX>=6 && downX<150 && downY>=33 && downY<139 &&
        x>=6 && x<150 && y>=33 && y<139) {
      ClockApp::leave();
      if(!VideoApp::start())ClockApp::enter();
      return;
    }
    if (buttonTap && downX>=120 && downX<360 && downY>=40 && downY<136 &&
        x>=120 && x<360 && y>=40 && y<136) selectApp(AppId::Home);
    return;
  }
  // Other apps retain their existing lower-right Home button.
  if (activeApp!=AppId::Home && buttonTap && downX>=400 && downY>=281 &&
      downX<480 && downY<320 && x>=400 && y>=281 && x<480 && y<320) {
    selectApp(AppId::Home); return;
  }
  if (activeApp==AppId::Home && tap && held<700) {
    if (downY>=86 && downY<242 && y>=86 && y<242) {
      if (downX>=24 && downX<228 && x>=24 && x<228) selectApp(AppId::Music);
      else if (downX>=252 && downX<456 && x>=252 && x<456) selectApp(AppId::Clock);
    } else if (x>=218 && x<332 && y>=40 && y<70) {
      selectApp(AppId::Clock);
    } else if (x>=342 && y>=40 && y<70) {
      selectApp(AppId::Wifi);
    } else if (x>=342 && y>=275 && y<305) {
      CodexApp::calibrate(); homeDirty=true;
    }
  } else if (activeApp==AppId::Codex) CodexApp::handleGesture(x,y,dx,dy,held);
}

void inputTick(uint32_t now) {
  if (now-lastTouch>=12) {
    lastTouch=now;
    int x=0,y=0; const bool pressed=sharedDisplay.getTouch(&x,&y);
    if (pressed) ++touchSamples;
    if (pressed && !touchDown) {
      touchDown=true; downX=lastX=x; downY=lastY=y; downAt=now;
      if(activeApp==AppId::Codex) CodexApp::pointerDown(x,y,now);
    } else if (pressed) {
      lastX=x;lastY=y;
      if(activeApp==AppId::Codex) CodexApp::pointerMove(x,y,now);
    }
    else if (touchDown) {
      touchDown=false;
      bool consumed=activeApp==AppId::Codex && CodexApp::pointerUp(now);
      if(!consumed) dispatchGesture(lastX,lastY,lastX-downX,lastY-downY,now-downAt);
    }
  }
  const bool pressed=digitalRead(0)==LOW;
  if (pressed && !bootDown) {bootDown=true;bootAt=now;bootLong=false;}
  else if (pressed && !bootLong && now-bootAt>=3000) {
    bootLong=true; selectApp(AppId::Wifi);
  } else if (!pressed && bootDown) {
    bootDown=false;
    if (!bootLong && now-bootAt>=40) selectApp(AppId::Home);
  }
}
} // namespace

void printShellTouchStatus() {
  JsonDocument d;
  d["samples"]=touchSamples;d["pressed"]=touchDown;
  d["last_x"]=lastX;d["last_y"]=lastY;
  d["irq"]=digitalRead(display_config::kTouchIrq);
  d["calibrated"]=CodexApp::isCalibrated();
  lgfx::touch_point_t raw{};d["raw_count"]=sharedDisplay.getTouchRaw(&raw);
  d["raw_x"]=raw.x;d["raw_y"]=raw.y;d["pressure"]=raw.size;
  d["touch_spi_host"]=sharedDisplay.touch()->config().spi_host;
  d["touch_irq_used"]=sharedDisplay.touch()->config().pin_int;
  hubConsole().print("TOUCH_STATUS ");serializeJson(d,hubConsole());hubConsole().println();
}
void printMusicStatus(){musicUi.printStatus();}
void injectShellGesture(int x0,int y0,int x1,int y1) {
  downX=x0;downY=y0;dispatchGesture(x1,y1,x1-x0,y1-y0,100);
}

bool receiveUsbPacket(const String& kind,const uint8_t* data,size_t length) {
  if(kind=="dashboard") return ClockApp::updateDashboard(data,length);
  if(activeApp!=AppId::Music) return false;
  if(kind=="cover") {
    uint32_t w=0,h=0;
    bool png=length>24 && !memcmp(data,"\x89PNG\r\n\x1a\n",8);
    if(png) {
      w=(uint32_t(data[16])<<24)|(uint32_t(data[17])<<16)|(uint32_t(data[18])<<8)|data[19];
      h=(uint32_t(data[20])<<24)|(uint32_t(data[21])<<16)|(uint32_t(data[22])<<8)|data[23];
    } else {
      uint16_t jw=0,jh=0;uint8_t marker=0;
      if(!CoverRenderer::readDimensions(data,length,jw,jh,marker)) return false;
      if(marker==0xc2)return drawProgressiveCover(sharedDisplay,data,length,jw,jh);
      if(marker!=0xc0)return false;
      w=jw;h=jh;
    }
    if(!w || !h || w>4096 || h>4096) return false;
    float scale=min(float(ui_config::kCoverWidth)/w,float(ui_config::kCoverHeight)/h);
    sharedDisplay.fillRect(ui_config::kCoverX,ui_config::kCoverY,ui_config::kCoverWidth,ui_config::kCoverHeight,ui_config::kCoverBackground);
    int x=ui_config::kCoverX+(ui_config::kCoverWidth-int(w*scale))/2,y=ui_config::kCoverY+(ui_config::kCoverHeight-int(h*scale))/2;
    return png ? sharedDisplay.drawPng(data,length,x,y,128,128,0,0,scale,scale) :
                 sharedDisplay.drawJpg(data,length,x,y,128,128,0,0,scale,scale);
  }
  JsonDocument d;if(deserializeJson(d,data,length)) return false;
  if(kind=="control_result") {if(!d["id"].is<uint32_t>()||!d["success"].is<bool>())return false;musicUi.controlResult(d["id"],d["success"],millis());return true;}
  if(kind=="idle") {track.clear();lyrics.clear();musicUi.drawIdle("等待电脑播放音乐","USB 已连接");return true;}
  String id=d["track_id"] | "";
  if(kind=="track") {
    if(id.isEmpty() || !d["duration_ms"].is<uint32_t>()) return false;
    track.setTrack(id,sanitizeUtf8(String(d["title"] | ""),512),sanitizeUtf8(String(d["artist"] | ""),256),
      sanitizeUtf8(String(d["album"] | ""),256),d["duration_ms"].as<uint32_t>(),millis());
    lyrics.clear();musicUi.drawTrack(track);return true;
  }
  if(!track.matches(id)) return false;
  if(kind=="playback") {
    if(!d["position_ms"].is<uint32_t>() || !d["duration_ms"].is<uint32_t>() || !d["playing"].is<bool>()) return false;
    track.setPlayback(d["position_ms"].as<uint32_t>(),d["duration_ms"].as<uint32_t>(),d["playing"].as<bool>(),millis());return true;
  }
  if(kind=="lyrics") {
    String lrc=d["lrc"] | "";String plain=d["plain"] | "";
    if(lrc.isEmpty() && plain.length()) {
      int lines=1;for(unsigned i=0;i<plain.length();++i)if(plain[i]=='\n')++lines;
      int line=0,start=0;
      while(start<int(plain.length())) {
        int end=plain.indexOf('\n',start);if(end<0)end=plain.length();
        uint32_t ms=(uint64_t(track.durationMs)*line)/max(1,lines);
        char stamp[24];snprintf(stamp,sizeof(stamp),"[%02lu:%02lu.%03lu]",(unsigned long)(ms/60000),(unsigned long)((ms/1000)%60),(unsigned long)(ms%1000));
        lrc+=stamp;lrc+=plain.substring(start,end);lrc+='\n';start=end+1;++line;
      }
    }
    String error;return lyrics.parse(lrc,error);
  }
  return false;
}

const char* currentAppName() {
  return activeApp==AppId::Clock ? "clock" : activeApp==AppId::Music ? "music" : activeApp==AppId::Codex ? "codex" : activeApp==AppId::Wifi ? "wifi" : "home";
}
void printUsbAppStatus() {
  JsonDocument d;d["app"]=currentAppName();d["rendering"]="S3";
  d["track_active"]=track.active;d["title"]=track.title;d["lyrics_count"]=lyrics.count();
  d["position_ms"]=track.estimatedPosition(millis());d["lyric_index"]=lyrics.indexAt(track.estimatedPosition(millis()));
  d["lyric_text"]=lyrics.textAt(lyrics.indexAt(track.estimatedPosition(millis())));
  d["heap"]=ESP.getFreeHeap();d["psram_free"]=ESP.getFreePsram();
  hubConsole().print("USB_STATUS ");serializeJson(d,hubConsole());hubConsole().println();
}
void stopMusicServer() { api.stop(); }
void requestAppByName(const String& name) {
  if(VideoApp::playing()){VideoApp::stop();if(activeApp==AppId::Clock)ClockApp::enter();}
  if (name=="clock") selectApp(AppId::Clock);
  else if (name=="home") selectApp(AppId::Home);
  else if (name=="music") selectApp(AppId::Music);
  else if (name=="codex") selectApp(AppId::Clock);
  else if (name=="wifi") selectApp(AppId::Wifi);
}
void setup() {
  CodexApp::setup();
  VideoApp::begin();
  // Startup calibration is optional; BOOT and serial still work without it.
  ClockApp::begin();ClockApp::enter();
  hubConsole().println("[INFO] DeskHub ready: default=clock; HUB_APP clock|home|music|codex; short BOOT: Home");
}
void loop() {
  uint32_t now=millis();
  CodexApp::loop();
  inputTick(millis());
  WifiSettings::tick();
  if(VideoApp::playing()) {
    if(VideoApp::tick())ClockApp::enter();
  } else ClockApp::tick(activeApp==AppId::Clock);
  UsbPackets::tick();
  const bool wifi=WiFi.status()==WL_CONNECTED;
  if (wifi && !CodexApp::isPortalActive()) {
    if (!mdns) {mdns=MDNS.begin("musicdisplay"); if (mdns) MDNS.addService("http","tcp",80);}
    if (!api.started()) api.begin();
    api.tick();
  } else {
    api.stop(); if (mdns) {MDNS.end();mdns=false;}
  }
  now=millis(); // HTTP requests can take time; avoid stale timestamp underflow.
  if (activeApp==AppId::Music) {
    musicUi.tick(track,lyrics,now);
    if (track.active && now-track.lastPlaybackMs>=config::kIdleTimeoutMs) {
      track.clear();lyrics.clear();musicUi.drawIdle("等待播放",wifi ? WiFi.localIP().toString() : String());
    }
  }
  if (activeApp==AppId::Home) {
    if (now-lastStatus>=2000) {
      lastStatus=now;
      String status=String(wifi)+WiFi.localIP().toString()+String(CodexApp::isConnected())+
                    String(CodexApp::isPortalActive());
      if (status!=lastHomeStatus) {lastHomeStatus=status;homeDirty=true;}
    }
    if (homeDirty) drawHome();
  } else if (activeApp!=AppId::Wifi && activeApp!=AppId::Clock && now-lastButton>=100) {lastButton=now;drawHomeButton();}
  static uint32_t heapAt=0;
  if (now-heapAt>=10000) {
    heapAt=now;
    hubConsole().printf("[HEAP] app=%s free=%u minimum=%u largest=%u\n",currentAppName(),
                  ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
  delay(1);
}
