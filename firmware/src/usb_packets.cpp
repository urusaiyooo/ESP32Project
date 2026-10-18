#include "usb_packets.h"
#include "hub_console.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

namespace UsbPackets {
namespace {
uint8_t* buffer=nullptr;
uint32_t id=0,expectedCrc=0,crc=0xffffffff,last=0;
size_t expected=0,received=0;
String kind;
void clear() {if(buffer)heap_caps_free(buffer);buffer=nullptr;kind="";expected=received=0;}
void fail(uint32_t request) {hubConsole().printf("USB_ERROR %lu\n",static_cast<unsigned long>(request));clear();}
}
void tick() {if(buffer && millis()-last>30000) fail(id);}
bool line(const String& input) {
  if (!input.startsWith("HUB_USB ")) return false;
  JsonDocument doc;
  if(deserializeJson(doc,input.substring(8)) || !doc["id"].is<uint32_t>()) {fail(0);return true;}
  uint32_t request=doc["id"].as<uint32_t>();String op=doc["op"] | "";
  if(op=="begin") {
    clear();kind=String(doc["kind"] | "");
    if(!doc["length"].is<uint32_t>() || !doc["crc"].is<uint32_t>()) {fail(request);return true;}
    expected=doc["length"].as<uint32_t>();
    size_t limit=kind=="cover" ? 512*1024 : kind=="lyrics" ? 65536 :
        kind=="track" ? 16384 : kind=="dashboard" ? 8192 : kind=="playback" || kind=="idle" ? 1024 : 0;
    if(!limit || !expected || expected>limit) {fail(request);return true;}
    buffer=static_cast<uint8_t*>(heap_caps_malloc(expected+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!buffer) {fail(request);return true;}
    id=request;expectedCrc=doc["crc"].as<uint32_t>();crc=0xffffffff;received=0;last=millis();
    hubConsole().printf("USB_ACK %lu 0\n",static_cast<unsigned long>(id));return true;
  }
  if(!buffer || request!=id) {fail(request);return true;}
  last=millis();
  if(op=="chunk") {
    if(!doc["offset"].is<uint32_t>() || doc["offset"].as<uint32_t>()!=received) {fail(request);return true;}
    const char* encoded=doc["data"] | "";uint8_t decoded[256];size_t count=0;
    if(mbedtls_base64_decode(decoded,sizeof(decoded),&count,reinterpret_cast<const uint8_t*>(encoded),strlen(encoded)) ||
       !count || received+count>expected) {fail(request);return true;}
    memcpy(buffer+received,decoded,count);received+=count;
    for(size_t i=0;i<count;++i) {
      crc^=decoded[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&static_cast<uint32_t>(-static_cast<int32_t>(crc&1)));
    }
    hubConsole().printf("USB_ACK %lu %u\n",static_cast<unsigned long>(id),static_cast<unsigned>(received));return true;
  }
  if(op=="end" && received==expected && (crc^0xffffffffu)==expectedCrc) {
    buffer[expected]=0;
    const bool okay=receiveUsbPacket(kind,buffer,expected);
    hubConsole().printf("USB_DONE %lu %s\n",static_cast<unsigned long>(id),okay ? "OK" : "REJECTED");
    clear();return true;
  }
  fail(request);return true;
}
}
