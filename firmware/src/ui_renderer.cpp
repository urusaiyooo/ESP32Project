#include "ui_renderer.h"
#include "font_fallback.h"
#include "ui_config.h"
#include "clock_app.h"
#include "hub_console.h"
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
namespace {
constexpr uint16_t white=0xffff,ink=0x18c5,muted=0x8410,edge=0xd69a;
constexpr uint16_t blue=0x43d8,orange=0xfbe0,lyricBg=0xf7bf,purple=0x6afb,pale=0xef5f;
constexpr int lx=162,ly=100,lw=296,lh=204,pitch=42;
template<class G> void txt(G& d,const String& s,int x,int y,uint16_t fg=ink,uint16_t bg=white,bool big=false){
 d.setTextDatum(lgfx::textdatum_t::top_left);d.setTextWrap(false,false);
 font_fallback::draw(d,s.c_str(),x,y,!big,false,fg,bg);
}
template<class G> void center(G& d,const String& s,int x,int y,uint16_t fg=ink,uint16_t bg=white){
 int w=font_fallback::measure(d,s.c_str(),true,false);txt(d,s,x-w/2,y,fg,bg);
}
template<class G> void quotaHeader(G& canvas,int offset,const String& label,int width,bool fresh){
 const int y=offset?5:0;
 txt(canvas,"五小时额度",246-offset,8-y);
 canvas.fillRoundRect(332-offset,12-y,88,8,3,0xe71c);
 if(width)canvas.fillRoundRect(332-offset,12-y,width,8,3,blue);
 txt(canvas,label+"   ",430-offset,8-y,fresh?ink:muted);
}
uint32_t bounce(uint32_t age,int overflow){
 if(overflow<=0||age<900)return 0;
 uint32_t travel=uint32_t(overflow)*1000/26,period=2*travel+1800,t=(age-900)%period;
 if(t<travel)return t*26/1000;if(t<travel+900)return overflow;
 if(t<2*travel+900)return overflow-(t-travel-900)*26/1000;return 0;
}
}
void UiRenderer::layout(){
 release();display_.clearClipRect();display_.fillScreen(white);
 display_.drawRoundRect(150,34,320,278,14,edge);
 display_.fillRoundRect(158,96,304,212,12,lyricBg);
 display_.drawRoundRect(10,254,128,58,10,edge);center(display_,"ChatGPT",74,261);
 display_.fillRoundRect(10,36,128,128,8,ui_config::kCoverBackground);
 display_.drawCircle(74,91,31,blue);display_.drawCircle(74,91,21,white);display_.fillCircle(74,91,4,blue);
 viewport_.setPsram(true);viewport_.setColorDepth(16);ready_=viewport_.createSprite(lw,lh)!=nullptr;
 headerBuffer_.setPsram(true);headerBuffer_.setColorDepth(16);headerBuffer_.createSprite(468,24);
 second_=-1;activityKey_="";lastProgress_=UINT32_MAX;playing_=true;controls(false);header();activity();
}
void UiRenderer::metadata(const String& value,lgfx::LGFX_Sprite& sprite,bool big){
 sprite.deleteSprite();sprite.setPsram(true);sprite.setColorDepth(16);
 int width=max(lw,font_fallback::measure(display_,value.c_str(),!big,false));width=min(width,4096);
 if(!sprite.createSprite(width,big?28:20))return;
 sprite.fillSprite(white);int measured=font_fallback::measure(sprite,value.c_str(),!big,false);
 txt(sprite,value,measured<=lw?(lw-measured)/2:0,0,big?ink:muted,white,big);
}
void UiRenderer::drawIdle(const char* status,const String&){
 layout();metadata(status,title_,true);metadata("等待电脑音乐播放",artist_,false);metadataAt_=millis();metadataTick(metadataAt_);
 if(ready_){viewport_.fillSprite(lyricBg);center(viewport_,"播放后显示歌词",lw/2,lh/2-8,muted,lyricBg);viewport_.pushSprite(lx,ly);}
 drawProgress(0,0);
}
void UiRenderer::drawTrack(const TrackState& state){
 if(!ready_)layout();
 metadata(state.title,title_,true);metadata(state.artist,artist_,false);metadataAt_=millis();metadataTick(metadataAt_);
 index_=anchor_=-2;revision_=UINT32_MAX;visual_=from_=-1;transition_=false;lyricDirty_=true;
 display_.fillRoundRect(10,36,128,128,8,ui_config::kCoverBackground);
 display_.drawCircle(74,94,28,blue);display_.fillCircle(74,94,4,white);
 controls(state.playing);lastProgress_=UINT32_MAX;drawProgress(0,state.durationMs);
}
void UiRenderer::metadataTick(uint32_t now){
 display_.setClipRect(lx,42,lw,28);if(title_.width())title_.pushSprite(lx-int(bounce(now-metadataAt_,title_.width()-lw)),42);
 display_.setClipRect(lx,73,lw,20);if(artist_.width())artist_.pushSprite(lx-int(bounce(now-metadataAt_,artist_.width()-lw)),73);
 display_.clearClipRect();lastMetadata_=now;
}
void UiRenderer::header(){
 time_t epoch=time(nullptr);tm local{};localtime_r(&epoch,&local);
 String dateText,timeText,dayText;
 if(epoch>=1704067200){
  char date[20],clock[12];strftime(date,sizeof(date),"%Y-%m-%d",&local);strftime(clock,sizeof(clock),"%H:%M:%S",&local);
  const char* days[]={"日","一","二","三","四","五","六"};
  dateText=date;timeText=clock;dayText=String("星期")+days[local.tm_wday];
 }else dateText="等待校时";
 // Compose off-screen, then transfer only fields whose visible contents changed.
 // Never clear the LCD header before drawing the new second.
 const bool buffered=headerBuffer_.width()!=0;
 auto field=[&](const String& value,String& previous,int x,int width,uint16_t color){
  if(value==previous&&second_!=-1)return;
  if(buffered){
   headerBuffer_.fillRect(x-6,0,width,24,white);txt(headerBuffer_,value,x-6,3,color);
   display_.setClipRect(x,5,width,24);headerBuffer_.pushSprite(6,5);display_.clearClipRect();
  }else{
   // Opaque, padded text also avoids a clear-then-draw flash if PSRAM is exhausted.
   display_.setClipRect(x,5,width,24);txt(display_,value+"            ",x,8,color);display_.clearClipRect();
  }
  previous=value;
 };
 field(dateText,headerDate_,10,88,epoch>=1704067200?ink:muted);
 if(timeText!=headerTime_||second_==-1)++headerTimeUpdates_;
 field(timeText,headerTime_,98,72,ink);field(dayText,headerDay_,170,70,ink);
 auto info=ClockApp::musicInfo();
 const int barWidth=info.quotaFresh?roundf(constrain(info.remaining,0.f,100.f)*.88f):0;
 const String quotaText=info.quotaFresh?String(info.remaining,0)+"%":"--";
 const String quotaKey=quotaText+":"+String(barWidth);
 if(quotaKey!=headerQuota_){
  if(buffered){
   headerBuffer_.fillRect(234,0,234,24,white);quotaHeader(headerBuffer_,6,quotaText,barWidth,info.quotaFresh);
   display_.setClipRect(240,5,234,24);headerBuffer_.pushSprite(6,5);display_.clearClipRect();
  }else quotaHeader(display_,0,quotaText,barWidth,info.quotaFresh);
  headerQuota_=quotaKey;++headerQuotaUpdates_;
 }
 second_=epoch;
}
void UiRenderer::activity(){
 auto info=ClockApp::musicInfo();String key=info.online?(info.busy?"running":"idle"):"offline";
 if(key==activityKey_)return;activityKey_=key;
 uint16_t bg=info.online&&info.busy?purple:pale,fg=info.online&&info.busy?white:0x5a93;
 display_.fillRoundRect(19,284,110,21,6,bg);
 if(info.online&&info.busy)display_.drawCircle(30,294,3,fg);else display_.drawRect(27,291,6,6,fg);
 center(display_,info.online?(info.busy?"运行中":"空闲中"):"未连接",82,286,fg,bg);
}
void UiRenderer::controls(bool playing){
 if(playing_==playing)return;playing_=playing;display_.fillRect(10,209,128,34,white);
 display_.fillTriangle(21,225,36,214,36,236,blue);display_.fillRect(19,214,3,22,blue);
 if(playing)display_.fillRoundRect(65,216,18,18,2,blue);else display_.fillTriangle(65,214,65,236,84,225,blue);
 display_.fillTriangle(127,225,112,214,112,236,blue);display_.fillRect(127,214,3,22,blue);
}
const char* UiRenderer::controlAt(int x,int y,bool playing) const {
 if(y<205||y>=248||x<8||x>=140)return nullptr;
 return x<52?"previous":x<96?(playing?"pause":"play"):"next";
}
void UiRenderer::controlPending(uint32_t id,uint32_t now){pending_=id;pendingAt_=now;feedback_="等待播放器";messageUntil_=now+5000;}
void UiRenderer::controlResult(uint32_t id,bool success,uint32_t now){if(id!=pending_)return;pending_=0;feedback_=success?"":"播放器未响应";messageUntil_=now+4000;}
void UiRenderer::drawProgress(uint32_t position,uint32_t duration){
 uint32_t seconds=position/1000;if(seconds==lastProgress_)return;lastProgress_=seconds;
 display_.fillRoundRect(10,177,128,6,3,0xe71c);
 if(duration){int width=uint64_t(min(position,duration))*128/duration;if(width)display_.fillRoundRect(10,177,width,6,3,orange);}
 char value[32];snprintf(value,sizeof(value),"%02lu:%02lu/%02lu:%02lu",(unsigned long)(seconds/60),(unsigned long)(seconds%60),(unsigned long)(duration/60000),(unsigned long)(duration/1000%60));
 display_.fillRect(10,189,128,17,white);center(display_,value,74,189,muted);
}
void UiRenderer::buildRows(const LyricsEngine& lyrics,int index){
 anchor_=index;
 for(int slot=0;slot<7;++slot){
  rows_[slot].deleteSprite();String value=lyrics.textAt(index+slot-3);if(value.isEmpty()&&slot==3)value="♪";
  // Every timestamp group stays on one line, including original/translation.
  value.replace("\n"," / ");const bool current=slot==3;
  const int measured=font_fallback::measure(display_,value.c_str(),!current,false);
  // Only the active row needs an off-screen tail. Neighbours are clipped in place.
  const int width=current?min(8192,max(lw,measured)):lw;
  rowWidths_[slot]=current?min(measured,width):lw;
  rows_[slot].setPsram(true);rows_[slot].setColorDepth(16);
  if(!rows_[slot].createSprite(width,40))continue;
  rows_[slot].fillSprite(lyricBg);
  txt(rows_[slot],value,measured<=lw?(lw-measured)/2:0,current?8:12,current?0x2210:0x94b2,lyricBg,current);
 }
}
void UiRenderer::renderLyrics(const LyricsEngine& lyrics,uint32_t position,uint32_t now){
 int target=lyrics.indexAt(position);bool changed=target!=index_||revision_!=lyrics.revision();
 if(changed){
  bool adjacent=revision_==lyrics.revision()&&index_>=-1&&target==index_+1;
  from_=visual_;index_=target;transition_=adjacent;transitionAt_=now;if(!adjacent)visual_=target;
  revision_=lyrics.revision();buildRows(lyrics,target);lyricDirty_=true;
 }
 if(transition_){float t=min(1.f,float(now-transitionAt_)/480.f);float ease=t*t*(3-2*t);visual_=from_+(index_-from_)*ease;if(t>=1){visual_=index_;transition_=false;}lyricDirty_=true;}
 int overflow=rowWidths_[3]-lw;bool horizontal=overflow>0;
 if(!ready_||now-lastFrame_<50||(!lyricDirty_&&!horizontal))return;
 lastFrame_=now;viewport_.fillSprite(lyricBg);
 if(!lyrics.count())center(viewport_,"暂无歌词",lw/2,lh/2-8,muted,lyricBg);
 else for(int slot=0;slot<7;++slot){
  if(!rows_[slot].width())continue;int y=roundf((lh-40)/2+(anchor_+slot-3-visual_)*pitch);
  int x=slot==3?-int(bounce(position>=lyrics.timestampAt(index_)?position-lyrics.timestampAt(index_):0,overflow)):0;
  rows_[slot].pushSprite(&viewport_,x,y);
 }
 viewport_.pushSprite(lx,ly);lyricDirty_=false;++animatedFrames_;
}
void UiRenderer::tick(const TrackState& state,const LyricsEngine& lyrics,uint32_t now){
 if(time(nullptr)!=second_)header();activity();controls(state.active&&state.playing);
 if(now-lastMetadata_>=80&&(title_.width()>lw||artist_.width()>lw))metadataTick(now);
 drawProgress(state.estimatedPosition(now),state.durationMs);if(state.active)renderLyrics(lyrics,state.estimatedPosition(now),now);
 if(pending_&&now-pendingAt_>5000){pending_=0;feedback_="播放器未响应";messageUntil_=now+3000;}
 String message=static_cast<int32_t>(messageUntil_-now)>0?feedback_:"";
 if(shownFeedback_!=message){display_.fillRect(14,258,120,22,white);center(display_,message.length()?message:String("ChatGPT"),74,261,muted);shownFeedback_=message;}
}
void UiRenderer::printStatus() const{
 JsonDocument doc;doc["layout"]="music-v2-cover-home";doc["lyric_index"]=index_;doc["visual_index"]=visual_;doc["animating"]=transition_;
 doc["lyrics_height"]=lh;
 doc["active_lyric_font_px"]=24;doc["lyric_wrap"]=false;
 doc["rendered_frames"]=animatedFrames_;doc["pending_control"]=pending_;doc["feedback"]=feedback_;doc["playing"]=playing_;
 doc["header_buffered"]=headerBuffer_.width()!=0;doc["header_time_updates"]=headerTimeUpdates_;doc["header_quota_updates"]=headerQuotaUpdates_;
 hubConsole().print("MUSIC_STATUS ");serializeJson(doc,hubConsole());hubConsole().println();
}
void UiRenderer::release(){
 viewport_.deleteSprite();title_.deleteSprite();artist_.deleteSprite();for(auto& row:rows_)row.deleteSprite();ready_=false;
 headerBuffer_.deleteSprite();headerDate_=headerTime_=headerDay_=headerQuota_="";second_=-1;headerTimeUpdates_=headerQuotaUpdates_=0;
 index_=anchor_=-2;revision_=UINT32_MAX;visual_=from_=-1;transition_=false;lyricDirty_=true;pending_=0;feedback_=shownFeedback_="";animatedFrames_=0;
}
void UiRenderer::drawDemo(bool){drawIdle("音乐屏","");}
void UiRenderer::drawProvisioning(const String& ssid,const char*,const String& ip){drawIdle(ssid.c_str(),ip);}
void UiRenderer::drawLyricsPreview(const TrackState&){lyricDirty_=true;}
void UiRenderer::drawLyricsWindow(const String&,const String&,const String&,bool,bool,uint32_t){lyricDirty_=true;}
bool UiRenderer::drawLyricsBitmap(char,const uint8_t*,size_t,int,int,bool,uint32_t){return false;}
