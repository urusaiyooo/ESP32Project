#include "progressive_cover.h"
#include "ui_config.h"
#include <esp_heap_caps.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#define STBI_MALLOC(n) heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_REALLOC(p,n) heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_FREE(p) heap_caps_free(p)
#include "../third_party/stb_image.h"

bool drawProgressiveCover(DisplayDriver& display,const uint8_t* data,size_t length,uint32_t width,uint32_t height) {
  if(!width || !height || uint64_t(width)*height>1024*1024) return false;
  int w=0,h=0,channels=0;
  uint8_t* pixels=stbi_load_from_memory(data,length,&w,&h,&channels,3);
  if(!pixels)return false;
  float scale=min(float(ui_config::kCoverWidth)/w,float(ui_config::kCoverHeight)/h);
  int outW=max(1,int(w*scale)),outH=max(1,int(h*scale));
  lgfx::LGFX_Sprite scaled(&display);scaled.setPsram(true);scaled.setColorDepth(24);
  if(!scaled.createSprite(outW,outH)){stbi_image_free(pixels);return false;}
  for(int y=0;y<outH;++y) {
    for(int x=0;x<outW;++x) {
      size_t at=(size_t(y)*h/outH*w+size_t(x)*w/outW)*3;
      scaled.drawPixel(x,y,(uint32_t(pixels[at])<<16)|(uint32_t(pixels[at+1])<<8)|pixels[at+2]);
    }
    yield();
  }
  display.fillRect(ui_config::kCoverX,ui_config::kCoverY,ui_config::kCoverWidth,ui_config::kCoverHeight,ui_config::kCoverBackground);
  scaled.pushSprite(ui_config::kCoverX+(ui_config::kCoverWidth-outW)/2,ui_config::kCoverY+(ui_config::kCoverHeight-outH)/2);
  scaled.deleteSprite();stbi_image_free(pixels);return true;
}
