#include "IcalParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace {

bool isLeapYear(int year) {
  return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

int daysInMonth(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  if (month >= 1 && month <= 12) return days[month - 1];
  return 30;
}

int64_t toEpoch(int year, int month, int day, int hour, int min, int sec) {
  if (year < 1970) return 0;
  int64_t days = 0;
  for (int y = 1970; y < year; ++y) {
    days += isLeapYear(y) ? 366 : 365;
  }
  for (int m = 1; m < month; ++m) {
    days += daysInMonth(year, m);
  }
  days += (day - 1);
  return days * 86400LL + hour * 3600LL + min * 60LL + sec;
}

// Extract property value after ':'
std::string getPropertyValue(const std::string& line) {
  const size_t colonPos = line.find(':');
  if (colonPos == std::string::npos) return "";
  return line.substr(colonPos + 1);
}

}  // namespace

std::string IcalParser::unescapeText(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\' && i + 1 < str.size()) {
      const char next = str[i + 1];
      if (next == 'n' || next == 'N') {
        result += ' ';  // Convert line breaks in summary/location to space
        i++;
      } else if (next == ',' || next == ';' || next == '\\') {
        result += next;
        i++;
      } else {
        result += str[i];
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

int64_t IcalParser::parseDateTime(const std::string& rawStr, int utcOffsetSeconds, bool& outAllDay) {
  outAllDay = false;
  // Format can be:
  // "20260518" (all-day)
  // "20260518T130000Z" (UTC)
  // "20260518T150000" (Local)
  if (rawStr.size() < 8) return 0;

  int year = std::atoi(rawStr.substr(0, 4).c_str());
  int month = std::atoi(rawStr.substr(4, 2).c_str());
  int day = std::atoi(rawStr.substr(6, 2).c_str());

  if (rawStr.size() == 8 || rawStr[8] != 'T') {
    outAllDay = true;
    return toEpoch(year, month, day, 0, 0, 0);
  }

  int hour = 0;
  int min = 0;
  int sec = 0;
  if (rawStr.size() >= 13) {
    hour = std::atoi(rawStr.substr(9, 2).c_str());
    min = std::atoi(rawStr.substr(11, 2).c_str());
  }
  if (rawStr.size() >= 15) {
    sec = std::atoi(rawStr.substr(13, 2).c_str());
  }

  int64_t epoch = toEpoch(year, month, day, hour, min, sec);
  if (!rawStr.empty() && rawStr.back() == 'Z') {
    // Time was given in UTC, convert to local by adding timezone offset
    epoch += utcOffsetSeconds;
  }
  return epoch;
}

IcalStreamParser::IcalStreamParser(int utcOffsetSeconds, int64_t minLocalEpoch, int64_t maxLocalEpoch,
                                   std::vector<CalendarEvent>& outEvents, size_t maxCapacity)
    : utcOffsetSeconds_(utcOffsetSeconds),
      minLocalEpoch_(minLocalEpoch),
      maxLocalEpoch_(maxLocalEpoch),
      outEvents_(outEvents),
      maxCapacity_(maxCapacity) {
  lineBuffer_.reserve(256);
}

void IcalStreamParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (state_ == State::SAW_CR) {
      if (c == '\n') {
        state_ = State::SAW_LF;
        continue;
      }
      if (c == ' ' || c == '\t') {
        state_ = State::ACCUMULATING;
        continue;
      }
      processLine(lineBuffer_);
      lineBuffer_.clear();
      state_ = State::ACCUMULATING;
      if (c == '\r') {
        state_ = State::SAW_CR;
        continue;
      }
      lineBuffer_.push_back(c);
    } else if (state_ == State::SAW_LF) {
      if (c == ' ' || c == '\t') {
        state_ = State::ACCUMULATING;
        continue;
      }
      processLine(lineBuffer_);
      lineBuffer_.clear();
      state_ = State::ACCUMULATING;
      if (c == '\r') {
        state_ = State::SAW_CR;
        continue;
      }
      if (c == '\n') {
        state_ = State::SAW_LF;
        continue;
      }
      lineBuffer_.push_back(c);
    } else {
      if (c == '\r') {
        state_ = State::SAW_CR;
      } else if (c == '\n') {
        state_ = State::SAW_LF;
      } else {
        if (lineBuffer_.size() < 1024) {
          lineBuffer_.push_back(c);
        }
      }
    }
  }
}

void IcalStreamParser::processLine(const std::string& line) {
  if (line == "BEGIN:VEVENT") {
    inVEvent_ = true;
    currentEvent_ = CalendarEvent{};
    return;
  }

  if (line == "END:VEVENT") {
    if (inVEvent_ && !currentEvent_.title.empty()) {
      if (currentEvent_.endTime <= 0) {
        currentEvent_.endTime = currentEvent_.allDay ? (currentEvent_.startTime + 86400LL) : (currentEvent_.startTime + 3600LL);
      }
      if (currentEvent_.endTime >= minLocalEpoch_ && currentEvent_.startTime <= maxLocalEpoch_) {
        if (outEvents_.size() < maxCapacity_) {
          outEvents_.push_back(currentEvent_);
        }
      }
    }
    inVEvent_ = false;
    return;
  }

  if (!inVEvent_) return;

  if (line.rfind("SUMMARY", 0) == 0) {
    currentEvent_.title = IcalParser::unescapeText(getPropertyValue(line));
  } else if (line.rfind("LOCATION", 0) == 0) {
    currentEvent_.location = IcalParser::unescapeText(getPropertyValue(line));
  } else if (line.rfind("DTSTART", 0) == 0) {
    bool isAllDay = false;
    currentEvent_.startTime = IcalParser::parseDateTime(getPropertyValue(line), utcOffsetSeconds_, isAllDay);
    if (isAllDay) currentEvent_.allDay = true;
  } else if (line.rfind("DTEND", 0) == 0) {
    bool isAllDay = false;
    currentEvent_.endTime = IcalParser::parseDateTime(getPropertyValue(line), utcOffsetSeconds_, isAllDay);
  } else if (line.rfind("DURATION", 0) == 0) {
    std::string val = getPropertyValue(line);
    int64_t durSec = 0;
    int curNum = 0;
    for (char ch : val) {
      if (std::isdigit(ch)) {
        curNum = curNum * 10 + (ch - '0');
      } else if (ch == 'H') {
        durSec += curNum * 3600LL;
        curNum = 0;
      } else if (ch == 'M') {
        durSec += curNum * 60LL;
        curNum = 0;
      } else if (ch == 'S') {
        durSec += curNum;
        curNum = 0;
      } else if (ch == 'D') {
        durSec += curNum * 86400LL;
        curNum = 0;
      }
    }
    if (durSec > 0 && currentEvent_.endTime <= 0) {
      currentEvent_.endTime = currentEvent_.startTime + durSec;
    }
  }
}

void IcalStreamParser::finish() {
  if (!lineBuffer_.empty()) {
    processLine(lineBuffer_);
    lineBuffer_.clear();
  }
  std::sort(outEvents_.begin(), outEvents_.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.startTime < b.startTime; });
}

bool IcalParser::parse(const std::string& icsData, int utcOffsetSeconds, int64_t minLocalEpoch, int64_t maxLocalEpoch,
                       std::vector<CalendarEvent>& outEvents, size_t maxCapacity) {
  if (icsData.empty()) return false;
  IcalStreamParser parser(utcOffsetSeconds, minLocalEpoch, maxLocalEpoch, outEvents, maxCapacity);
  parser.feed(icsData.data(), icsData.size());
  parser.finish();
  return !outEvents.empty();
}
