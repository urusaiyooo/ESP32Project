#include "hub_console.h"
#include "api_server.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include "config.h"
#include "app_shell.h"
#include "codex_app.h"
#include "ui_config.h"

namespace {
class BitmapRequestHandler : public RequestHandler {
 public:
  BitmapRequestHandler(WebServer& server, WebServer::THandlerFunction done,
                       WebServer::THandlerFunction data)
      : server_(server), done_(done), data_(data) {}
  bool canHandle(HTTPMethod method,String uri) override {
    return method==HTTP_POST && uri=="/api/lyrics-bitmap";
  }
  bool canRaw(String uri) override {
    return uri=="/api/lyrics-bitmap" &&
           server_.header("Content-Type").startsWith("application/octet-stream");
  }
  bool handle(WebServer&,HTTPMethod method,String uri) override {
    if (!canHandle(method,uri)) return false;
    done_();return true;
  }
  void raw(WebServer&,String,HTTPRaw&) override { data_(); }
 private:
  WebServer& server_;
  WebServer::THandlerFunction done_,data_;
};
bool readUInt(JsonVariantConst value, uint32_t& out) {
  if (!value.is<uint32_t>()) return false;
  out = value.as<uint32_t>();
  return true;
}
}

void ApiServer::begin() {
  if (started_) return;
  if (!routesRegistered_) {
  const char* headers[] = {"X-Track-ID", "Content-Type", "X-Lyric-Role",
                           "X-Bitmap-Width", "X-Bitmap-Height"};
  server_.collectHeaders(headers, 5);
  server_.on("/api/health", HTTP_GET, [this] { handleHealth(); });
  server_.on("/api/track", HTTP_POST, [this] { handleTrack(); });
  server_.on("/api/playback", HTTP_POST, [this] { handlePlayback(); });
  server_.on("/api/lyrics", HTTP_POST, [this] { handleLyrics(); });
  server_.on("/api/cover", HTTP_POST, [this] { handleCover(); },
             [this] { handleCoverRaw(); });
  server_.addHandler(new BitmapRequestHandler(server_,[this] { handleLyricsBitmap(); },
                                              [this] { handleLyricsBitmapRaw(); }));
  server_.on("/api/idle", HTTP_POST, [this] { handleIdle(); });
  server_.onNotFound([this] { jsonError(404, "not_found", "Unknown endpoint"); });
  routesRegistered_ = true;
  }
  server_.begin();
  started_ = true;
  hubConsole().println("[INFO][API] HTTP server started on port 80");
}

void ApiServer::stop() {
  if (!started_) return;
  server_.stop();
  started_ = false;
  hubConsole().println("[INFO][API] HTTP server stopped");
}

void ApiServer::handleLyricsBitmapRaw() {
  if (!foreground_) return;
  HTTPRaw& raw = server_.raw();
  if (raw.status == RAW_START) {
    bitmapRawActive_ = server_.header("Content-Type").startsWith("application/octet-stream");
    if (!bitmapRawActive_) return;
    bitmapBuffer_.reset();
    bitmapCapacity_ = 0;
    bitmapLength_ = 0;
    bitmapExpected_ = 0;
    bitmapErrorStatus_ = 0;
    bitmapErrorCode_ = nullptr;
    bitmapErrorMessage_ = nullptr;
    const String id = sanitizeUtf8(server_.header("X-Track-ID"), 96);
    if (!state_.matches(id)) {
      bitmapErrorStatus_ = 409; bitmapErrorCode_ = "stale_track";
      bitmapErrorMessage_ = "X-Track-ID is not current"; return;
    }
    const String role = server_.header("X-Lyric-Role");
    bitmapRole_ = role.length() == 1 ? role[0] : 0;
    bitmapWidth_ = server_.header("X-Bitmap-Width").toInt();
    bitmapHeight_ = server_.header("X-Bitmap-Height").toInt();
    if ((bitmapRole_ != 'p' && bitmapRole_ != 'c' && bitmapRole_ != 'n' &&
         bitmapRole_ != 't' && bitmapRole_ != 'a' && bitmapRole_ != 'b') ||
        bitmapWidth_ <= 0 || bitmapWidth_ > 1024 || bitmapHeight_ <= 0 ||
        bitmapHeight_ > 48) {
      bitmapErrorStatus_ = 422; bitmapErrorCode_ = "invalid_bitmap_metadata";
      bitmapErrorMessage_ = "Invalid role or bitmap dimensions"; return;
    }
    bitmapExpected_ = (static_cast<size_t>(bitmapWidth_) * bitmapHeight_ + 1) / 2;
    if (server_.clientContentLength() != static_cast<int>(bitmapExpected_)) {
      bitmapErrorStatus_ = 400; bitmapErrorCode_ = "invalid_length";
      bitmapErrorMessage_ = "Alpha4 body length does not match dimensions"; return;
    }
    bitmapBuffer_.reset(new (std::nothrow) uint8_t[bitmapExpected_]);
    if (!bitmapBuffer_) {
      bitmapErrorStatus_ = 503; bitmapErrorCode_ = "insufficient_memory";
      bitmapErrorMessage_ = "Not enough memory for lyric bitmap";
      return;
    }
    // Consume precisely Content-Length here. The framework's generic raw loop
    // otherwise waits for a full final chunk even when only a few bytes remain.
    bitmapLength_=server_.client().readBytes(bitmapBuffer_.get(),bitmapExpected_);
    raw.totalSize=server_.clientContentLength();
    if (bitmapLength_!=bitmapExpected_) {
      bitmapErrorStatus_=400; bitmapErrorCode_="incomplete_upload";
      bitmapErrorMessage_="Bitmap transfer did not complete";
    }
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (!bitmapRawActive_) return;
    if (bitmapErrorStatus_ || !bitmapBuffer_) return;
    if (bitmapLength_ + raw.currentSize > bitmapExpected_) {
      bitmapErrorStatus_ = 400; bitmapErrorCode_ = "invalid_length";
      bitmapErrorMessage_ = "Bitmap body exceeded expected length";
      bitmapBuffer_.reset(); return;
    }
    memcpy(bitmapBuffer_.get() + bitmapLength_, raw.buf, raw.currentSize);
    bitmapLength_ += raw.currentSize;
  } else if (raw.status == RAW_ABORTED) {
    if (!bitmapRawActive_) return;
    bitmapErrorStatus_ = 400; bitmapErrorCode_ = "upload_aborted";
    bitmapErrorMessage_ = "Bitmap upload was interrupted";
    bitmapBuffer_.reset();
  }
}

void ApiServer::handleLyricsBitmap() {
  if (!ensureForeground()) return;
  const uint32_t started = micros();
  if (server_.header("Content-Type").startsWith("application/octet-stream")) {
    if (bitmapErrorStatus_) {
      jsonError(bitmapErrorStatus_, bitmapErrorCode_, bitmapErrorMessage_);
      bitmapBuffer_.reset(); bitmapCapacity_ = 0; bitmapRawActive_ = false;
      return;
    }
    if (!bitmapRawActive_ || !bitmapBuffer_ || bitmapLength_ != bitmapExpected_) {
      jsonError(400, "incomplete_upload", "Alpha4 bitmap body was incomplete");
      bitmapBuffer_.reset(); bitmapCapacity_ = 0; bitmapRawActive_ = false;
      return;
    }
    const bool ok = ui_.drawLyricsBitmap(bitmapRole_, bitmapBuffer_.get(), bitmapLength_,
                                         bitmapWidth_, bitmapHeight_, state_.playing, millis());
    const char role = bitmapRole_;
    const int width = bitmapWidth_;
    const int height = bitmapHeight_;
    bitmapBuffer_.reset();
    bitmapCapacity_ = 0;
    bitmapLength_ = 0;
    bitmapExpected_ = 0;
    bitmapRawActive_ = false;
    if (!ok) { jsonError(422, "bitmap_draw_failed", "Lyric bitmap draw failed"); return; }
    hubConsole().printf("[PERF] lyric bitmap raw role=%c size=%dx%d draw_us=%u heap=%u\n",
                  role, width, height,
                  static_cast<uint32_t>(micros() - started), ESP.getFreeHeap());
    server_.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  const String id = sanitizeUtf8(server_.header("X-Track-ID"), 96);
  if (!state_.matches(id)) { jsonError(409, "stale_track", "X-Track-ID is not current"); return; }
  const String role = server_.header("X-Lyric-Role");
  const char bitmapRole = role.length() == 1 ? role[0] : 0;
  const int width = server_.header("X-Bitmap-Width").toInt();
  const int height = server_.header("X-Bitmap-Height").toInt();
  if ((bitmapRole != 'p' && bitmapRole != 'c' && bitmapRole != 'n' &&
       bitmapRole != 't' && bitmapRole != 'a' && bitmapRole != 'b') ||
      width <= 0 || width > 1024 || height <= 0 || height > 48) {
    jsonError(422, "invalid_bitmap_metadata", "Invalid role or bitmap dimensions"); return;
  }
  const size_t expected = (static_cast<size_t>(width) * height + 1) / 2;
  if (!server_.hasArg("plain") || server_.arg("plain").length() > 30000) {
    jsonError(413, "body_too_large", "Bitmap JSON body is missing or too large"); return;
  }
  JsonDocument doc;
  // Compatibility path for existing JSON clients. The new companion uses the
  // binary route above to avoid large Base64 copies and growing JSON strings.
  String bitmapJson=server_.arg("plain");
  if (bitmapJson.isEmpty()) { jsonError(400,"invalid_json","Empty bitmap body"); return; }
  const DeserializationError bitmapJsonError=deserializeJson(doc, &bitmapJson[0],bitmapJson.length());
  if (bitmapJsonError) {
    hubConsole().printf("[WARN][BITMAP] JSON=%s body_bytes=%u heap=%u\n",bitmapJsonError.c_str(),
                  server_.arg("plain").length(),ESP.getFreeHeap());
    jsonError(400, "invalid_json", bitmapJsonError.c_str()); return;
  }
  const char* encoded = doc["alpha4"] | "";
  if (!strlen(encoded)) { jsonError(422, "missing_alpha", "alpha4 is required"); return; }
  if (bitmapCapacity_ < expected) {
    std::unique_ptr<uint8_t[]> next(new (std::nothrow) uint8_t[expected]);
    if (!next) { jsonError(503, "insufficient_memory", "Not enough memory for bitmap"); return; }
    bitmapBuffer_ = std::move(next);
    bitmapCapacity_ = expected;
  }
  size_t decodedLength = 0;
  if (mbedtls_base64_decode(bitmapBuffer_.get(), expected, &decodedLength,
                            reinterpret_cast<const uint8_t*>(encoded), strlen(encoded)) != 0 ||
      decodedLength != expected) {
    jsonError(400, "invalid_alpha", "Base64 alpha length does not match dimensions"); return;
  }
  const bool ok = ui_.drawLyricsBitmap(bitmapRole, bitmapBuffer_.get(), decodedLength, width, height,
                                       state_.playing, millis());
  if (!ok) { jsonError(422, "bitmap_draw_failed", "Lyric bitmap draw failed"); return; }
  hubConsole().printf("[PERF] lyric bitmap role=%c size=%dx%d draw_us=%u heap=%u\n",
                bitmapRole, width, height,
                static_cast<uint32_t>(micros() - started), ESP.getFreeHeap());
  server_.send(200, "application/json", "{\"ok\":true}");
}

void ApiServer::handleCoverRaw() {
  if (!foreground_) return;
  HTTPRaw& raw = server_.raw();
  if (raw.status == RAW_START) {
    coverBuffer_.reset();
    coverLength_ = 0;
    coverErrorStatus_ = 0;
    coverErrorCode_ = nullptr;
    coverErrorMessage_ = nullptr;
    const String id = sanitizeUtf8(server_.header("X-Track-ID"), 96);
    if (!state_.matches(id)) {
      coverErrorStatus_ = 409;
      coverErrorCode_ = "stale_track";
      coverErrorMessage_ = "X-Track-ID is not current";
      return;
    }
    if (!server_.header("Content-Type").startsWith("image/jpeg")) {
      coverErrorStatus_ = 415;
      coverErrorCode_ = "unsupported_media_type";
      coverErrorMessage_ = "Content-Type must be image/jpeg";
      return;
    }
    const int contentLength = server_.clientContentLength();
    if (contentLength <= 0) {
      coverErrorStatus_ = 422;
      coverErrorCode_ = "empty_cover";
      coverErrorMessage_ = "JPEG body is empty";
      return;
    }
    if (static_cast<size_t>(contentLength) > config::kMaxCoverBytes) {
      coverErrorStatus_ = 413;
      coverErrorCode_ = "body_too_large";
      coverErrorMessage_ = "Cover JPEG limit is 81920 bytes";
      return;
    }
    coverBuffer_.reset(new (std::nothrow) uint8_t[contentLength]);
    if (!coverBuffer_) {
      coverErrorStatus_ = 503;
      coverErrorCode_ = "insufficient_memory";
      coverErrorMessage_ = "Not enough memory for cover JPEG";
    }
    return;
  }

  if (raw.status == RAW_WRITE) {
    if (coverErrorStatus_ || !coverBuffer_) return;
    const size_t expected = static_cast<size_t>(server_.clientContentLength());
    if (coverLength_ + raw.currentSize > expected) {
      coverErrorStatus_ = 400;
      coverErrorCode_ = "invalid_length";
      coverErrorMessage_ = "JPEG body exceeded Content-Length";
      coverBuffer_.reset();
      return;
    }
    memcpy(coverBuffer_.get() + coverLength_, raw.buf, raw.currentSize);
    coverLength_ += raw.currentSize;
  } else if (raw.status == RAW_ABORTED) {
    coverErrorStatus_ = 400;
    coverErrorCode_ = "upload_aborted";
    coverErrorMessage_ = "JPEG upload was interrupted";
    coverBuffer_.reset();
  }
}

void ApiServer::handleCover() {
  if (!ensureForeground()) return;
  const uint32_t requestStarted = micros();
  if (coverErrorStatus_) {
    jsonError(coverErrorStatus_, coverErrorCode_, coverErrorMessage_);
    coverBuffer_.reset();
    return;
  }
  if (!coverBuffer_ || coverLength_ != static_cast<size_t>(server_.clientContentLength())) {
    jsonError(400, "incomplete_upload", "JPEG body was incomplete");
    coverBuffer_.reset();
    return;
  }
  const uint32_t started = millis();
  uint16_t width = 0, height = 0;
  const CoverRenderResult result = cover_.render(coverBuffer_.get(), coverLength_, width, height);
  const size_t received = coverLength_;
  coverBuffer_.reset();
  coverLength_ = 0;
  switch (result) {
    case CoverRenderResult::Ok:
      hubConsole().printf("[INFO][COVER] bytes=%u size=%ux%u decode_ms=%u heap=%u\n",
                    static_cast<unsigned>(received), width, height,
                    static_cast<unsigned>(millis() - started), ESP.getFreeHeap());
      hubConsole().printf("[PERF] cover decode+lcd us=%u\n",
                    static_cast<uint32_t>(micros() - requestStarted));
      server_.send(200, "application/json", "{\"ok\":true}");
      return;
    case CoverRenderResult::WrongDimensions:
      jsonError(422, "wrong_dimensions", "Cover must be exactly 160x160"); return;
    case CoverRenderResult::UnsupportedJpeg:
      jsonError(415, "unsupported_jpeg", "Only baseline JPEG is supported"); return;
    case CoverRenderResult::DecodeFailed:
      jsonError(422, "decode_failed", "JPEG decoder rejected the image"); return;
    case CoverRenderResult::InvalidJpeg:
    default:
      jsonError(422, "invalid_jpeg", "Malformed JPEG body"); return;
  }
}

void ApiServer::tick() { if (started_) server_.handleClient(); }

void ApiServer::jsonError(int status, const char* code, const char* message) {
  JsonDocument doc;
  doc["ok"] = false; doc["error"] = code; doc["message"] = message;
  String body; serializeJson(doc, body);
  server_.send(status, "application/json; charset=utf-8", body);
}

void ApiServer::handleHealth() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["app"] = "DeskHub";
  doc["active_app"] = currentAppName();
  doc["codex_connected"] = CodexApp::isConnected();
  doc["touch_calibrated"] = CodexApp::isCalibrated();
  doc["music_active"] = foreground_;
  doc["version"] = config::kVersion;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["minimum_free_heap"] = ESP.getMinFreeHeap();
  doc["largest_free_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  doc["uptime_ms"] = millis();
  doc["track_id"] = state_.active ? state_.trackId : "";
  doc["lyric_lines"] = lyrics_.count();
  doc["lyric_index"] = state_.active ? lyrics_.indexAt(state_.estimatedPosition(millis())) : -1;
  String body; serializeJson(doc, body);
  server_.send(200, "application/json; charset=utf-8", body);
}

void ApiServer::handleTrack() {
  if (!ensureForeground()) return;
  const uint32_t requestStarted = micros();
  hubConsole().println("[PERF] track request received");
  if (!server_.hasArg("plain") || server_.arg("plain").length() > 2048) {
    jsonError(413, "body_too_large", "Track JSON limit is 2048 bytes"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    jsonError(400, "invalid_json", "Malformed JSON body"); return;
  }
  hubConsole().printf("[PERF] track json parse us=%u\n",
                static_cast<uint32_t>(micros() - requestStarted));
  const char* id = doc["track_id"] | "";
  const char* title = doc["title"] | "";
  const char* artist = doc["artist"] | "";
  const char* album = doc["album"] | "";
  uint32_t duration = 0;
  if (!strlen(id) || !strlen(title) || !readUInt(doc["duration_ms"], duration)) {
    jsonError(422, "invalid_track", "track_id, title and duration_ms are required"); return;
  }
  state_.setTrack(id, title, artist, album, duration, millis());
  lyrics_.clear();
  ui_.drawTrack(state_);
  hubConsole().printf("[INFO][TRACK] id=%s title=%s duration_ms=%u\n",
                state_.trackId.c_str(), state_.title.c_str(), state_.durationMs);
  server_.send(200, "application/json", "{\"ok\":true}");
}

void ApiServer::handlePlayback() {
  if (!ensureForeground()) return;
  if (!server_.hasArg("plain") || server_.arg("plain").length() > 1024) {
    jsonError(413, "body_too_large", "Playback JSON limit is 1024 bytes"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain"))) {
    jsonError(400, "invalid_json", "Malformed JSON body"); return;
  }
  const String id = sanitizeUtf8(doc["track_id"] | "", 96);
  if (!state_.matches(id)) { jsonError(409, "stale_track", "track_id is not current"); return; }
  uint32_t position = 0, duration = 0;
  if (!readUInt(doc["position_ms"], position) || !readUInt(doc["duration_ms"], duration) ||
      !doc["playing"].is<bool>()) {
    jsonError(422, "invalid_playback", "position_ms, duration_ms and playing are required"); return;
  }
  state_.setPlayback(position, duration, doc["playing"].as<bool>(), millis());
  ui_.drawProgress(state_.estimatedPosition(millis()), state_.durationMs);
  server_.send(200, "application/json", "{\"ok\":true}");
}

void ApiServer::handleLyrics() {
  if (!ensureForeground()) return;
  const String id = sanitizeUtf8(server_.header("X-Track-ID"), 96);
  if (!state_.matches(id)) { jsonError(409, "stale_track", "X-Track-ID is not current"); return; }
  if (server_.hasArg("plain") && server_.arg("plain").length() > config::kMaxLyricsBytes) {
    jsonError(413, "body_too_large", "Lyrics limit is 32768 bytes"); return;
  }
  const String body = server_.hasArg("plain") ? server_.arg("plain") : String();
  String error;
  if (!lyrics_.parse(body, error)) {
    jsonError(422, "invalid_lyrics", error.c_str()); return;
  }
  const int index = lyrics_.indexAt(state_.estimatedPosition(millis()));
  ui_.drawLyricsWindow(lyrics_.textAt(index - 1), lyrics_.textAt(index),
                       lyrics_.textAt(index + 1), lyrics_.count() > 0,
                       state_.playing, millis());
  hubConsole().printf("[INFO][LYRICS] parsed_lines=%u current_index=%d\n",
                static_cast<unsigned>(lyrics_.count()), index);
  JsonDocument response; response["ok"] = true; response["lines"] = lyrics_.count();
  String output; serializeJson(response, output);
  server_.send(200, "application/json; charset=utf-8", output);
}

void ApiServer::handleIdle() {
  if (!ensureForeground()) return;
  state_.clear();
  lyrics_.clear();
  ui_.drawIdle("等待播放", "");
  server_.send(200, "application/json", "{\"ok\":true}");
}

bool ApiServer::ensureForeground() {
  if (foreground_) return true;
  jsonError(409,"app_inactive","Open Music on DeskHub to resume synchronization");
  return false;
}
void ApiServer::setForeground(bool active) {
  foreground_=active;
  coverBuffer_.reset(); coverLength_=0;
  bitmapBuffer_.reset(); bitmapCapacity_=bitmapLength_=bitmapExpected_=0;
  bitmapRawActive_=false;
}
