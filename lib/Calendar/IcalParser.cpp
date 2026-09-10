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

bool IcalParser::parse(const std::string& icsData, int utcOffsetSeconds, int64_t minLocalEpoch, int64_t maxLocalEpoch,
                       std::vector<CalendarEvent>& outEvents, size_t maxCapacity) {
  if (icsData.empty()) return false;

  // Step 1: Unfold lines
  std::vector<std::string> lines;
  lines.reserve(128);

  std::stringstream ss(icsData);
  std::string rawLine;
  while (std::getline(ss, rawLine)) {
    // Strip trailing \r
    while (!rawLine.empty() && (rawLine.back() == '\r' || rawLine.back() == ' ')) {
      rawLine.pop_back();
    }
    if (rawLine.empty()) continue;

    if ((rawLine[0] == ' ' || rawLine[0] == '\t') && !lines.empty()) {
      // Continuation of previous line
      lines.back().append(rawLine.substr(1));
    } else {
      lines.push_back(rawLine);
    }
  }

  // Step 2: Parse VEVENT components
  bool inVEvent = false;
  CalendarEvent currentEvent;

  for (const auto& line : lines) {
    if (line == "BEGIN:VEVENT") {
      inVEvent = true;
      currentEvent = CalendarEvent{};
      continue;
    }

    if (line == "END:VEVENT") {
      if (inVEvent && !currentEvent.title.empty()) {
        if (currentEvent.endTime <= 0) {
          // Default duration: 1 hour if not specified, or end of day if all-day
          currentEvent.endTime = currentEvent.allDay ? (currentEvent.startTime + 86400LL) : (currentEvent.startTime + 3600LL);
        }

        // Check if event overlaps with [minLocalEpoch, maxLocalEpoch]
        if (currentEvent.endTime >= minLocalEpoch && currentEvent.startTime <= maxLocalEpoch) {
          if (outEvents.size() < maxCapacity) {
            outEvents.push_back(currentEvent);
          }
        }
      }
      inVEvent = false;
      continue;
    }

    if (!inVEvent) continue;

    if (line.rfind("SUMMARY", 0) == 0) {
      currentEvent.title = unescapeText(getPropertyValue(line));
    } else if (line.rfind("LOCATION", 0) == 0) {
      currentEvent.location = unescapeText(getPropertyValue(line));
    } else if (line.rfind("DESCRIPTION", 0) == 0) {
      currentEvent.description = unescapeText(getPropertyValue(line));
    } else if (line.rfind("DTSTART", 0) == 0) {
      bool isAllDay = false;
      currentEvent.startTime = parseDateTime(getPropertyValue(line), utcOffsetSeconds, isAllDay);
      if (isAllDay) currentEvent.allDay = true;
    } else if (line.rfind("DTEND", 0) == 0) {
      bool isAllDay = false;
      currentEvent.endTime = parseDateTime(getPropertyValue(line), utcOffsetSeconds, isAllDay);
    }
  }

  // Sort events chronologically
  std::sort(outEvents.begin(), outEvents.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.startTime < b.startTime; });

  return true;
}
