#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <string>
#include <vector>

struct CalendarSource {
  std::string name;
  std::string url;
  bool enabled = true;
};

class CalendarConfigStore : public PersistableStore<CalendarConfigStore> {
 private:
  std::vector<CalendarSource> calendars;
  bool loaded_ = false;

  static constexpr size_t MAX_CALENDARS = 8;

  CalendarConfigStore() = default;

  friend class PersistableStore<CalendarConfigStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/calendar_config.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool loadFromFile();
  void ensureLoaded() const;
  void release();

  bool addCalendar(const CalendarSource& cal);
  bool updateCalendar(size_t index, const CalendarSource& cal);
  bool removeCalendar(size_t index);

  const std::vector<CalendarSource>& getCalendars() const {
    ensureLoaded();
    return calendars;
  }
  const CalendarSource* getCalendar(size_t index) const;
  size_t getCount() const {
    ensureLoaded();
    return calendars.size();
  }
  bool hasCalendars() const {
    ensureLoaded();
    return !calendars.empty();
  }
};

#define CALENDAR_CONFIG_STORE CalendarConfigStore::getInstance()
