#pragma once

#include <Arduino.h>
#include <vector>

struct LyricLine {
  uint32_t timestampMs;
  String text;
};

class LyricsEngine {
 public:
  bool parse(const String& lrc, String& error);
  void clear();
  void release() { clear(); std::vector<LyricLine>().swap(lines_); }
  size_t count() const { return lines_.size(); }
  uint32_t revision() const { return revision_; }
  int indexAt(uint32_t positionMs) const;
  const String& textAt(int index) const;
  uint32_t timestampAt(int index) const {return index>=0&&size_t(index)<lines_.size()?lines_[index].timestampMs:0;}

 private:
  static bool parseTimestamp(const String& token, uint32_t& timestampMs);
  std::vector<LyricLine> lines_;
  uint32_t revision_ = 0;
};
