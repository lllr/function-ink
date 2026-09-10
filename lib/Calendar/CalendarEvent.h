#pragma once

#include <cstdint>
#include <string>

struct CalendarEvent {
  int64_t startTime = 0;  // Unix timestamp in seconds (local epoch)
  int64_t endTime = 0;    // Unix timestamp in seconds (local epoch)
  std::string title;
  std::string location;
  std::string description;
  bool allDay = false;
};
