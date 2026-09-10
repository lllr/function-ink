#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CalendarEvent.h"

class CalendarEventStore : public PersistableStore<CalendarEventStore> {
 private:
  std::vector<CalendarEvent> events;
  bool loaded_ = false;

  static constexpr size_t MAX_EVENTS = 64;

  CalendarEventStore() = default;

  friend class PersistableStore<CalendarEventStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/calendar_events.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool loadFromFile();
  void ensureLoaded() const;
  void release();

  const std::vector<CalendarEvent>& getEvents() const {
    ensureLoaded();
    return events;
  }

  bool setEvents(std::vector<CalendarEvent> newEvents);
  void clear();

  size_t getCount() const {
    ensureLoaded();
    return events.size();
  }

  bool hasEvents() const {
    ensureLoaded();
    return !events.empty();
  }
};

#define CALENDAR_EVENT_STORE CalendarEventStore::getInstance()
