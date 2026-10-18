#include "lyrics_engine.h"
#include <algorithm>
#include "track_state.h"

namespace {
constexpr size_t kMaxLyricLines = 1000;
const String kEmpty;
}

bool LyricsEngine::parseTimestamp(const String& token, uint32_t& timestampMs) {
  const int colon = token.indexOf(':');
  if (colon <= 0 || colon + 1 >= static_cast<int>(token.length())) return false;
  for (int i = 0; i < colon; ++i) if (!isDigit(token[i])) return false;
  const String minutesText = token.substring(0, colon);
  String secondsText = token.substring(colon + 1);
  const int dot = secondsText.indexOf('.');
  String wholeSeconds = dot >= 0 ? secondsText.substring(0, dot) : secondsText;
  String fraction = dot >= 0 ? secondsText.substring(dot + 1) : String();
  if (wholeSeconds.isEmpty() || wholeSeconds.length() > 2 || fraction.length() > 3) return false;
  for (size_t i = 0; i < wholeSeconds.length(); ++i) if (!isDigit(wholeSeconds[i])) return false;
  for (size_t i = 0; i < fraction.length(); ++i) if (!isDigit(fraction[i])) return false;
  const uint32_t seconds = wholeSeconds.toInt();
  if (seconds >= 60) return false;
  uint32_t fractionMs = 0;
  if (fraction.length() == 1) fractionMs = fraction.toInt() * 100;
  else if (fraction.length() == 2) fractionMs = fraction.toInt() * 10;
  else if (fraction.length() == 3) fractionMs = fraction.toInt();
  const uint64_t total = static_cast<uint64_t>(minutesText.toInt()) * 60000ULL +
                         seconds * 1000ULL + fractionMs;
  if (total > UINT32_MAX) return false;
  timestampMs = static_cast<uint32_t>(total);
  return true;
}

bool LyricsEngine::parse(const String& lrc, String& error) {
  std::vector<LyricLine> parsed;
  parsed.reserve(min(static_cast<size_t>(64), kMaxLyricLines));
  int32_t offsetMs = 0;
  int cursor = 0;
  while (cursor <= static_cast<int>(lrc.length())) {
    int end = lrc.indexOf('\n', cursor);
    if (end < 0) end = lrc.length();
    String row = lrc.substring(cursor, end);
    row.trim();
    if (row.startsWith("[offset:") && row.endsWith("]")) {
      String value = row.substring(8, row.length() - 1);
      value.trim();
      bool valid = !value.isEmpty();
      size_t start = valid && (value[0] == '+' || value[0] == '-') ? 1 : 0;
      if (start == value.length()) valid = false;
      for (size_t i = start; valid && i < value.length(); ++i) valid &= isDigit(value[i]);
      if (valid) offsetMs = constrain(value.toInt(), -600000L, 600000L);
    } else {
      std::vector<uint32_t> timestamps;
      int tagCursor = 0;
      while (tagCursor < static_cast<int>(row.length()) && row[tagCursor] == '[') {
        const int close = row.indexOf(']', tagCursor + 1);
        if (close < 0) break;
        uint32_t timestamp = 0;
        if (!parseTimestamp(row.substring(tagCursor + 1, close), timestamp)) break;
        timestamps.push_back(timestamp);
        tagCursor = close + 1;
      }
      const String text = sanitizeUtf8(row.substring(tagCursor), 192);
      if (!text.isEmpty()) {
        for (uint32_t timestamp : timestamps) {
          if (parsed.size() >= kMaxLyricLines) break;
          parsed.push_back({timestamp, text});
        }
      }
    }
    if (end >= static_cast<int>(lrc.length()) || parsed.size() >= kMaxLyricLines) break;
    cursor = end + 1;
  }

  for (auto& line : parsed) {
    const int64_t adjusted = static_cast<int64_t>(line.timestampMs) + offsetMs;
    line.timestampMs = adjusted <= 0 ? 0 :
        (adjusted > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(adjusted));
  }
  std::stable_sort(parsed.begin(), parsed.end(),
                   [](const LyricLine& a, const LyricLine& b) {
                     return a.timestampMs < b.timestampMs;
                   });
  lines_.swap(parsed);
  ++revision_;
  error = "";
  return true;
}

void LyricsEngine::clear() {
  lines_.clear();
  lines_.shrink_to_fit();
  ++revision_;
}

int LyricsEngine::indexAt(uint32_t positionMs) const {
  size_t low = 0, high = lines_.size();
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (lines_[middle].timestampMs <= positionMs) low = middle + 1;
    else high = middle;
  }
  return low ? static_cast<int>(low - 1) : -1;
}

const String& LyricsEngine::textAt(int index) const {
  return index >= 0 && static_cast<size_t>(index) < lines_.size() ?
      lines_[index].text : kEmpty;
}
