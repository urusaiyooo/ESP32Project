#pragma once
#include <WebServer.h>
#include <memory>
#include "cover_renderer.h"
#include "lyrics_engine.h"
#include "track_state.h"
#include "ui_renderer.h"

class ApiServer {
 public:
  ApiServer(TrackState& state, UiRenderer& ui, CoverRenderer& cover, LyricsEngine& lyrics)
      : state_(state), ui_(ui), cover_(cover), lyrics_(lyrics), server_(80) {}
  void begin();
  void setForeground(bool active);
  void stop();
  void tick();
  bool started() const { return started_; }
 private:
  bool ensureForeground();
  bool foreground_ = false;
  void jsonError(int status, const char* code, const char* message);
  void handleHealth();
  void handleTrack();
  void handlePlayback();
  void handleLyrics();
  void handleCoverRaw();
  void handleCover();
  void handleLyricsBitmapRaw();
  void handleLyricsBitmap();
  void handleIdle();
  TrackState& state_;
  UiRenderer& ui_;
  CoverRenderer& cover_;
  LyricsEngine& lyrics_;
  WebServer server_;
  std::unique_ptr<uint8_t[]> coverBuffer_;
  size_t coverLength_ = 0;
  int coverErrorStatus_ = 0;
  const char* coverErrorCode_ = nullptr;
  const char* coverErrorMessage_ = nullptr;
  std::unique_ptr<uint8_t[]> bitmapBuffer_;
  size_t bitmapCapacity_ = 0;
  size_t bitmapLength_ = 0;
  size_t bitmapExpected_ = 0;
  int bitmapWidth_ = 0;
  int bitmapHeight_ = 0;
  char bitmapRole_ = 0;
  int bitmapErrorStatus_ = 0;
  const char* bitmapErrorCode_ = nullptr;
  const char* bitmapErrorMessage_ = nullptr;
  bool bitmapRawActive_ = false;
  bool started_ = false;
  bool routesRegistered_ = false;
};
