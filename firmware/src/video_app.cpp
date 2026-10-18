#include "video_app.h"
#include "app_shell.h"
#include "hub_console.h"
#include <ArduinoJson.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <esp_crc.h>

namespace VideoApp {
namespace {
struct Header {
  char magic[8];
  uint32_t version,width,height,fpsNum,fpsDen,count,indexOffset,payloadOffset,
           payloadBytes,maxFrame,payloadCrc,indexCrc,reserved0,reserved1;
} header{};
static_assert(sizeof(Header)==64,"On-flash header must be 64 bytes");
struct Frame {uint32_t offset,length;};
const esp_partition_t* partition=nullptr;
Frame* index=nullptr;
uint8_t* compressed=nullptr;
lgfx::LGFX_Sprite picture(&sharedDisplay);
bool ready=false,active=false;
uint32_t next=0,shown=0,skipped=0,started=0,elapsed=0,renderMs=0;
const char* result="idle";
void release() {
  picture.deleteSprite();free(compressed);compressed=nullptr;
  free(index);index=nullptr;
}
bool read(size_t offset,void* data,size_t length) {
  return partition&&offset<=partition->size&&length<=partition->size-offset&&
         esp_partition_read(partition,offset,data,length)==ESP_OK;
}
}
void begin() {
  partition=esp_partition_find_first(static_cast<esp_partition_type_t>(0x40),ESP_PARTITION_SUBTYPE_ANY,"video");
  ready=read(0,&header,sizeof(header)) && !memcmp(header.magic,"DHVIDEO1",8) &&
    header.version==1 && header.width==320 && header.height==180 &&
    header.fpsNum==12 && header.fpsDen==1 && header.count>0 && header.count<=10000 &&
    header.indexOffset==64 && header.payloadOffset>=64+header.count*sizeof(Frame) &&
    header.payloadOffset<=partition->size && header.payloadBytes<=partition->size-header.payloadOffset &&
    header.maxFrame>=4 && header.maxFrame<=65536 && !header.reserved0 && !header.reserved1;
  result=ready?"idle":"missing_media";
}
bool start() {
  if(!ready||active)return false;
  release();
  index=static_cast<Frame*>(heap_caps_malloc(header.count*sizeof(Frame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  compressed=static_cast<uint8_t*>(heap_caps_malloc(header.maxFrame,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  picture.setColorDepth(16);picture.setPsram(true);
  if(!index||!compressed||!picture.createSprite(320,180)||
     !read(header.indexOffset,index,header.count*sizeof(Frame))||
     esp_crc32_le(0,reinterpret_cast<uint8_t*>(index),header.count*sizeof(Frame))!=header.indexCrc) {
    result="memory_or_index_error";release();return false;
  }
  uint32_t end=0;
  for(uint32_t i=0;i<header.count;++i) {
    const auto& f=index[i];
    if(f.offset!=end||f.length<4||f.length>header.maxFrame||f.offset>header.payloadBytes||f.length>header.payloadBytes-f.offset) {
      result="invalid_index";release();return false;
    }
    end=f.offset+f.length;
  }
  if(end!=header.payloadBytes){result="invalid_length";release();return false;}
  sharedDisplay.clearClipRect();sharedDisplay.fillScreen(0);
  next=shown=skipped=elapsed=renderMs=0;started=millis();active=true;result="playing";
  hubConsole().println("VIDEO_EVENT started");return true;
}
void stop() {
  if(active){elapsed=millis()-started;result="stopped";hubConsole().println("VIDEO_EVENT stopped");}
  active=false;release();
}
bool playing(){return active;}
bool tick() {
  if(!active)return false;
  uint32_t now=millis();elapsed=now-started;
  if(next>=header.count) {
    if(elapsed<uint64_t(header.count)*1000/12)return false;
    active=false;result="complete";release();hubConsole().println("VIDEO_EVENT complete");return true;
  }
  uint32_t due=uint64_t(elapsed)*12/1000;
  if(due<next)return false;
  // Keep the complete 110-second timeline even if a slow display misses a frame.
  if(due>next){uint32_t target=min(due,header.count-1);skipped+=target-next;next=target;}
  const Frame f=index[next];
  const uint32_t renderAt=millis();
  if(!read(header.payloadOffset+f.offset,compressed,f.length)||compressed[0]!=0xff||compressed[1]!=0xd8||
     compressed[f.length-2]!=0xff||compressed[f.length-1]!=0xd9||
     !picture.drawJpg(compressed,f.length,0,0,320,180)) {
    active=false;result="decode_error";release();hubConsole().println("VIDEO_EVENT decode_error");return true;
  }
  picture.pushSprite(80,70);renderMs+=millis()-renderAt;++shown;++next;
  return false;
}
void printStatus() {
  JsonDocument doc;doc["ready"]=ready;doc["playing"]=active;doc["result"]=result;
  doc["frames"]=header.count;doc["next_frame"]=next;doc["shown"]=shown;doc["skipped"]=skipped;
  doc["elapsed_ms"]=active?millis()-started:elapsed;doc["duration_ms"]=uint64_t(header.count)*1000/12;
  doc["average_render_ms"]=shown?float(renderMs)/shown:0;
  doc["psram_free"]=ESP.getFreePsram();
  hubConsole().print("VIDEO_STATUS ");serializeJson(doc,hubConsole());hubConsole().println();
}
}
