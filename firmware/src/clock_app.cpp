#include "hub_console.h"
#include "clock_app.h"
#include "app_shell.h"
#include "font_fallback.h"
#include "clock_art.h"
#include "video_app.h"
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <WiFi.h>
namespace ClockApp {
namespace {
constexpr uint16_t bg=0xffff,ink=0,line=0x9cd3,muted=0x8410,blue=0x249f,green=0x05c9;
struct Window {bool present=false;float remaining=0;uint32_t reset=0;} five,week;
bool available=false,dirty=true,timeBuffer=false,weatherOk=false,wbOk=false;
uint32_t quotaAt=0,revision=0,dashboardAt=0,weatherAt=0,aqiAt=0,wbAt=0,lastMarquee=0,tomorrowDate=0;
float temperature=0,humidity=0,wbRemaining=0,wbTotal=0;
int today=-1,tomorrow=-1,aqi=-1,drawnDay=-1,scroll=0;
time_t drawnSecond=-1;
String song,activity="idle",drawnActivity,dataSignature;
int drawnHealth=-1;
lgfx::LGFX_Sprite digits(&sharedDisplay),ticker(&sharedDisplay);
bool validTime(time_t t){return t>=1704067200LL&&t<4102444800LL;}
void text(const String& v,int x,int y,uint16_t color=ink){sharedDisplay.setTextDatum(lgfx::textdatum_t::top_left);sharedDisplay.setTextWrap(false,false);font_fallback::draw(sharedDisplay,v.c_str(),x,y,true,false,color,bg);}
void tiny(const String& v,int x,int y,uint16_t color=ink){auto& d=sharedDisplay;d.setFont(&lgfx::fonts::Font0);d.setTextSize(1);d.setTextDatum(lgfx::textdatum_t::top_left);d.setTextColor(color,bg);d.drawString(v,x,y);}
void centeredText(const String& value,int centerX,int y,uint16_t color=ink) {
  const int width=font_fallback::measure(sharedDisplay,value.c_str(),true,false);
  text(value,centerX-width/2,y,color);
}
void centeredTiny(const String& value,int centerX,int y,uint16_t color=ink) {
  auto& d=sharedDisplay;d.setFont(&lgfx::fonts::Font0);d.setTextSize(1);
  tiny(value,centerX-d.textWidth(value)/2,y,color);
}
bool current(const Window& w,time_t t){return available&&w.present&&quotaAt&&t>=quotaAt&&t-quotaAt<=180&&(!w.reset||t<w.reset);}
int freshness(time_t t){return !five.present&&!week.present?0:current(five,t)&&current(week,t)?2:1;}
bool weatherFresh(time_t t){return weatherOk&&weatherAt&&t>=weatherAt&&t-weatherAt<1800;}
bool wbFresh(time_t t){return wbOk&&wbAt&&t>=wbAt&&t-wbAt<120;}
bool dashboardFresh(){return dashboardAt&&millis()-dashboardAt<10000;}
void border(){String state=dashboardFresh()?activity:"offline";if(state==drawnActivity)return;drawnActivity=state;uint16_t c=state=="running"?blue:state=="complete"?green:line;for(int i=0;i<6;i++)sharedDisplay.drawRect(i,i,480-2*i,320-2*i,c);}
// Every weather glyph fits within 28 x 28 pixels, including rain/lightning.
void icon(int x,int y,int code) {
  auto& d=sharedDisplay;
  if(code<0){centeredText("?",x,y-8,muted);return;}
  if(code<=2) {
    d.fillCircle(x,y-2,5,0xfd20);
    for(int i=0;i<8;i++) {float a=i*.785398f;d.drawLine(x+cosf(a)*8,y-2+sinf(a)*8,x+cosf(a)*10,y-2+sinf(a)*10,0xfd20);}
  }
  if(code>0) {
    d.fillCircle(x-6,y,5,0x9d16);d.fillCircle(x,y-3,6,0x9d16);
    d.fillCircle(x+6,y,5,0x9d16);d.fillRect(x-9,y,18,5,0x9d16);
  }
  if(code==45||code==48) {d.drawFastHLine(x-11,y+8,23,muted);d.drawFastHLine(x-8,y+12,17,muted);}
  else if((code>=71&&code<=77)||code==85||code==86) {
    for(int i=-7;i<=7;i+=7) {d.drawLine(x+i-2,y+8,x+i+2,y+12,blue);d.drawLine(x+i+2,y+8,x+i-2,y+12,blue);}
  } else if(code>=95) {
    d.fillTriangle(x,y+5,x-5,y+10,x+2,y+9,0xfd20);
    d.fillTriangle(x+2,y+8,x-2,y+14,x+7,y+8,0xfd20);
  } else if(code>=51) {
    for(int i=-7;i<=7;i+=7)d.drawLine(x+i,y+8,x+i-2,y+12,blue);
  }
}
void quotaCard(int x,const char* title,const Window& w,time_t epoch) {
  auto& d=sharedDisplay;
  d.fillRoundRect(x,221,148,85,9,bg);
  d.drawRoundRect(x,221,148,85,9,line);
  const int center=x+74;
  centeredText(title,center,229);
  const bool fresh=current(w,epoch);
  const uint16_t c=fresh?ink:muted;
  const String percent=w.present?String(w.remaining,0)+"%":"--";
  const String label=fresh?"剩余":"待更新";
  const int labelWidth=font_fallback::measure(d,label.c_str(),true,false);
  d.setFont(&lgfx::fonts::Font4);d.setTextSize(1);
  const int percentWidth=d.textWidth(percent);
  const int start=center-(percentWidth+8+labelWidth)/2;
  d.setTextDatum(lgfx::textdatum_t::top_left);d.setTextColor(c,bg);
  d.drawString(percent,start,250);
  text(label,start+percentWidth+8,256,c);
  char date[20]="--",at[20]="--";
  if(w.reset&&validTime(w.reset)) {
    tm local{};time_t t=w.reset;localtime_r(&t,&local);
    strftime(date,sizeof(date),"%Y-%m-%d",&local);
    strftime(at,sizeof(at),"%H:%M:%S",&local);
  }
  d.setFont(&lgfx::fonts::Font0);d.setTextSize(1);
  const int dateWidth=d.textWidth(date);
  const int resetWidth=font_fallback::measure(d,"重置",true,false);
  constexpr int resetGap=14;
  const int resetStart=center-(resetWidth+resetGap+dateWidth)/2;
  const int dateStart=resetStart+resetWidth+resetGap;
  text("重置",resetStart,281,c);
  tiny(date,dateStart,279,c);
  // Date and clock share the right column; the label cannot overlap either row.
  centeredTiny(at,dateStart+dateWidth/2,293,c);
}
void drawData(time_t epoch){auto& d=sharedDisplay;bool wf=weatherFresh(epoch),bf=wbFresh(epoch);
 d.fillRoundRect(12,153,456,60,9,bg);d.drawRoundRect(12,153,456,60,9,line);
 // Four equal 114-pixel columns; each label/value group has its own center.
 const int centers[]={69,183,297,411};
 text("今日",centers[0]-34,161,wf?ink:muted);icon(centers[0]+20,169,wf?today:-1);
 text("明日",centers[1]-34,161,wf?ink:muted);icon(centers[1]+20,169,wf&&tomorrowDate>epoch&&tomorrowDate-epoch<=86400?tomorrow:-1);
 centeredText("温度:"+(wf?String(temperature,0)+"°C":"--"),centers[2],161,wf?ink:muted);
 centeredText("湿度:"+(wf?String(humidity,0)+"%":"--"),centers[3],161,wf?ink:muted);
 bool af=wf&&aqi>=0&&aqiAt&&epoch>=aqiAt&&epoch-aqiAt<10800;
 String quality=aqi<=50?"优":aqi<=100?"良":aqi<=150?"敏感":aqi<=200?"不健康":aqi<=300?"很差":"危险";
 // Two equal halves, one shared baseline for location and air quality.
 centeredText("北京大兴",126,188,muted);
 centeredText(af?"空气:"+quality+" "+String(aqi)+" US":"空气:待更新",354,188,af?ink:muted);
 quotaCard(12,"Codex 5小时",five,epoch);quotaCard(166,"Codex 每周",week,epoch);
 d.fillRoundRect(320,221,148,85,9,bg);d.drawRoundRect(320,221,148,85,9,line);centeredText("WorkBuddy",394,229);centeredText("账号池积分",394,251,bf?ink:muted);
 d.setFont(&lgfx::fonts::Font4);d.setTextSize(.8);d.setTextColor(bf?ink:muted,bg);d.setTextDatum(lgfx::textdatum_t::top_center);d.drawString(wbAt?String(wbRemaining,0)+"/"+String(wbTotal,0):"--/--",394,274);
}
void drawDigits(time_t epoch){tm local{};char value[12]="--:--:--";if(validTime(epoch)){localtime_r(&epoch,&local);strftime(value,sizeof(value),"%H:%M:%S",&local);}
 if(timeBuffer){digits.fillSprite(bg);digits.drawJpg(clockArt,sizeof(clockArt),0,0);digits.setFont(&lgfx::fonts::Font7);digits.setTextSize(1.2);digits.setTextColor(ink);digits.setTextDatum(lgfx::textdatum_t::top_center);digits.drawString(value,237,20);digits.pushSprite(3,33);}
 const int day=validTime(epoch)?local.tm_year*400+local.tm_yday:-2;if(day!=drawnDay){sharedDisplay.fillRect(8,8,228,20,bg);const char* days[]={"日","一","二","三","四","五","六"};char date[40];snprintf(date,sizeof(date),"%04d-%02d-%02d 星期%s",local.tm_year+1900,local.tm_mon+1,local.tm_mday,days[local.tm_wday]);text(validTime(epoch)?String(date):"等待校时",10,9);drawnDay=day;}drawnSecond=epoch;}
void drawTicker(){if(!ticker.width())return;ticker.fillSprite(bg);String title=song.length()?song:"无";int width=font_fallback::measure(ticker,title.c_str(),true,false);int x=width>142?-scroll:0;font_fallback::draw(ticker,title.c_str(),x,0,true,false,ink,bg);if(width>142){font_fallback::draw(ticker,title.c_str(),x+width+40,0,true,false,ink,bg);scroll=(scroll+2)%(width+40);}ticker.pushSprite(327,9);}
bool parseWindow(JsonVariantConst source,Window& out){if(source.isNull()){out=Window{};return true;}if(!source.is<JsonObjectConst>()||!source["remaining"].is<float>())return false;float v=source["remaining"].as<float>();if(!isfinite(v)||v<0||v>100)return false;uint32_t reset=source["reset"]|0U;if(reset&&!validTime(reset))return false;out.present=true;out.remaining=v;out.reset=reset;return true;}
}
void begin(){configTzTime("CST-8","ntp.aliyun.com","time.cloudflare.com","pool.ntp.org");}
MusicInfo musicInfo(){MusicInfo info;info.quotaFresh=current(five,time(nullptr));info.remaining=five.remaining;info.online=dashboardFresh();info.busy=info.online&&activity=="running";return info;}
void enter(){auto& d=sharedDisplay;d.fillScreen(bg);text("正在播放:",241,9);d.setTextDatum(lgfx::textdatum_t::top_left);digits.setColorDepth(16);digits.setPsram(true);timeBuffer=digits.createSprite(474,106)!=nullptr;ticker.setColorDepth(16);ticker.setPsram(true);ticker.createSprite(142,18);drawnSecond=-1;drawnDay=-1;drawnActivity="";dirty=true;tick(true);}
void leave(){VideoApp::stop();digits.deleteSprite();ticker.deleteSprite();timeBuffer=false;}
void tick(bool foreground){if(!foreground)return;sharedDisplay.setClipRect(6,6,468,308);time_t t=time(nullptr);if(t!=drawnSecond)drawDigits(t);int health=freshness(t)+(weatherFresh(t)?4:0)+(wbFresh(t)?8:0)+(dashboardFresh()?16:0);if(dirty||health!=drawnHealth){drawData(t);dirty=false;drawnHealth=health;}if(millis()-lastMarquee>=60){lastMarquee=millis();drawTicker();}sharedDisplay.clearClipRect();border();}
bool update(const String& json){if(json.length()>480)return false;JsonDocument doc;if(deserializeJson(doc,json)||!doc["epoch"].is<uint32_t>()||!doc["available"].is<bool>()||!doc["quota_at"].is<uint32_t>())return false;uint32_t epoch=doc["epoch"],at=doc["quota_at"];if(!validTime(epoch)||at>epoch+5||(at&&!validTime(at)))return false;Window f,w;if(!parseWindow(doc["five"],f)||!parseWindow(doc["week"],w))return false;if(doc["available"].as<bool>()&&(!at||(!f.present&&!w.present)))return false;timeval now{static_cast<time_t>(epoch),0};settimeofday(&now,nullptr);five=f;week=w;quotaAt=at;available=doc["available"];dirty=true;++revision;return true;}
bool updateDashboard(const uint8_t* data,size_t length){JsonDocument d;if(deserializeJson(d,data,length))return false;String state=d["activity"]["state"]|"idle";if(state!="running"&&state!="complete"&&state!="idle")return false;String newSong=d["music"]["title"]|"";if(newSong!=song){song=newSong.substring(0,512);scroll=0;}activity=state;dashboardAt=millis();JsonVariantConst w=d["weather"];JsonVariantConst b=d["workbuddy"];String signature;serializeJson(w,signature);serializeJson(b,signature);if(signature!=dataSignature){dirty=true;dataSignature=signature;}weatherOk=w["available"]|false;weatherAt=w["at"]|0U;today=w["today"]|-1;tomorrow=w["tomorrow"]|-1;tomorrowDate=w["tomorrow_date"]|0U;temperature=w["temperature"]|0.f;humidity=w["humidity"]|0.f;aqi=w["aqi"].isNull()?-1:w["aqi"].as<int>();aqiAt=w["aqi_at"]|0U;wbOk=b["available"]|false;wbAt=b["at"]|0U;wbRemaining=b["remaining"]|0.f;wbTotal=b["total"]|0.f;return true;}
void printStatus(){JsonDocument d;time_t t=time(nullptr);d["epoch"]=uint32_t(t);d["time_valid"]=validTime(t);d["timezone"]="Asia/Shanghai";d["device_mac"]=WiFi.macAddress();d["quota_at"]=quotaAt;d["freshness"]=freshness(t);d["revision"]=revision;d["buffered"]=timeBuffer;d["activity"]=dashboardFresh()?activity:"offline";d["song"]=song;d["weather_fresh"]=weatherFresh(t);d["workbuddy_fresh"]=wbFresh(t);d["wb_remaining"]=wbRemaining;d["wb_total"]=wbTotal;d["temperature"]=temperature;d["humidity"]=humidity;d["aqi"]=aqi;for(int i=0;i<2;i++){auto& w=i?week:five;JsonObject o=d[i?"week":"five"].to<JsonObject>();o["present"]=w.present;o["remaining"]=w.remaining;o["reset"]=w.reset;o["current"]=current(w,t);}hubConsole().print("CLOCK_STATUS ");serializeJson(d,hubConsole());hubConsole().println();}
}
