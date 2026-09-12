#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CalendarEvent.h"

class IcalStreamParser {
 public:
  IcalStreamParser(int utcOffsetSeconds, int64_t minLocalEpoch, int64_t maxLocalEpoch,
                   std::vector<CalendarEvent>& outEvents, size_t maxCapacity = 50);

  void feed(const char* data, size_t len);
  void finish();

 private:
  enum class State {
    ACCUMULATING,
    SAW_CR,
    SAW_LF
  };

  int utcOffsetSeconds_;
  int64_t minLocalEpoch_;
  int64_t maxLocalEpoch_;
  std::vector<CalendarEvent>& outEvents_;
  size_t maxCapacity_;

  State state_ = State::ACCUMULATING;
  std::string lineBuffer_;
  bool inVEvent_ = false;
  CalendarEvent currentEvent_;

  void processLine(const std::string& line);
};

class IcalParser {
 public:
  /**
   * Parse ICS text content and extract events overlapping [minLocalEpoch, maxLocalEpoch].
   * @param icsData The complete or streamed ICS string content.
   * @param utcOffsetSeconds Timezone offset to convert UTC ('Z') times into local epoch seconds.
   * @param minLocalEpoch Start of filter window (e.g. midnight today in local epoch seconds).
   * @param maxLocalEpoch End of filter window (e.g. end of 3rd day in local epoch seconds).
   * @param outEvents Output vector where parsed events are appended (up to maxCapacity).
   * @param maxCapacity Maximum number of events to retain to guard memory.
   */
  static bool parse(const std::string& icsData, int utcOffsetSeconds, int64_t minLocalEpoch, int64_t maxLocalEpoch,
                    std::vector<CalendarEvent>& outEvents, size_t maxCapacity = 50);

  /**
   * Helper to parse an iCal date or date-time string into local epoch seconds.
   * Handles "20260518T130000Z", "20260518T150000", "20260518".
   */
  static int64_t parseDateTime(const std::string& rawStr, int utcOffsetSeconds, bool& outAllDay);

  /**
   * Unescapes RFC 5545 text sequences (\,, \;, \n, \N, \\).
   */
  static std::string unescapeText(const std::string& str);
};
