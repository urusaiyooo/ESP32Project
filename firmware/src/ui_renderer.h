#pragma once
#include "display_driver.h"
#include "lyrics_marquee.h"
#include "lyrics_engine.h"
#include "track_state.h"
class UiRenderer {
 public:
  UiRenderer(DisplayDriver& display,LyricsMarquee&):display_(display),viewport_(&display),title_(&display),artist_(&display){}
  void release();
  void drawDemo(bool longText=false);
  void drawIdle(const char* status,const String& ip);
  void drawProvisioning(const String& ssid,const char* password,const String& ip);
  void drawTrack(const TrackState& state);
  void drawLyricsPreview(const TrackState& state);
  void drawLyricsWindow(const String& previous,const String& current,const String& next,bool hasLyrics,bool playing,uint32_t now);
  void drawProgress(uint32_t positionMs,uint32_t durationMs);
  bool drawLyricsBitmap(char role,const uint8_t* alpha4,size_t length,int width,int height,bool playing,uint32_t now);
  void tick(const TrackState& state,const LyricsEngine& lyrics,uint32_t now);
  const char* controlAt(int x,int y,bool playing) const;
  void controlPending(uint32_t id,uint32_t now);
  void controlResult(uint32_t id,bool success,uint32_t now);
  void printStatus() const;
 private:
  void layout();void controls(bool playing);void header();void activity();
  void metadata(const String& text,lgfx::LGFX_Sprite& sprite,bool big);
  void metadataTick(uint32_t now);
  void buildRows(const LyricsEngine& lyrics,int index);
  void renderLyrics(const LyricsEngine& lyrics,uint32_t position,uint32_t now);
  DisplayDriver& display_;
  lgfx::LGFX_Sprite viewport_,title_,artist_,rows_[7];
  int rowWidths_[7]{};
  bool ready_=false,playing_=false,lyricDirty_=true,transition_=false;
  int index_=-2,anchor_=-2;float from_=-1,visual_=-1;
  uint32_t revision_=UINT32_MAX,transitionAt_=0,lastFrame_=0,metadataAt_=0,lastMetadata_=0;
  uint32_t animatedFrames_=0,pending_=0,pendingAt_=0,messageUntil_=0,lastProgress_=UINT32_MAX;
  int64_t second_=-1;String activityKey_,feedback_,shownFeedback_;
};
